#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include "log.h"


#define EPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)

#if 0
#define DPRINT(format, args...) printf("%s: "format"\n", __FUNCTION__, ##args)
#else
#define DPRINT(format, args...) do { /* nothing */ } while (0)
#endif

#if 0
#define DUMP(pre, buf, len) dump(pre, buf, len)
#else
#define DUMP(pre, buf, len) do { /* nothing */ } while (0)
#endif

static const char * logfile;
static const char * progname;

void mylog_logpath(const char * log) {
	logfile = log;
}

void mylog_progname(const char * prog) {
	progname = prog;
}

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

	if (logfile) {
		FILE* fh = fopen(logfile, "a");
		if (fh) {	
			fprintf(fh, "%s.%03u %s[%d] %s\n", timebuf, (unsigned)(tv.tv_usec/1000), progname, getpid(), logbuf);
			fclose(fh);
		} else {
			perror("mylog fopen");
		}
	} else {
		fprintf(stderr, "%s.%03u [%d] %s\n", timebuf, (unsigned)(tv.tv_usec/1000), getpid(), logbuf);
	}
}

