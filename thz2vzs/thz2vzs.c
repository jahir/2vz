#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include "log.h"
#include "thz_com.h"

#include "thz2vzs_ts.h"

#define PROG "thz2vzs"
#define VER "0.4.0"
// /path/to/spool/timestamp_uuid_value
#define VZ_SPOOLFMT "%s%llu_%s_%g"

#if 0
#define DPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)
#define DUMP(pre, buf, len) dump(pre, buf, len)
#else
#define DPRINT(format, args...) do { /* nothing */ } while (0)
#define DUMP(pre, buf, len) do { /* nothing */ } while (0)
#endif

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
	unsigned long long read_interval;
	unsigned long long min_post_interval;
	struct datadef * def;
};

// config is global
static struct config_t conf;

char * proctitle;
size_t proctitle_size;

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
	// set default values
	memset(conf, 0, sizeof(*conf));
	conf.read_interval = 60;

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
		} else if (!strcmp(c, "read_interval")) {
			char * s, *endptr;
			if (CONFIG_ELEM_PTR(c, s) && (conf->read_interval=strtoll(s, &endptr, 10)*1e6) > 0 && *endptr == 0) {
				DPRINT("line %d: read_interval %llu us", lines, conf->read_interval);
			} else {
				mylog("config error in line %d (port)", lines);
			}
		} else if (!strcmp(c, "min_post_interval")) {
			char * s, *endptr;
			if (CONFIG_ELEM_PTR(c, s) && (conf->min_post_interval=strtoll(s, &endptr, 10)*1e3) > 0 && *endptr == 0) {
				DPRINT("line %d: min_post_interval %llu ms", lines, conf->min_post_interval);
			} else {
				mylog("config error in line %d (port)", lines);
			}
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
			mylog("config line %d invalid: '%.99s'", lines, line);
		}
	}
	//mylog("config has %d value definitions (%d will be posted)", defs, pdefs);
	fclose(fh);
	return conf;
}

/*********************************************************************************/

void vzspool(unsigned long long ts, char * uuid, double val) {
	char spoolfile[256];
	if (!conf.spool) {
		mylog("warning: vzspool without spool path, check config");
		return;
	}
	snprintf(spoolfile, sizeof(spoolfile), VZ_SPOOLFMT, conf.spool, ts, uuid, val); // TODO: check length 
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
	if (!conf.port) {
		mylog("ERROR: no port in config");
		exit(EXIT_FAILURE);
	}
	proctitle = argv[0]; // TODO: make it safe 
	proctitle_size = strlen(argv[0])+strlen(argv[1])+2;
	setproctitle("startup");

	mylog_progname(PROG);
	if (conf.log)
		mylog_logpath(conf.log);
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
	reopen_com(conf.port);

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
			reopen_com(conf.port);
		} else {
			for (struct datadef * def = conf.def; def; def=def->next) {
				if (def->pos > got-2)
					continue;

				double val = fp(buf+def->pos, def->decimals);

				if (def->uuid && ((conf.min_post_interval && ts-def->pts > conf.min_post_interval) || trigger(def, val)))
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
		{
			struct timeval tv2;
			setproctitle("pausing...");
			gettimeofday(&tv2, NULL);
			unsigned long long dur_us = (tv2.tv_sec - tv.tv_sec) * 1e6 + (tv2.tv_usec - tv.tv_usec);
			useconds_t sleep_us = conf.read_interval - dur_us;
			if (sleep_us > 0)
				usleep(sleep_us);
		}
		setproctitle("ping");
		while (!ping())
			reopen_com(conf.port);
	}
}

