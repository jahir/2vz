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
#include <poll.h>

#include "ev2vzs_ts.h"

#define PROG "ev2vzs"
#define VER "0.5.6"
// /path/to/spool/timestamp_uuid_value
#define VZ_SPOOLFMT "%s%llu_%s_%g"

#if 0
#define DEBUG
#endif
#ifdef DEBUG
#define DPRINT(format, args...) mylog("{%s} "format, __FUNCTION__, ##args)
#define DUMP(pre, buf, len) dump(pre, buf, len)
#else
#define DPRINT(format, args...) do { /* nothing */ } while (0)
#define DUMP(pre, buf, len) do { /* nothing */ } while (0)
#endif

typedef unsigned long long TSMS;
#define CALC_TSMS(tv) ((TSMS) tv.tv_sec * 1000 + tv.tv_usec / 1000)

// input event api works with bitfields, so we use some handy macros
// number of qwords (64bit) necessray to store the given number of bits
#define bits64(n)     (((n) + 64 - 1)>>6)
// check if bit n is set in (u)int64 array p
#define bit_get(p, n) (!!(p[(n)>>6] & (1u << ((n)&63))))

// config /////////////////////////

struct tariff {
	const char * name; // channel name (for logging)
	const char * uuid; // VZ UUID
	TSMS ts;  // timestamp of latest event
	int cnt; // accumulated counts, if interval is set (i.e., > 0), is reset to 0 after post
};

struct button {
	uint16_t code;
	const char * name;
};
struct button buttons[] = {
	{ BTN_LEFT, "L" }, { BTN_RIGHT, "R" }, { BTN_MIDDLE, "M" }, { BTN_SIDE, "S" }, { BTN_EXTRA, "E" }, 
	{ 0, NULL }
};

struct channel {
	union {
		struct tariff trf[2]; // tariffs (only the first one is used if there's no off-peak)
		struct {
			struct tariff peak, offpeak;
		};
	};
	struct button *btn_imp, *btn_trf; // buttons codes for impulse and tariff (from struct input_event)
	double val; // value per impulse
	unsigned int act : 1; // active tariff, 0 = peak, 1 = offpeak
	struct channel * next;
};

// map button to channel
struct channel * b2c[KEY_CNT];

struct config_t {
	char * log;
	char * spool;
	char * dev;
	int interval;
	struct timespec read_wait;
	struct channel * chan;
};

// globals ///////////////////////

static struct config_t conf;
int dev_fd = -1;

/*** logging and signal handling *************************************************/

// returns a readable timestamp "YYYY-mm-dd HH:MM:SS.sss" from tvp
const char * strtime(struct timeval * tvp) {
	static char buf[24]; // strlen + \0
	if (strftime(buf, sizeof(buf), "%F %T.", localtime(&tvp->tv_sec)) > 0) {
		int rem =             tvp->tv_usec % 1000000;
		buf[20] = '0' + rem /  100000; rem %= 100000;
		buf[21] = '0' + rem /   10000; rem %=  10000;
		buf[22] = '0' + rem /    1000;
		buf[23] = '\0';
	} else // fall back to unix timestamp
		snprintf(buf, sizeof(buf), "%10ld.%06ld", tvp->tv_sec, tvp->tv_usec);
	return buf;
}

