#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "d0.h"
#include "d0vz.h"
#include "d0vz_ts.h"

// config is global
static struct config_t conf;


void mylog(char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
void mylog(char *fmt, ...)
{
	va_list ap;
	char logbuf[256];

	va_start(ap, fmt);
	vsnprintf(logbuf, sizeof(logbuf), fmt, ap);
	va_end(ap);

	if (conf.log) {
		struct timeval tv;
		char timebuf[32];
		gettimeofday(&tv, NULL);
		strftime(timebuf, sizeof(timebuf), "%F %T", localtime(&tv.tv_sec)); // 2012-10-01 18:13:45.678
		FILE* fh = fopen(conf.log, "a");
		if (fh) {
			fprintf(fh, "%s.%03u %s[%d] %s\n", timebuf, (unsigned)(tv.tv_usec/1000), PROGNAME, getpid(), logbuf);
			fclose(fh);
		} else {
			fprintf(stderr, "open log file %s failed: %s\n", conf.log, strerror(errno));
			exit(EXIT_FAILURE);
		}
	} else { // fallback to stderr output
		fprintf(stderr, "%s\n", logbuf);
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


/*** config stuff ****************************************************************/

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
struct config_t * read_config(const char * conffile, struct config_t * conf) {
	memset(conf, 0, sizeof(*conf));
	FILE * fh = fopen(conffile, "r");
	if (!fh) {
		mylog("open config '%s' failed: %s", conffile, strerror(errno));
		return NULL;
	}
	char line[1024];
	char * c;
	int lines = 0, ports = 0, devices = 0, channels = 0;
	struct port_t ** po0 = &conf->port;
	struct device_t ** dev0 = &conf->device;
	struct channel_t ** ch0 = NULL;
	while (fgets(line, sizeof(line), fh)) {
		// TODO: more error checking and output
		++lines;
		if (line[0] == '#')
			continue; // skip comments and empty lines
		if ((c = strtok(line, CONF_SEP)) == NULL) 
			continue; // empty line, skip
		if (!strcmp(c, "log")) {
			if (CONFIG_ELEM(c, conf->log)) {
				DPRINT("line %d: log to '%s'", lines, conf->log);
			}
		} else if (!strcmp(c, "spool")) {
			if (CONFIG_ELEM(c, conf->spool)) {
				DPRINT("line %d: spool to '%s'", lines, conf->spool);
			}
		// port /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_xxxxxxxx-if00-port0
		} else if (!strcmp(c, "port")) {
			struct port_t * po = myalloc(sizeof(struct port_t));
			if (CONFIG_ELEM(c, po->path)) {
				DPRINT("line %d: port %d is '%s'", lines, ports, c);
				*po0 = po;
				po0 = &po->next;
				++ports;
			} else {
				free(po->path);
				free(po);
			}
		// device 0-0:C.1.0*255 12345
		} else if (!strcmp(c, "device")) {
			struct device_t * dev = myalloc(sizeof(struct device_t));
			if (CONFIG_ELEM(c, dev->oid) && CONFIG_ELEM(c, dev->serial)) {
				// TODO? dupecheck
				DPRINT("line %d: device %d oid %s serial %s", lines, devices, dev->oid, dev->serial);
				*dev0 = dev;
				dev0 = &dev->next;
				ch0 = &dev->channel;
				++devices;
			} else {
				mylog("line %d: add device failed", lines);
				// free(...)
			}
		// channel 1-0:1.8.0*255 aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
		} else if (!strcmp(c, "channel")) {
			if (!ch0) {
				mylog("line %d: channel without device", lines);
				continue;
			}
			struct channel_t * ch = myalloc(sizeof(struct channel_t));
			if (CONFIG_ELEM(c, ch->oid) && CONFIG_ELEM(c, ch->uuid)) {
				DPRINT("line %d: channel %d oid %s uuid %s", lines, channels, ch->oid, ch->uuid);
				*ch0 = ch;
				ch0 = &ch->next;
				++channels;
			} else {
				mylog("line %d: add channel failed", lines);
				// free(...)
			}
		} else {
			mylog("line %d invalid: '%.99s'", lines, line);
		}
	}
	mylog("config has %d ports and %d devices with %d channels", ports, devices, channels);
	fclose(fh);
	return conf;
}

/*** d0 handling ***/

void vzspool(unsigned long long tsms, char * uuid, char * val) {
	char spoolfile[256];
	snprintf(spoolfile, sizeof(spoolfile), VZ_SPOOLFMT, conf.spool, tsms, uuid, val);
	int fd = open(spoolfile, O_CREAT|O_EXCL|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
	if (fd < 0) {
		mylog("ERROR: open %s: %s", spoolfile, strerror(errno));
	} else {
		close(fd);
	}
}

int d0read(D0 * d0) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	unsigned long long tsms = (unsigned long long) tv.tv_sec * 1000 + tv.tv_usec / 1000;
	if (!d0_read(d0)) {
		mylog("d0_read: %s", d0->errstr);
		return 0;
	}
	DPRINT("d0_read got %d values", d0->vals);
#ifdef DEBUG
	d0_dump(d0);
#endif
	
	// find config channels for received values
	for (struct device_t * dev=conf.device; dev; dev=dev->next) {
		if (strcmp(dev->serial, d0->serial))
			continue;
		// found device
		// TODO: micro optimization: put this device at the start of the device list
		for (struct channel_t * ch=dev->channel; ch; ch=ch->next) {
			for (int i=0; i<d0->vals; ++i) {
				struct d0val val = d0->val[i];
				if (strcmp(ch->oid, val.id))
					continue;
				// found channel
				char * p = val.val;
				// strip leading zeroes ...
				for (p=val.val; *p == '0' ; ++p) { }
				// ... but leave one if there's nothing else
				if (p > val.val && (!*p || *p == '.'))
					--p;
				DPRINT("serial %s obis-id %s -> uuid %s val %s", dev->serial, ch->oid, ch->uuid, p);
				if (strlen(p) < sizeof(ch->value)) {
					if (*(ch->value) == 0) { // initial read
						mylog("serial %s obis-id %s : initial value %s", dev->serial, ch->oid, p);
					} else if (strcmp(ch->value, p)) {
						mylog("serial %s obis-id %s : value change %s to %s", dev->serial, ch->oid, ch->value, p);
						vzspool(tsms, ch->uuid, p);
					}
					strcpy(ch->value, p);
				} else { // unlikely, but you never know...
					mylog("ERROR: ignoring value, string too long (serial %s obis-id %s value %s)", dev->serial, ch->oid, p);
					*(ch->value) = 0; // clear value
				}
			}
		}
	}
	return 1;
}

void d0loop(char * port) {
	while (1) {
		D0 * d0 = d0_open(port);
		if (!d0) {
			mylog("d0_open failed (%s), retry in 10 seconds", strerror(errno));
			sleep(10);
			continue;
		}
		mylog("opened port device %s", port);
		do {
			sleep(2);
		} while (d0read(d0));
		mylog("d0read error, reopen port %s in 5 seconds", port);
		d0_close(d0);
		sleep(5);
	}
}

void fork_reader(struct port_t * port) {
		pid_t pid = fork();
		if (pid < 0) {
			mylog("fork failed: %s", strerror(errno));
			kill(0, SIGTERM); // kill whole process group
			exit(EXIT_FAILURE);
		} else if (pid == 0) { // child process
			d0loop(port->path);
			exit(EXIT_FAILURE);
		}
		port->pid = pid;
}

void wait4sigchld() {
	int status;
	pid_t pid = wait(&status);
	if (pid < 0) {
		mylog("waitpid error: %s", strerror(errno));
		return;
	}
	for (struct port_t * port=conf.port; port; port=port->next) { // search port of pid
		if (port->pid == pid) {
			mylog("child pid %d (port %s) exited with status %d", pid, port->path, status);
			fork_reader(port);
			return;
		}
	}
	mylog("unknown pid %d exited with status %d", pid, status);
}


/*** main ***/
int main(int argc, char * argv[]) {
	if (argc<2) {
		mylog("usage: %s <config>", argv[0]);
		exit(EXIT_FAILURE);
	}

	const char * conffile = argv[1];
	if (!read_config(conffile, &conf)) {
		exit(EXIT_FAILURE);
	}

	mylog("startup. source ts %s, binary ts %s, spool dir %s", SOURCE_TS, COMPILE_TS, conf.spool);

	if (!conf.port) {
		mylog("no ports to listen to, exiting...");
		exit(EXIT_FAILURE);
	}

	{ // install signal handlers
		struct sigaction action;
		memset(&action, 0, sizeof(struct sigaction));
		action.sa_handler = handle_sig;
		sigaction(SIGTERM, &action, NULL);
		sigaction(SIGQUIT, &action, NULL);
		sigaction(SIGINT, &action, NULL);
		sigaction(SIGHUP, &action, NULL);
	}

	for (struct port_t * port=conf.port; port; port=port->next) {
		fork_reader(port);
	}

	/* wait for a child process to end */
	while (1) {
		wait4sigchld();
	}
}

