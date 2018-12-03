#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>
#include <sys/file.h>

#include "log.h"
#include "thz_com.h"

// constants for serial communication
#define CMD_GET 0x00
#define CMD_SET 0x80
#define SOH 0x01
#define STX 0x02
#define ETX 0x03
#define ACK 0x06
#define DLE 0x10

#define REOPEN_WAITTIME 10
#define POLL_TIMEOUT 2

#define EPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)

#if 0
#define DPRINT(format, args...) printf("%s: "format"\n", __FUNCTION__, ##args)
//#define DPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)
#else
#define DPRINT(format, args...) do { /* nothing */ } while (0)
#endif

#if 0
#define DUMP(pre, buf, len) dump(pre, buf, len)
#else
#define DUMP(pre, buf, len) do { /* nothing */ } while (0)
#endif


// serial device file descriptor
static int com_fd = -1;
static struct pollfd pollfds = { -1, POLLIN, 0 };

/*********************************************************************************/
void reopen_com(const char * port) {
	if (com_fd >= 0) {
		mylog("device already open, closing");
		close(com_fd);
		sleep(1);
	}
	while (1) {
		mylog("opening %s", port);
		com_fd = open(port, O_RDWR|O_NONBLOCK|O_NOCTTY); // |O_CLOEXEC);
		if (com_fd >= 0)
			break;
		mylog("could not open %s (retry in 10s): %s (%d)", port, strerror(errno), errno);
		sleep(REOPEN_WAITTIME);
	}
	pollfds.fd = com_fd;

	struct termios newtio;
	memset(&newtio, 0, sizeof(newtio)); /* clear struct for new port settings */
	cfmakeraw(&newtio);
	cfsetspeed(&newtio, B115200);
	tcflush(com_fd, TCIFLUSH);
	tcsetattr(com_fd, TCSANOW, &newtio);

	mylog("opened %s", port);
}

int lock_com(int nb)
{
	int rc = flock(com_fd, LOCK_EX | (nb ? LOCK_NB : 0));
	if (rc)
		mylog("lock(%d) failed: %m", nb);
	return rc;
}

int unlock_com()
{
	return flock(com_fd, LOCK_UN);
}

void dump(char * pre, BUF * buf, ssize_t len)
{
	if (*pre != '\0')
		printf("%s:", pre);
	for(int i=0; i<len; ++i)
		printf(" %02hhx", buf[i]);
	printf(" (%zd)\n", len);
}

int rx(BUF * buf, size_t bufsize)
{
	int retval = poll(&pollfds, 1, POLL_TIMEOUT*1000);

	if (retval > 0) {
		if (pollfds.revents & POLLIN) {
			int got = read(com_fd, buf, bufsize);
			if (got < 0) {
				perror("rx: read()");
			} else {
				if (got == 0)
					EPRINT("eof");
				return got;
			} 
		} else if (pollfds.revents & POLLERR) {
			EPRINT("poll reported error condition (%hd)", pollfds.revents);
		} else {
			EPRINT("poll revents = %hd", pollfds.revents);
		}
	} else if (retval < 0) {
		perror("rx: poll()");
	} else { // 0
		DPRINT("timeout");
	}

	return -1;
}

int rxx(BUF * buf, size_t bufsize, int want)
{
	size_t got = 0;
	while (got < want) {
		ssize_t rc = rx(buf+got, bufsize-got);
		if (rc <= 0)
			return -1;
		else
			got += rc;
	}
	return got;
}

int ack()
{
	BUF buf[] = { DLE, STX };
	write(com_fd, buf, 2);
	int rc = rx(buf, 1);
	if (rc > 0 && buf[0] == DLE) {
		DPRINT("ack ok");
		return 1;
	} else {
		EPRINT("ack failed");
		return 0;
	}
}

int ping()
{
	BUF buf;
	int got = -1;
	lock_com(0);
	for (int i=5; i>0 && got<0; --i) {
		buf = STX;
		DPRINT("tx: STX");
		write(com_fd, &buf, 1);
		got = rx(&buf, sizeof(buf));
	}
	if (got > 0)
		DUMP("rx", &buf, got);
	unlock_com();
	return (got > 0);
}

BUF checksum(BUF * buf, size_t len)
{
	BUF sum = 0;
//	DPRINT("[checksum] %zu bytes", len);
	for(size_t i=0; i<len; ++i) {
		if (i == 2) // skip checksum byte
			continue;
		sum += buf[i];
//		DPRINT("[checksum] %2zu. %02hhx => %02hhx", i, buf[i], sum);
	}
	return sum;
}

int req(BUF cmd, BUF * outbuf, size_t bufsize)
{
	return req2(&cmd, 1, outbuf, bufsize);
}

