#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "log.h"
#include "thz_com.h"

#define PROG "set_time"

struct datadef {
	int pos;
	int len;
	int digits;
	char * name;
};

const struct datadef thz_time[] = {
	{ 0, 1, 0, "Weekday" },
	{ 1, 1, 0, "Hour" },
	{ 2, 1, 0, "Minute" },
	{ 3, 1, 0, "Second" },
	{ 4, 1, 0, "Year" },
	{ 5, 1, 0, "Month" },
	{ 6, 1, 0, "Day" },
	{ -1,  0, 0, NULL } // terminator
};

const double p10[] = { 1, 10, 100, 1000 };

char * rawval(BUF * buf, const struct datadef * dd) {
	static char out[64];
	BUF * c = buf + dd->pos;
	int len = dd->len;
	if (len > 0) {
		sprintf(out, "%02hhx", c[0]);
		for (int i=1; i<len; ++i)
			sprintf(out+i*3-1, " %02hhx", c[i]);
	} else {
		for (int i=0; i<8; ++i)
			out[i] = ((*c << i) & 0x80) ? '1' : '0';
		out[8] = 0;
		sprintf(out+8, "  %02hhx", c[0]);
	}	
	return out;
}

double conv(BUF * buf, const struct datadef * dd) {
	BUF * c = buf + dd->pos;
	int len = dd->len;
	if (len > 0) {
		signed int ival = c[0];
		for (int i=1; i<len; ++i) {
			ival <<= 8;
			ival += c[i];
		}
		if (c[0]&0x80 && len<sizeof(signed int))
			ival -= 1 << (len*8); // two's complement
		double fval = ival;
		return fval / p10[dd->digits];
	} else {
		int shift = - dd->len;
		return (*c >> shift) & 1;
	}
}

int main(int argc, char * argv[])
{
	if (argc < 2) {
		EPRINT("Usage: %s <serial device>", argv[0]);
		exit(1);
	}
	mylog_progname(PROG);

	char * dev = argv[1];
	mylog("%s opening %s", argv[0], dev);
	reopen_com(dev);

	BUF buf[1024];
	
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

	if (req(REQ_FIRMWARE, buf, sizeof(buf)) >= 2)
		mylog("version: %.2f", fp(buf, 2));

	int got;

	got = req(REQ_TIMEDATE, buf, sizeof(buf));
	dump("datetime", buf, got);
	if (got == 7) {
#if 0
		for (const struct datadef * dd=thz_time; dd->pos >= 0; ++dd) {
			if (dd->pos+dd->len > got)
				continue;
			double val = conv(buf, dd);
			mylog("Offset %2d (%s): %.*f (%s)", dd->pos, dd->name?dd->name:"", dd->digits, val, rawval(buf, dd));
		}
#endif
		mylog("thz_time: %02hhd.%02hhd.%02hhd %02hhd:%02hhd:%02hhd (day %d)", buf[6], buf[5], buf[4], buf[1], buf[2], buf[3], buf[0]);	
	}

	return 0;

#if 0
	time_t now = time(NULL);
	struct tm * tmp = localtime(&now);
	sleep(1);
	ping();
	BUF timebuf[] = {
		tmp->tm_wday > 0 ? tmp->tm_wday-1 : 7,
		tmp->tm_hour,
		tmp->tm_min,
		tmp->tm_sec,
		tmp->tm_year - 100,
		tmp->tm_mon + 1,
		tmp->tm_mday
	};
	thz_set(REQ_TIMEDATE, timebuf, sizeof(timebuf));
#endif

	sleep(1);
	ping();
	got = req(REQ_TIMEDATE, buf, sizeof(buf));
	dump("datetime", buf, got);
	if (got == 7) {
		mylog("thz_time: %02hhd.%02hhd.%02hhd %02hhd:%02hhd:%02hhd (day %d)", buf[6], buf[5], buf[4], buf[1], buf[2], buf[3], buf[0]);	
	}
}

