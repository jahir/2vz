#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>

#include "thz2vzs_ts.h"

#define PROG "thz2vzs"
#define VER "0.3.0"
// /path/to/spool/timestamp_uuid_value
#define VZ_SPOOLFMT "%s%llu_%s_%g"

#define READ_INTERVAL 10

#define NUL 0x00
#define SOH 0x01
#define STX 0x02
#define ETX 0x03
#define ACK 0x06
#define DLE 0x10

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

typedef unsigned char BUF;


struct datadef {
	int pos;
	char * name;
	int decimals;
	char * uuid;
	double trigger;
	// store values from last run
	double lval; // last read value
	double pval; // last posted value
	unsigned long long lts; // timestamp of last read value
	unsigned long long pts; // timestamp of last posted value
	unsigned int posted : 1;
	struct datadef * next;
};

struct config_t {
	char * log;
	char * spool;
	char * port;
	struct datadef * def;
};

// config is global
static struct config_t conf;

// serial device file descriptor
int com_fd = -1;

char * proctitle;
size_t proctitle_size;

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

	FILE* fh = fopen(conf.log, "a");
	if (fh) {	
		fprintf(fh, "%s.%03u %s[%d] %s\n", timebuf, (unsigned)(tv.tv_usec/1000), PROG, getpid(), logbuf);
		fclose(fh);
	} else {
		perror("mylog");
	}	
}

void handle_sig(int signum) {
	if (signum == SIGHUP) {
		mylog("reload on SIGHUP is not implemented yet");
	} else {
		mylog("exit on signal %d (%s)", signum, strsignal(signum));
		exit(EXIT_SUCCESS);
	}
}

/*** config reading **************************************************************/

// safely allocate memory and initialize it. exit programm if it fails!
void * myalloc(size_t size) {
	void * p = calloc(1, size);
	if (p)
		return p;
	mylog("malloc %zd bytes failed: %s", size, strerror(errno));
	exit(EXIT_FAILURE);
}


const char * CONF_SEP = " \r\n";
#define CONFIG_ELEM(c,dest) ((c=strtok(NULL,CONF_SEP)) && (dest=strdup(c)))
#define CONFIG_ELEM_PTR(c,dest) ((c=dest=strtok(NULL,CONF_SEP)))
struct config_t * read_config(char * conffile, struct config_t * conf) {
	memset(conf, 0, sizeof(*conf));
	FILE * fh = fopen(conffile, "r");
	if (!fh) {
		mylog("open config '%s' failed: %s", conffile, strerror(errno));
		return NULL;
	}
	char line[1024];
	char * c;
	int lines = 0, defs = 0, pdefs = 0;
	struct datadef ** def0 = &conf->def;
	while (fgets(line, sizeof(line), fh)) {
		++lines;
		if (line[0] == '#')
			continue; // skip comment
		if ((c = strtok(line, CONF_SEP)) == NULL)
			continue; // skip empty line
		if (!strcmp(c, "log")) {
			if (CONFIG_ELEM(c, conf->log))
				DPRINT("line %d: log to '%s'", lines, conf->log);
			else
				mylog("config error in line %d (log)", lines);
		} else if (!strcmp(c, "spool")) {
			if (CONFIG_ELEM(c, conf->spool))
				DPRINT("line %d: spool to '%s'", lines, conf->spool);
			else
				mylog("config error in line %d (spool)", lines);
		} else if (!strcmp(c, "port")) {
			if (CONFIG_ELEM(c, conf->port))
				DPRINT("line %d: port '%s'", lines, conf->port);
			else
				mylog("config error in line %d (port)", lines);
		} else if (!strcmp(c, "def")) {
			struct datadef * def = myalloc(sizeof(struct datadef));
			char * pos, *dec, *trig;
			int ok = 1;
			if (CONFIG_ELEM_PTR(c, pos) && CONFIG_ELEM(c, def->name) && CONFIG_ELEM_PTR(c, dec)) {
				def->pos = atoi(pos);
				def->decimals = atoi(dec);
				if (CONFIG_ELEM(c, def->uuid) && CONFIG_ELEM_PTR(c, trig)) {
					def->trigger = atof(trig);
					++pdefs;
				}
			} else {
				ok = 1;
			}
			// TODO: check for valid ranges of pos, decimals and trigger
			if (ok) {
				DPRINT("line %d: pos %d name %s decimals %d uuid %s trigger %g", lines, def->pos, def->name, def->decimals, def->uuid, def->trigger);
				*def0 = def;
				def0 = &def->next;
				++defs;
			} else {
				free(def);
			}
		} else {
			mylog("line %d invalid: '%.99s'", lines, line);
		}
	}
	mylog("config has %d value definitions (%d will be posted)", defs, pdefs);
	fclose(fh);
	return conf;
}