int req2(const BUF * cmd, size_t cmdlen, BUF * outbuf, size_t bufsize)
{
	size_t txlen = 5 + cmdlen;
	int got;
	BUF buf[1024] = { SOH, CMD_GET, 0 }; 
	memcpy(buf+3, cmd, cmdlen);
	buf[txlen-2] = DLE;
	buf[txlen-1] = ETX;

	lock_com(0);

	// tx command
	buf[2] = checksum(buf, txlen-2);
	DUMP("tx", buf, txlen);
	write(com_fd, buf, txlen);
	
	// rx ack
	got = rxx(buf, sizeof(buf), 2);
	if (got != 2) {
		unlock_com();
		EPRINT("ACK timeout (%d)", got);
		return -1;
	}
	DUMP("rx", buf, got);
	if (buf[0] != DLE && buf[1] != STX) {
		unlock_com();
		EPRINT("bad ACK (%02hhx %02hhx)", buf[0], buf[1]);
		return -1;
	}
	// tx ack
	buf[0] = DLE;
	write(com_fd, buf, 1);
	DUMP("tx", buf, 1);

	// rx data
	BUF * pos = buf;
	BUF * endpos = buf + sizeof(buf);
	int escape = 0;
	int skip = 0;
	while (pos < endpos) {
		got = rx(pos, 1);
		if (got < 0) {
			EPRINT("data timeout (%d)", got);
			DUMP("rx", buf, pos-buf);
			unlock_com();
			return -1;
		} else if (got != 1) {
			continue;
		}
		if (escape) {
			if (*pos == DLE) {
				escape = 0; // skip escaped DLE
			} else if (*pos == ETX) {
				++pos;
				break; // end of transmission
			} else {
				EPRINT("bad escaped character %02hhx", *pos);
				dump("rx", buf, pos-buf);
				unlock_com();
				return -1;
			}
		} else if (skip) {
			if (*pos != 0x18)
				EPRINT("bad character after 0x2b: %02hx", *pos);
			skip = 0;
		} else { // no special char received before
			if (*pos == DLE)
				escape = 1;
			else if (*pos == 0x2b)
				skip = 1;
			++pos;
		}
	}
	size_t len = pos-buf;
	DUMP("rx", buf, len);
	ack();
	unlock_com();
	if (pos >= endpos) {
		EPRINT("message too long (%zu>%zu)", len, sizeof(buf));
		return -1;
	}

	char * err = NULL;
	size_t datalen = pos - buf - 6;
	if (len < 6)
		err = "message too short";
	else if (buf[0] != SOH)
		err = "no SOH at start";
	else if (buf[1] == 0x02) 
		err = "request checksum error";
	else if (buf[1] != CMD_GET)
		err = "request error";
	else if (*(pos-2) != DLE || *(pos-1) != ETX)
		err = "missing terminator";
	else if (buf[2] != checksum(buf, len-2))
		err = "reply checksum error";
	else if (datalen > bufsize)
		err = "buffer too small";

	if (err != NULL) {
		dump("rx", buf, len);
		EPRINT("req error: %s", err);
		return -1;
	}	

	memcpy(outbuf, buf+4, datalen);
	return datalen;
}



int thz_set(BUF cmd, BUF * inbuf, size_t ilen)
{
	int got;
	BUF buf[1024] = { SOH, CMD_SET, 0, cmd };

	size_t in = 0, out = 4;
	for (; in<ilen && out<sizeof(buf)-3; ++in) {
		buf[out] = inbuf[in];
		if (inbuf[in] == 0x2b)
			buf[++out] = 0x18;
		else if (inbuf[in] == 0x10)
			buf[++out] = 0x10;
		++out;
	}
	if (in != ilen)
		return -1;
	
	buf[2] = checksum(buf, out);
	buf[out++] = DLE;
	buf[out] = ETX;
	DUMP("tx", buf, out+1);
	write(com_fd, buf, 6);

	got = rxx(buf, sizeof(buf), 2);
	if (got > 0) {
		DUMP("rx", buf, got);
		if (buf[0] == DLE && buf[1] == STX) {
			buf[0] = DLE;
			write(com_fd, buf, 1);
			DUMP("tx", buf, 1);
			return 1;
		}
	}
	return 0;
}


double fp(BUF * buf, int decimals)
{
	if (decimals >= 0) {
		signed short val = buf[0];
		val <<= 8;
		val += buf[1];
		double f = val;
		if (decimals == 1)
			f /= 10;
		else if (decimals == 2)
			f /= 100;
		DPRINT("%02hhx %02hhx => %hd => %f", buf[0], buf[1], val, f);
		return f;
	} else { // 0 < decimals <= 8
		int shift = - decimals - 1;
		return (*buf >> shift) & 1;
	}
}


