#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <linux/input.h>
#include <signal.h>

#include "ev2vzs_ts.h"

#define PROG "ev2vzs"
#define VER "0.3.0"
// /path/to/spool/timestamp_uuid_value
#define VZ_SPOOLFMT "%s%llu_%s_%g"

// DEBUG
#if 0
#define DPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)
#define DUMP(pre, buf, len) dump(pre, buf, len)
#else
#define DPRINT(format, args...) do { /* nothing */ } while (0)
#define DUMP(pre, buf, len) do { /* nothing */ } while (0)
#endif

#define DIM(vec) (sizeof(vec)/sizeof(vec[0]))

typedef unsigned long long TSMS;

// config /////////////////////////

struct tariff {
	uint16_t code; // button code (from struct input_event)
	const char * name; // channel name (for logging)
	const char * uuid; // VZ UUID
	TSMS ts;  // timestamp of last post
};

struct channel {
        struct tariff peak; // peak tariff (the only one if there's no off-peak)
        struct tariff offpeak; // off-peak tariff (if set)
        double val; // value per impulse
        int * opbs; // pointer to button_state value of off-peak button
        unsigned int act : 1; // active tariff
        struct channel * next;
};

struct config_t {
	char * log;
	char * spool;
	char * dev;
	struct channel * chan;
};

struct button {
	uint16_t code;
	const char * name;
};

struct button button_list[] = {
	{ BTN_LEFT, "L" }, { BTN_RIGHT, "R" }, { BTN_MIDDLE, "M" }, { BTN_SIDE, "S" }, { BTN_EXTRA, "E" }, 
};
int button_state[DIM(button_list)];
struct channel * button_channel[DIM(button_list)];

// globals ///////////////////////

static struct config_t conf;
int dev_fd = -1;

/*** logging and signal handling *************************************************/

void mylog_ll(struct timeval * tvp, char *fmt, va_list ap) {
	char logbuf[256];
	vsnprintf(logbuf, sizeof(logbuf), fmt, ap);
	if (conf.log) {
		FILE* fh = fopen(conf.log, "a");
		if (fh) {
			char timebuf[32];
			strftime(timebuf, sizeof(timebuf), "%F %T", localtime(&tvp->tv_sec)); // 2012-10-01 18:13:45
			fprintf(fh, "%s.%03u %s[%d] %s\n", timebuf, (unsigned)(tvp->tv_usec/1000), PROG, getpid(), logbuf);
			fclose(fh);
		} else {
			perror("mylog fopen");
		}
	} else { // fallback to stderr output
		fprintf(stderr, "%s\n", logbuf);
	}
}

