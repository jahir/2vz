#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <sys/select.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>


#define NUL 0x00
#define SOH 0x01
#define STX 0x02
#define ETX 0x03
#define ACK 0x06
#define DLE 0x10

#define EPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)

#if 0
#define DPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)
#define DUMP(pre, buf, len) dump(pre, buf, len)
#else
#define DPRINT(format, args...) do { /* nothing */ } while (0)
#define DUMP(pre, buf, len) do { /* nothing */ } while (0)
#endif

typedef unsigned char BUF;

// serial device file descriptor
int fd;

void mylog(char *fmt, ...)
{
	va_list ap;
	struct timeval tv;
	char timebuf[32];
	char logbuf[256];

	gettimeofday(&tv, NULL);
	strftime(timebuf, sizeof(timebuf), "%F %T", localtime(&tv.tv_sec)); // 2012-10-01 18:13:45.678

	va_start(ap, fmt);
	vsnprintf(logbuf, sizeof(logbuf),fmt, ap);
	va_end(ap);
	
	printf("%s.%03ld %s\n", timebuf, tv.tv_usec/1000, logbuf);
	
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
	FD_SET(fd, &rfds);
	struct timeval tv = { 2, 0 };
	int retval = select(fd+1, &rfds, NULL, NULL, &tv);

	if (retval == -1) {
	       perror("select()");
	} else if (retval) {
		int got = read(fd, buf, bufsize);
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
	write(fd, buf, 2);
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
	BUF c;
	for (int i=5; i>0; --i) {
		c = STX;
		write(fd, &c, 1);
		DPRINT("tx: STX");
		
		int got = rx(&c, sizeof(c));
		if (got > 0) {
			DUMP("rx", &c, got);
			return 1;
		}
	}
	return 0;
}


int checksum(BUF * buf, size_t len)
{
	unsigned char sum = 1;
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
	write(fd, buf, 6);
	
	got = rxx(buf, sizeof(buf), 2);
	if (got > 0) {
		DUMP("rx", buf, got);
		if (buf[0] == DLE && buf[1] == STX) {
			buf[0] = DLE;
			write(fd, buf, 1);
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
		EPRINT("message too long (>%u)", sizeof(buf));
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
	double f = (signed short) ((*buf) * 256 + *(buf+1));
	if (decimals == 1)
		f /= 10;
	else if (decimals == 2)
		f /= 100;
	return f;
}

char * rawval(BUF * s)
{
	static char buf[64];
	snprintf(buf, sizeof(buf), "%02hhx %02hhx", s[0], s[1]);
	return buf;
}

void parse_ts(char * buf, BUF * raw)
{
	int time_val = raw[0] + (raw[1] << 8);
	int date_val = raw[2] + (raw[3] << 8);
	sprintf(buf, "%02hhu.%02hhu. %02hhu:%02hhu",
		date_val/100, date_val%100,
		time_val/100, time_val%100
	);
}


int main(int argc, char * argv[])
{
	if (argc < 2) {
		EPRINT("Usage: %s <serial device>", argv[0]);
		exit(1);
	}

	char * dev = argv[1];
	mylog("%s opening %s", argv[0], dev);
	fd = open(dev, O_RDWR|O_NONBLOCK|O_NOCTTY); // |O_CLOEXEC);
	if (fd < 0) {
		perror("open()");
		exit(1);
	}

	struct termios oldtio, newtio;
	BUF buf[1024];
	
	tcgetattr(fd, &oldtio); /* save current serial port settings */
	memset(&newtio, 0, sizeof(newtio)); /* clear struct for new port settings */
	
	cfmakeraw(&newtio);
	cfsetspeed(&newtio, B115200);
//	newtio.c_cc[VMIN] = 0; // get at least this number of characters
//	newtio.c_cc[VTIME] = 10; // timeout for reading
	
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &newtio);

	int pingrc;
	for (int i=3; i>0; --i) {
		pingrc = ping();
		mylog("ping %s", pingrc ? "ok" : "failed");
		if (pingrc)
			break;
	}
	if (!pingrc) {
		mylog("no ping reply");
		exit(1);
	}

	if (req(0xfd, buf, sizeof(buf)) >= 2)
		mylog("version: %.2f", fp(buf, 2));

	do {
		int got = req(0xd1, buf, sizeof(buf));
		if (got < 62) {
			mylog("data too short (%d)", got);
		} else {
			mylog("got %d octets", got);
			//dump("data", buf, 16); dump("data", buf+16, 16); dump("data", buf+32, 16); dump("data", buf+48, 14); 
			int err_cnt = buf[0];
			mylog ("%d errors", err_cnt);
			for (int i=0; i<err_cnt; ++i) {
				char err_ts[16];
				BUF * p = buf+2+i*6;
				int err_code = p[0];
				parse_ts(err_ts, p+2);
				mylog("error %2d: %s code %02d", i+1, err_ts, err_code);
			}
		}
#if 0
		sleep(10);
		while (!ping())
			sleep(1);
#endif
	} while (0);
}

