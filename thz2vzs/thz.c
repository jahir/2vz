#include <stdint.h>
#include <stdlib.h>
#include <alloca.h>
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

#include "timestamps.h"


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

struct datadef {
	int pos;
	char * name;
	int digits;
};
struct datadef datadefs[] = {
	{ 0,	NULL,	0 },
	{ 2,	"TAussen",	1 },
	{ 4,	"TVorlauf",	1 },
	{ 6,	"TRuecklauf",	1 },
	{ 8,	"THeissgas",	1 },
	{ 10,	"TWarmwasser",	1 },
	{ 12,	NULL,	0 },
	{ 14,	NULL,	0 },
	{ 16,	"TVerdampfer",	1 },
	{ 18,	"TVerfluessig",	1 },
	{ 20,	NULL,	0 },
	{ 23,	NULL,	0 },
	{ 25,	NULL,	0 },
	{ 27,	NULL,	0 },
	{ 29,	"Abluft Ist",	0 },
	{ 31,	"Zuluft Ist",	0 },
	{ 33,	"?Fortluft Ist", 0},
	{ 35,	"PHeizkreis",	2 },

	{ 41,	"Niederdruck",	2 },
	{ 43,	"Hochdruck",	2 },

	{ 45,	NULL,	1 },
	{ 47,	NULL,	1 },
	{ 49,	NULL,	1 },
	{ 51,	NULL,	1 },
	{ 53,	NULL,	1 },
	{ 55,	NULL,	1 },
	{ 57,	NULL,	1 },
	{ 59,	NULL,	1 },
	{ 61,	NULL,	1 },
	{ 63,	NULL,	1 },
	{ 65,	"TKuehlung",	1 },
	{ 67,	"TVerdampfAus",	1 },
	{ 69,	NULL,	1 },
	{ 71,	NULL,	1 },
	{ 73,	NULL,	1 },
	{ 75,	NULL,	1 },

	{ -1, NULL, -1, NULL }
};

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
	ack();
	if (pos >= endpos) {
		EPRINT("message too long (>%u)", sizeof(buf));
		return -1;
	}
	size_t len = pos-buf;

	char * err = NULL;
	if (len < 6)
		err = "message too short";
	else if (buf[0] != SOH || buf[1] != NUL)
		err = "invalid header";
	else if (*(pos-2) != DLE || *(pos-1) != ETX)
		err = "missing terminator";
	else if (buf[2] != checksum(buf+3, len-5)) {
		err = alloca(256);
		snprintf(err, 256, "checksum error: %02hhx != %02hhx", buf[2], checksum(buf+3, len-5));
	}

	if (err != NULL) {
		dump("rx", buf, len);
		EPRINT("req error: %s", err);
		return -1;
	}
	DUMP("rx", buf, len);

	size_t datalen = pos - buf - 6;
	memcpy(outbuf, buf+4, datalen);
	return datalen;
}

double fp(BUF * buf, int decimals)
{
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
}

char * rawval(BUF * s)
{
	static char buf[64];
	snprintf(buf, sizeof(buf), "%02hhx %02hhx", s[0], s[1]);
	return buf;
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
//		char str[1024];
//		size_t len = 0;

		int got = req(0xfb, buf, sizeof(buf));
		if (got < -1) {
			mylog("bad data (%d)", got);
		} else if (got > 0 && got < 77) {
			mylog("data too short (%d)", got);
		} else {
			//dump("data", buf, 25); dump("data", buf+25, 25); dump("data", buf+50, 27);
			for (int i=0; datadefs[i].pos >= 0; ++i) {
				struct datadef * def = &datadefs[i];
				if (def->pos > got-2)
					continue;
				
				double val = fp(buf+def->pos, def->digits);
				mylog("Offset %d (%s): %.*f (%s)",def->pos, def->name?def->name:"", def->digits, val, rawval(buf+def->pos));

				//len += snprintf(str+len, sizeof(str)-len, "%s %.*f ", name, def->digits, val);
			}
			//mylog("%s", str);
		}
#if 0
		sleep(10);
		while (!ping())
			sleep(1);
#endif
	} while (0);
}