void mylog_ts(struct timeval * tvp, char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
void mylog_ts(struct timeval * tvp, char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	mylog_ll(tvp, fmt, ap);
	va_end(ap);
}

void mylog(char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
void mylog(char *fmt, ...) {
	va_list ap;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	va_start(ap, fmt);
	mylog_ll(&tv, fmt, ap);
	va_end(ap);
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

int button_code(char * name, uint16_t * code) {
	for (int i=0; i<DIM(button_list); ++i) {
		struct button * b = &button_list[i];
		if (strcmp(name, b->name))
			continue;
		*code = b->code;
		return i;
	}
	return -1;
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
	int lines = 0, chans = 0;
	struct channel ** ch0 = &conf->chan;
	while (fgets(line, sizeof(line), fh)) {
		char * c;
		++lines;
		if (line[0] == '#' || (c = strtok(line, CONF_SEP)) == NULL)
			continue; // skip comments and empty lines
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
		} else if (!strcmp(c, "device")) {
			if (CONFIG_ELEM(c, conf->dev))
				DPRINT("line %d: device path '%s'", lines, conf->dev);
			else
				mylog("config error in line %d (dev)", lines);
		} else if (!strcmp(c, "button")) {
			struct channel * ch = myalloc(sizeof(struct channel));
			char *button, *val;
			if (CONFIG_ELEM_PTR(c, button) && button_code(button, &ch->peak.code) >= 0 &&
			    CONFIG_ELEM(c, ch->peak.name) && CONFIG_ELEM(c, ch->peak.uuid) &&
			    CONFIG_ELEM_PTR(c, val) && (ch->val = atof(val)))
			{
				DPRINT("line %d: button %s (0x%03hx) name %s uuid %s val %g", lines, button, ch->peak.code, ch->peak.name, ch->peak.uuid, ch->val);
				++chans;
				*ch0 = ch;
				ch0 = &ch->next;
				if (CONFIG_ELEM_PTR(c, button)) { // off-peak tariff given
					int bi; // button index
					if ((bi = button_code(button, &ch->offpeak.code)) >= 0 &&
					    CONFIG_ELEM(c, ch->offpeak.name) && CONFIG_ELEM(c, ch->offpeak.uuid))
					{
						if (button_channel[bi]) {
							mylog("config error in line %d (button): button %s is already used by channel %s/%s",
								lines, button, button_channel[bi]->peak.name, button_channel[bi]->offpeak.name);
						} else {
							button_channel[bi] = ch;
							ch->opbs = &button_state[bi];
							DPRINT("line %d:   off-peak button %s (0x%03hx) name %s uuid %s", 
								lines, button, ch->offpeak.code, ch->offpeak.name, ch->offpeak.uuid);
						}
					} else {
						mylog("config error in line %d (button off-peak)", lines);
					}
				}
			} else {
				mylog("config error in line %d (button)", lines);
				free(ch); // possible mem leak, but doesn't matter
			}
		} else {
			mylog("config line %d invalid: '%.99s'", lines, line);
		}
	}
	fclose(fh);
	return conf;
}

//////////////////////////////////

#define button_pressed(p, n) !!(p[n>>3] & (1u << (n&7)))
void read_button_states() {
	uint8_t keys[(KEY_MAX<<3)+1];
	memset(keys, 0, sizeof(keys));
	ioctl(dev_fd, EVIOCGKEY(sizeof(keys)), keys);
	for (int i=0; i<DIM(button_state); ++i) {
		int code = button_list[i].code;
		button_state[i] = button_pressed(keys, code);
		DPRINT("button_state %d (%s): %d", i, button_list[i].name, button_state[i]);
	}
}

void reopen_device() {
	char * dev_path = conf.dev;
	if (dev_fd >= 0) {
		mylog("device already open, closing");
		close(dev_fd);
		sleep(1);
	}
	
	while (1) {
		mylog("opening %s", dev_path);
		dev_fd = open(dev_path, O_RDWR);
		if (dev_fd >= 0)
			break;
		mylog("could not open %s: %s", dev_path, strerror(errno));
		sleep(5);
	}
	mylog("opened %s", dev_path);
	int version;
	if (ioctl(dev_fd, EVIOCGVERSION, &version))
		perror("evdev ioctl");
	else
		mylog("evdev driver version is %d.%d.%d", version >> 16, (version >> 8) & 0xff, version & 0xff);

	// log some device info
	char devname[64], phys[32];
	if (ioctl(dev_fd, EVIOCGNAME(sizeof(devname)), devname) < 0) {
		perror("evdev ioctl EVIOCGNAME");
		strcpy(devname, "unknown");
	}
	if (ioctl(dev_fd, EVIOCGPHYS(sizeof(phys)), phys) < 0) {
		perror("event ioctl EVIOCGPHYS");
		strcpy(phys, "unknown");
	}
	mylog("device: %s on %s", devname, phys);

	read_button_states();
}


double tv_diff(struct timeval * tv1, struct timeval * tv2) {
	if (tv1->tv_usec < tv2->tv_usec)
		return (tv2->tv_sec - tv1->tv_sec) + (tv2->tv_usec - tv1->tv_usec)/1e6;
	else
		return (tv2->tv_sec - tv1->tv_sec) - (tv1->tv_usec - tv2->tv_usec)/1e6;
}

void vzspool(unsigned long long tsms, const char * uuid, const double val) {
	char spoolfile[256];
	snprintf(spoolfile, sizeof(spoolfile), VZ_SPOOLFMT, conf.spool, tsms, uuid, val);
	int fd = open(spoolfile, O_CREAT|O_EXCL|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
	if (fd < 0) {
		mylog("ERROR: open %s: %s", spoolfile, strerror(errno));
	} else {
		DPRINT("vzspool %s", spoolfile);
		close(fd);
	}
}


int main(int argc, char* argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s </path/to/s0.conf>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	if (!read_config(argv[1], &conf)) {
		exit(EXIT_FAILURE);
	}
	if (!conf.dev) {
		mylog("ERROR: no input device in config");
		exit(EXIT_FAILURE);
	}

	mylog("%s %s (spool dir %s)", argv[0], VER, conf.spool);
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
	reopen_device();

	while (1) {
		struct input_event ev;
		ssize_t rc = read(dev_fd, &ev, sizeof(ev));
		if (rc <= 0) {
			if (rc < 0)
				mylog("read: %s (%d)", strerror(errno), errno);
			else
				mylog("read: EOF");
			reopen_device();
			continue;
		}

		if (ev.type != EV_KEY) // mouse button pressed?
			continue;

		TSMS tsms = (TSMS) ev.time.tv_sec * 1000 + ev.time.tv_usec / 1000;

		// update button states		 
		for (int i=0; i<DIM(button_state); ++i) {
			if (ev.code == button_list[i].code) {
				button_state[i] = ev.value;
				DPRINT("button %d (%s) state changed to %d", i, button_list[i].name, ev.value);
				if (button_channel[i] && button_channel[i]->act != ev.value) {
					struct channel * ch = button_channel[i];
					struct tariff * trf  = *(ch->opbs) ? &ch->offpeak : &ch->peak;
					struct tariff * trf2 = *(ch->opbs) ? &ch->peak : &ch->offpeak;
					mylog("tariff switch: %s -> %s", trf2->name, trf->name);
					if (trf2->ts)
						vzspool(trf2->ts, trf->uuid, 0.0);
					vzspool(tsms, trf2->uuid, 0.0);
					ch->act = ev.value;
				}
				break;
			}
		}

		if  (ev.value != 1) // button released
			continue;
		// find channel
		struct channel * ch;
		for (ch = conf.chan; ch; ch=ch->next) {
			if (ev.code == ch->peak.code)
				break;
		}
		if (!ch)
			continue;

		char msg[64];

		struct tariff * trf = ch->opbs && *(ch->opbs) ? &ch->offpeak : &ch->peak;
		if (trf->ts) {
			TSMS tdiff = tsms - trf->ts;
			double p = (3600.0 * 1000) * ch->val / tdiff;
			snprintf(msg, sizeof(msg), "tdiff %6llu ms   P %8.3f W", tdiff, p);
		} else {
			snprintf(msg, sizeof(msg), "first impulse");
		}
		trf->ts = tsms;

		mylog_ts(&ev.time, "%-13s: %s", trf->name, msg);

		vzspool(tsms, trf->uuid, ch->val);
	}
}

