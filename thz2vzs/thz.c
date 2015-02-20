#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "thz_com.h"

#define PROG "thz"

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
const struct datadef thz_hist[] = {
	{ 0, 2, 0, "compressorHeating" },
	{ 2, 2, 0, "compressorCooling" },
	{ 4, 2, 0, "compressorDHW" },
	{ 6, 2, 0, "boosterDHW" },
	{ 8, 2, 0, "boosterHeating" },
	{ -1,  0, 0, NULL } // terminator
};

const struct datadef datadefs[] = {
	{ 0,   2, 1, "collectorTemp" },
	{ 2,   2, 1, "TAussen" },
	{ 4,   2, 1, "TVorlauf" },
	{ 6,   2, 1, "TRuecklauf" },
	{ 8,   2, 1, "THeissgas" },
	{ 10,  2, 1, "TWarmwasser" },
#if 1
	{ 12,  2, 1, "flowTempHC2" },
	{ 14,  2, 1, "insideTemp" },
	{ 16,  2, 1, "TVerdampfer" },
	{ 18,  2, 1, "TVerfluessig" },

	{ 20,  0, 0, "dhwPump"	},
	{ 20, -1, 0, "heatingCircuitPump" },
	{ 20, -2, 0, "" },
	{ 20, -3, 0, "solarPump" },
	{ 20, -4, 0, "mixerOpen" },
	{ 20, -5, 0, "mixerClosed" },
	{ 20, -6, 0, "heatPipeValve" },
	{ 20, -7, 0, "diverterValve" },

	{ 21,  0, 0, "boosterStage3" },
	{ 21, -1, 0, "boosterStage2" },
	{ 21, -2, 0, "boosterStage1" },
	{ 21, -3, 0, ""	},
	{ 21, -4, 0, "" },
	{ 21, -5, 0, "" },
	{ 21, -6, 0, "" },
	{ 21, -7, 0, "compressor" },

	{ 22,  0, 0, "rvuRelease" },
	{ 22, -1, 0, "ovenFireplace" },
	{ 22, -2, 0, "STB" },
	{ 22, -3, 0, "" },
	{ 22, -4, 0, "!highPressureSensor" },
	{ 22, -5, 0, "!lowPressureSensor" },
	{ 22, -6, 0, "evaporatorIceMonitor" },
	{ 22, -7, 0, "signalAnode" },

	{ 23,  2, 1, "outputVentilatorPower" },
	{ 25,  2, 1, "inputVentilatorPower" },
	{ 27,  2, 1, "mainVentilatorPower" },
	{ 29,  2, 1, "outputVentilatorSpeed" },
	{ 31,  2, 1, "inputVentilatorSpeed" },
	{ 33,  2, 1, "mainVentilatorSpeed"},
	{ 35,  2, 1, "outside_tempFiltered" },
	{ 37,  2, 1, "relHumidity" },
	{ 39,  2, 1, "dewPoint" },

	{ 41,  2, 2, "Niederdruck" },
	{ 43,  2, 2, "Hochdruck" },

	{ 45,  4, 1, "actualPower_Qc" },
	{ 49,  4, 1, "actualPower_Pel" },
	{ 51,  2, 0, NULL },
	{ 53,  2, 0, NULL },
	{ 55,  2, 0, NULL },
	{ 57,  2, 0, NULL },
	{ 59,  2, 0, NULL },
	{ 61,  2, 0, NULL },
	{ 63,  2, 0, NULL },
	{ 65,  2, 1, "TKuehlung" },
	{ 67,  2, 1, "TVerdampfAus" },
	{ 69,  2, 0, NULL },
	{ 71,  2, 0, NULL },
	{ 73,  2, 0, NULL },
	{ 75,  2, 0, NULL },
#endif
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

	int maxDescLen = 0;
	for (const struct datadef * dd=datadefs; dd->pos >= 0; ++dd) {
		if (dd->name && strlen(dd->name) > maxDescLen)
			maxDescLen = strlen(dd->name);
	} 
	do {
		int got;
#if 1
		got = req(REQ_GLOBAL, buf, sizeof(buf));
		if (got < 0) {
			mylog("bad data (%d)", got);
		} else if (got < 77) {
			mylog("data too short (%d)", got);
		} else {
			//dump("data", buf, 25); dump("data", buf+25, 25); dump("data", buf+50, 27);
			for (const struct datadef * dd=datadefs; dd->pos >= 0; ++dd) {
				if (dd->pos > got-dd->len)
					continue;
				double val = conv(buf, dd);
				int fhempos = (dd->pos + 2) * 2;
				if (dd->len < -3)
					++fhempos;
				if (dd->len > 0)
					mylog("Offset %2d   %3d (%-*s): %6.*f (%s)", dd->pos, fhempos, maxDescLen, dd->name?dd->name:"", dd->digits, val, rawval(buf, dd));
				else
					mylog("Offset %2d.%d %3d (%-*s): %6.*f (%s)", dd->pos, -dd->len, fhempos, maxDescLen, dd->name?dd->name:"", dd->digits, val, rawval(buf, dd));
			}
		}
#endif

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

		got = req(REQ_HIST, buf, sizeof(buf));
		dump("hist", buf, got);
		for (const struct datadef * dd=thz_hist; dd->pos >= 0; ++dd) {
			if (dd->pos+dd->len > got)
				continue;
			double val = conv(buf, dd);
			mylog("Offset %2d (%s): %.*f (%s)", dd->pos, dd->name?dd->name:"", dd->digits, val, rawval(buf, dd));
		}
#if 0
		sleep(10);
		while (!ping())
			sleep(1);
#endif
	} while (0);
}