void mylog_ll(struct timeval * tvp, char *fmt, va_list ap) {
	char logbuf[256];
	vsnprintf(logbuf, sizeof(logbuf), fmt, ap);
	if (conf.log) {
		FILE* fh = fopen(conf.log, "a");
		if (fh) {
			fprintf(fh, "%s %s[%d] %s\n", strtime(tvp), PROG, getpid(), logbuf);
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
	mylog("malloc %zd bytes failed: %m", size);
	exit(EXIT_FAILURE);
}

int find_button(char * name, struct button ** btn) {
	for (struct button * b=buttons; b->code>0; ++b)
		if (strcmp(name, b->name) == 0) {
			if (btn != NULL)
				*btn = b;
			return b->code;
		}
	return 0;
}

#define CONFIG_ELEM(c,dest) ((c=strtok(NULL,CONF_SEP)) && (dest=strdup(c)))
#define CONFIG_ELEM_PTR(c,dest) ((c=dest=strtok(NULL,CONF_SEP)))
struct config_t * read_config(char * conffile, struct config_t * conf) {
	const char * CONF_SEP = " \r\n";

	memset(conf, 0, sizeof(*conf));
	memset(b2c, 0, sizeof(b2c));

	FILE * fh = fopen(conffile, "r");
	if (!fh) {
		mylog("open config '%s' failed: %m", conffile);
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
		} else if (!strcmp(c, "interval")) {
			char *p, *endptr;
			if (CONFIG_ELEM(c, p) && *p != '\0') {
				conf->interval = strtol(p, &endptr, 10);
				if (*endptr == '\0')
					DPRINT("line %d: spool interval %d s", lines, conf->interval);
				else
					mylog("config error in line %d (interval)", lines);
			} else
				mylog("config error in line %d (interval)", lines);
		} else if (!strcmp(c, "read_wait")) {
			char *p, *endptr;
			if (CONFIG_ELEM(c, p) && *p != '\0') {
				long ms = strtol(p, &endptr, 10);
				if (*endptr == '\0') {
					DPRINT("line %d: read wait time %ld ms", lines, ms);
					conf->read_wait.tv_nsec = ms * 1000 * 1000; // ms to ns
				}
				else
					mylog("config error in line %d (read_wait)", lines);
			} else
				mylog("config error in line %d (read_wait)", lines);
		} else if (!strcmp(c, "device")) {
			if (CONFIG_ELEM(c, conf->dev))
				DPRINT("line %d: device path '%s'", lines, conf->dev);
			else
				mylog("config error in line %d (dev)", lines);
		} else if (!strcmp(c, "button")) {
			struct channel * ch = myalloc(sizeof(struct channel));
			char *bnam, *val;
			struct button *b;
			if (CONFIG_ELEM_PTR(c, bnam) && find_button(bnam, &b) &&
			    CONFIG_ELEM(c, ch->peak.name) && CONFIG_ELEM(c, ch->peak.uuid) &&
			    CONFIG_ELEM_PTR(c, val) && (ch->val = atof(val)))
			{
				DPRINT("line %d: button %s (0x%03hx) name %s uuid %s val %g", lines, bnam, b->code, ch->peak.name, ch->peak.uuid, ch->val);
				++chans;
				*ch0 = ch; // save ch in current ch0 (which is conf.ch or the previous ch->next) ...
				ch0 = &ch->next; // and let ch0 point to the next pointer (for the next channel)
				ch->btn_imp = b;
				if (b2c[b->code]) {
					mylog("ERROR! channel %s impulse button %s is already used by channel %s", ch->peak.name, bnam, b2c[b->code]->peak.name);
					return NULL;
				}
				b2c[b->code] = ch;
				if (CONFIG_ELEM_PTR(c, bnam)) { // off-peak tariff given, get tariff switch button
					if (find_button(bnam, &b) &&
					    CONFIG_ELEM(c, ch->offpeak.name) && CONFIG_ELEM(c, ch->offpeak.uuid))
					{
						/*if (b->channel) {
							mylog("config error in line %d (button): button %s is already used by channel %s/%s",
								lines, button, b->channel->peak.name, b->channel->offpeak.name);
						} else {
							b->channel = ch;
							DPRINT("line %d:   off-peak button %s (0x%03hx) name %s uuid %s", 
								lines, button, ch->offpeak.code, ch->offpeak.name, ch->offpeak.uuid);
						}*/
						DPRINT("line %d:   off-peak button %s (0x%03hx) name %s uuid %s", 
							lines, bnam, b->code, ch->offpeak.name, ch->offpeak.uuid);
						ch->btn_trf = b;
						if (b2c[b->code]) {
							mylog("ERROR! channel %s tariff button %s is already used by channel %s", ch->peak.name, bnam, b2c[b->code]->peak.name);
							return NULL;
						}
						b2c[b->code] = ch;
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

void update_tariff_states() {
	uint64_t keys[bits64(KEY_CNT)];
	memset(keys, 0, sizeof(keys));
	ioctl(dev_fd, EVIOCGKEY(sizeof(keys)), keys);
	for (struct channel *ch=conf.chan; ch; ch=ch->next)
		if (ch->btn_trf) {
			ch->act = bit_get(keys, ch->btn_trf->code);
			DPRINT("button %s (used for channels %s/%s) state: %d", ch->btn_trf->name, ch->peak.name, ch->offpeak.name, ch->act);
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
		dev_fd = open(dev_path, O_RDWR | (conf.interval > 0 ? O_NONBLOCK : 0) );
		if (dev_fd >= 0)
			break;
		mylog("could not open %s: %m", dev_path);
		sleep(5);
	}
	mylog("opened %s", dev_path);
	{ // set event masks to filter out events we don't need. masking EV_SYN will filter all events, so only EV_MSC is filtered for now
		uint64_t codes[bits64(MSC_CNT)];
		memset(codes, 0, sizeof(codes));
		struct input_mask mask = { EV_MSC, sizeof(codes), (uint64_t)codes };
		if (ioctl(dev_fd, EVIOCSMASK, &mask) < 0)
			mylog("warning: failed to set EV_MSC event mask (%d) (%m)", mask.codes_size);
		else
			DPRINT("set input mask for EV_MSC");
	}

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

	update_tariff_states();
}


double tv_diff(struct timeval * tv1, struct timeval * tv2) {
	if (tv1->tv_usec < tv2->tv_usec)
		return (tv2->tv_sec - tv1->tv_sec) + (tv2->tv_usec - tv1->tv_usec)/1e6;
	else
		return (tv2->tv_sec - tv1->tv_sec) - (tv1->tv_usec - tv2->tv_usec)/1e6;
}

void vzspool(TSMS tsms, const char * uuid, const double val) {
	char spoolfile[256];
	snprintf(spoolfile, sizeof(spoolfile), VZ_SPOOLFMT, conf.spool, tsms, uuid, val);
	int fd = open(spoolfile, O_CREAT|O_EXCL|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
	if (fd < 0) {
		mylog("ERROR: open %s: %m", spoolfile);
	} else {
		DPRINT("vzspool %s", spoolfile);
		close(fd);
	}
}

void dequeue() {
	for (struct channel * ch = conf.chan; ch; ch=ch->next) {
		struct tariff * trf  = &ch->peak;
		if (trf->cnt > 0) {
			DPRINT("spooled channel %s UUID %s ts %lld cnt %d", trf->name, trf->uuid, trf->ts, trf->cnt);
			vzspool(trf->ts, trf->uuid, ch->val * trf->cnt);
			trf->cnt = 0;
		}
		trf = &ch->offpeak;
		if (trf->cnt > 0) {
			DPRINT("spooled offpeak channel %s UUID %s ts %lld cnt %d", trf->name, trf->uuid, trf->ts, trf->cnt);
			vzspool(trf->ts, trf->uuid, ch->val * trf->cnt);
			trf->cnt = 0;
		}
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

	time_t next_spool_time = 0;
	if (conf.interval > 0)
		next_spool_time = time(NULL) / conf.interval * conf.interval + conf.interval;
	int ready = (conf.interval == 0); // poll result. set to fixed value 1 if spool interval is not set
	int spool = 0; // do we have events to send to spool?
	while (1) {
		if (conf.interval > 0) 
		do {
			// calculate next interval
			struct timeval tv = {0, 0};
			struct pollfd fds[1] = { {dev_fd, POLLIN, 0} };
			int timeout;
			if (gettimeofday(&tv, NULL) != 0) {
				mylog("WARNING! gettimeofday: %m");
				tv.tv_sec = time(NULL);
				if (tv.tv_sec == -1)
					tv.tv_sec = next_spool_time - 1;
				tv.tv_usec = 500000; //
			}
			if (spool) {
				if (tv.tv_sec < next_spool_time)
					timeout = (next_spool_time - tv.tv_sec) * 1000 - tv.tv_usec / 1000;
				else
					timeout = 0; // poll events, but return immediately
			} else
				timeout = -1; // nothing to spool, so just wait for the next event
			DPRINT("waiting for event, timeout %d ms", timeout);
			ready = poll(fds, 1, timeout);
			if (ready < 0)
				mylog("ERROR! poll: %m");

			// send stored events to spool? timeout >= 0 means there is something to spool (set above)
			if (timeout == 0 || (tv.tv_sec = time(NULL)) >= next_spool_time) { // hammertime!
				DPRINT("spool time reached, %d events queued", spool);
				if (spool) {
					spool = 0;
					dequeue();
				}
				while (next_spool_time <= tv.tv_sec)
					next_spool_time += conf.interval;
				DPRINT("spooling finished, next spool time: %ld (%s)", next_spool_time, strtime(&(struct timeval){next_spool_time, 0}));
			}
		} while (ready <= 0);

		if (conf.read_wait.tv_nsec > 0)
			nanosleep(&conf.read_wait, NULL);
		struct input_event evs[16]; // mouse input events usually come in packets of 2 (EV_MSC+EV_KEY+EV_SYN), we read a multiple of it
		ssize_t rc = read(dev_fd, evs, sizeof(evs));
		if (rc == 0) {
			mylog("read: EOF??");
			reopen_device();
			continue;
		} else if (rc < 0) {
			if (errno != EAGAIN) {
				mylog("read error: %m");
				reopen_device();
			} else
				DPRINT("read EAGAIN");
			continue;
		}

		// for reference, struct input_event for mouse events:
		// type: EV_SYN EV_KEY EV_MSC
		// with type==EV_KEY:
		// code: BTN_LEFT BTN_RIGHT BTN_MIDDLE ...
		// value: 0 => released, 1 => pressed (and 2 => autorepeat)

		int cnt = rc / sizeof(evs[0]);
		for (int i=0; i<cnt; ++i) {
			struct input_event *ev = &evs[i];
			if (ev->type != EV_KEY) { // mouse button event?
				DPRINT("ignoring event type %d (code 0x%03x value %d)", ev->type, ev->code, ev->value);
				continue;
			} else
				DPRINT("handling event type %d (code 0x%03x value %d)", ev->type, ev->code, ev->value);

			struct channel * ch = b2c[ev->code];
			if (ch) { // channel set for button?
				if (ev->value == 1 && ch->btn_imp && ch->btn_imp->code == ev->code) { // impulse for channel
					struct tariff * trf = &ch->trf[ch->act];
					TSMS tsms = CALC_TSMS(ev->time);
					if (trf->ts) {
						TSMS tdiff = tsms - trf->ts;
						double power = (3600.0 * 1000) * ch->val / tdiff;
						mylog_ts(&ev->time, "%-13s: P = %7.1f W  (delta_t = %6llu ms)", trf->name, power, tdiff);
					} else {
						mylog_ts(&ev->time, "%-13s: first impulse", trf->name);
					}
					trf->ts = tsms;

					if (conf.interval > 0) {
						++(trf->cnt);
						++spool;
					} else
						vzspool(tsms, trf->uuid, ch->val);
				} // s0 impulse button
				else if (ch->btn_trf && ev->code == ch->btn_trf->code) { // tariff button
					if (ev->value != 0 && ev->value != 1) {
						mylog("Warning: ignoring unknown value %d for button %s", ev->value, ch->btn_trf->name);
					} else if (ch->act != ev->value) {
						ch->act = ev->value;
						struct tariff * trf_cur = &ch->trf[ch->act];
						struct tariff * trf_oth = &ch->trf[!ch->act];
						mylog("tariff switch: %s -> %s", trf_oth->name, trf_cur->name);
						if (trf_oth->ts)
							vzspool(trf_oth->ts, trf_cur->uuid, 0.0); // send 0-val with last timestamp of previous tariff for _current_ tariff
						vzspool(CALC_TSMS(ev->time), trf_oth->uuid, 0.0); // send 0-val with current timestamp for _previous_ tariff
					} else {
						mylog("Warning: tariff button %s event (%d) without state change", ch->btn_trf->name, ev->value);
					}
				} // tariff button
			} // button channel
		} // loop over read events
	} // loop forever
} // main