/*********************************************************************************/
void reopen_com() {
	if (com_fd >= 0) {
		mylog("device already open, closing");
		close(com_fd);
		sleep(1);
	}
	while (1) {
		mylog("opening %s", conf.port);
		com_fd = open(conf.port, O_RDWR|O_NONBLOCK|O_NOCTTY); // |O_CLOEXEC);
		if (com_fd >= 0)
			break;
		mylog("could not open %s (retry in 10s): %s (%d)", conf.port, strerror(errno), errno);
		sleep(10);
	}

	struct termios newtio;
	memset(&newtio, 0, sizeof(newtio)); /* clear struct for new port settings */
	cfmakeraw(&newtio);
	cfsetspeed(&newtio, B115200);
	tcflush(com_fd, TCIFLUSH);
	tcsetattr(com_fd, TCSANOW, &newtio);

	mylog("opened %s", conf.port);
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


void vzspool(unsigned long long ts, char * uuid, double val) {
	char spoolfile[256];
	snprintf(spoolfile, sizeof(spoolfile), VZ_SPOOLFMT, conf.spool, ts, uuid, val);
	int fd = open(spoolfile, O_CREAT|O_EXCL|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
	if (fd < 0) {
		mylog("ERROR: open %s: %s", spoolfile, strerror(errno));
	} else {
		close(fd);
	}
}

void setproctitle(char * s) {
	memset(proctitle, 0, proctitle_size);
	snprintf(proctitle, proctitle_size, "%s %s", PROG, s);
}

int trigger(struct datadef * def, double val) {
 	if (def->trigger > 0.0) {
		return (fabs(val - def->pval) > def->trigger);
	} else if (def->trigger < 0.0) {
		double trg = - def->trigger;
		double val0 = def->pval;
		return ((val0==0 && val!=0) || (val0!=0 && val==0) || (val<trg && val0>=trg) || (val>=trg && val0<trg));
	}
	return 0; // trigger == 0 means no trigger set
}

int main(int argc, char * argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <config file>\n", argv[0]);
		exit(1);
	}
	if (!read_config(argv[1], &conf)) {
		exit(EXIT_FAILURE);
	}
	proctitle = argv[0]; // TODO: make it safe 
	proctitle_size = strlen(argv[0])+strlen(argv[1])+2;
	setproctitle("startup");

	mylog("%s %s (using spool dir %s)", PROG, VER, conf.spool);
	mylog("source ts: %s  compile ts: %s", SOURCE_TS, COMPILE_TS);

	{ // install signal handlers
		struct sigaction action;
		memset(&action, 0, sizeof(struct sigaction));
		action.sa_handler = handle_sig;
		sigaction(SIGTERM, &action, NULL);
		sigaction(SIGQUIT, &action, NULL);			
		sigaction(SIGINT, &action, NULL);			
		sigaction(SIGHUP, &action, NULL);			
	}

	/* start main task */
	reopen_com();

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

	BUF buf[1024];
	if (req(0xfd, buf, sizeof(buf)) >= 2)
		mylog("version: %.2f", fp(buf, 2));

	setbuf(stdout, NULL); // disable buffering on stdout
	while (1) {
		char str[1024];
		size_t len = 0;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		unsigned long long ts = (unsigned long long) tv.tv_sec * 1000 + tv.tv_usec / 1000;

		setproctitle("reading...");
		int got = req(0xfb, buf, sizeof(buf));
		if (got < 77) {
			mylog("data too short (%d)", got);
			reopen_com();
		} else {
			for (struct datadef * def = conf.def; def; def=def->next) {
				if (def->pos > got-2)
					continue;

				double val = fp(buf+def->pos, def->decimals);

				// post at least once per hour (and on first run)
				if (def->uuid && (ts - def->pts > 60*60*1000 ||  trigger(def, val)))
				{
					if (!def->posted && def->lts)
						vzspool(def->lts, def->uuid, def->lval);
					def->posted = 1;
					vzspool(ts, def->uuid, val);
					def->pval = val;
					def->pts = ts;
				} else {
					def->posted = 0;
				}
				def->lval = val;
				def->lts = ts;
				len += snprintf(str+len, sizeof(str)-len, "%c%s %*.*f  ", (def->posted ? '*' : ' '), def->name, def->decimals + 3, def->decimals, val);
			}
			mylog("%s", str);
		}
		setproctitle("pausing...");
		gettimeofday(&tv, NULL);
		unsigned long long dur_us = ts * 1000 - ((unsigned long long) tv.tv_sec * 1000000 + tv.tv_usec);
		useconds_t sleep_us = (READ_INTERVAL * 1000000) - dur_us;
		if (sleep_us > 0)
			usleep(sleep_us);
		setproctitle("ping");
		while (!ping())
			reopen_com();
	}
}

