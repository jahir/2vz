#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "thz_com.h"

#define EPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)

#if 0
#define DPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)
#define DUMP(pre, buf, len) dump(pre, buf, len)
#else
#define DPRINT(format, args...) do { /* nothing */ } while (0)
#define DUMP(pre, buf, len) do { /* nothing */ } while (0)
#endif

typedef unsigned char BUF;

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

