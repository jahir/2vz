#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#include "log.h"
#include "thz-serial.h"

// serial device file descriptor
static int com_fd = -1;

/*********************************************************************************/
void reopen_com(const char * port) {
	if (com_fd >= 0) {
		//mylog("device already open, closing");
		close(com_fd);
		sleep(1);
	}
	while (1) {
		//mylog("opening %s", port);
		com_fd = open(port, O_RDWR|O_NONBLOCK|O_NOCTTY); // |O_CLOEXEC);
		if (com_fd >= 0)
			break;
		//mylog("could not open %s (retry in 10s): %s (%d)", port, strerror(errno), errno);
		sleep(10);
	}

	struct termios newtio;
	memset(&newtio, 0, sizeof(newtio)); /* clear struct for new port settings */
	cfmakeraw(&newtio);
	cfsetspeed(&newtio, B115200);
	tcflush(com_fd, TCIFLUSH);
	tcsetattr(com_fd, TCSANOW, &newtio);

	//mylog("opened %s", port);
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
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(com_fd, &rfds);
	struct timeval tv = { 2, 0 };
	int retval = select(com_fd+1, &rfds, NULL, NULL, &tv);

	if (retval == -1) {
	       perror("select()");
	} else if (retval) {
		int got = read(com_fd, buf, bufsize);
		if (got < 0) {
			perror("read");
		} else {
			return got;
		}
	} else {
		EPRINT("rx: timeout");
	}

	return -1;
}

int rxx(BUF * buf, size_t bufsize, int want)
{
	size_t got = 0;
	while (got < want) {
		ssize_t rc = rx(buf+got, bufsize-got);
		if (rc < 0)
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
	for (int i=5; i>0; --i) {
		buf = STX;
		write(com_fd, &buf, 1);
		DPRINT("tx: STX");
		
		int got = rx(&buf, sizeof(buf));
		if (got > 0) {
			DUMP("rx", &buf, got);
			return 1;
		}
	}
	return 0;
}

BUF checksum(BUF * buf, size_t len)
{
	BUF sum = 1;
	for(size_t i=0; i<len; ++i) {
		sum += buf[i];
	}
	return sum;
}

int req(int cmd, BUF * outbuf, size_t bufsize)
{
	int got;
	BUF buf[1024];

	buf[0] = SOH;
	buf[1] = NUL;
	buf[3] = cmd;
	buf[4] = DLE;
	buf[5] = ETX;
	buf[2] = checksum(buf+3, 1);
	DUMP("tx", buf, 6);
	write(com_fd, buf, 6);
	
	got = rxx(buf, sizeof(buf), 2);
	if (got > 0) {
		DUMP("rx", buf, got);
		if (buf[0] == DLE && buf[1] == STX) {
			buf[0] = DLE;
			write(com_fd, buf, 1);
			DPRINT("tx: DLE");
		}
	}

	BUF * pos = buf;
	BUF * endpos = buf + sizeof(buf);
	int escape = 0;
	int skip = 0;
	while (pos < endpos) {
		got = rx(pos, 1);
		if (got < 0) {
			DUMP("rx", buf, pos-buf);
			return -1;
		} else if (got > 0) {
			if (escape) {
				if (*pos == DLE) {
					escape = 0; // skip escaped DLE
				} else if (*pos == ETX) {
					++pos;
					break; // end of transmission
				} else {
					EPRINT("bad escaped character %02hhx", *pos);
					dump("rx", buf, pos-buf);
					return -1;
				}
			} else if (skip) {
				if (*pos != 0x18)
					EPRINT("bad character after 0x2b: %02hx", *pos);
				skip = 0;
			} else { // not in escape state
				if (*pos == DLE)
					escape = 1;
				else if (*pos == 0x2b)
					skip = 1;
				++pos;
			}
		}
	}
	size_t len = pos-buf;
	DUMP("rx", buf, len);
	ack();
	if (pos >= endpos) {
		EPRINT("message too long (%u>%u)", len, sizeof(buf));
		return -1;
	}

	char * err = NULL;
	if (len < 6)
		err = "message too short";
	else if (buf[0] != SOH || buf[1] != NUL)
		err = "invalid header";
	else if (*(pos-2) != DLE || *(pos-1) != ETX)
		err = "missing terminator";
	else if (buf[2] != checksum(buf+3, len-5))
		err = "checksum error";

	if (err != NULL) {
		dump("rx", buf, len);
		EPRINT("req error: %s", err);
		return -1;
	}	

	size_t datalen = pos - buf - 6;
	memcpy(outbuf, buf+4, datalen);
	return datalen;
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


