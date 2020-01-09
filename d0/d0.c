
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include "d0.h"

// read timeout in milliseconds
#define D0_READ_TIMEOUT 2000

/* helper ***********************************************************/

static void txline(struct pollfd * fds, char * line)
{
	char buf[BUFSIZE];

	int len = snprintf(buf, sizeof(buf), "%s\r\n", line);
	if (len < 0)
		return;
	write(fds->fd, buf, len);
}

static int rxline(struct pollfd * fds, char * buf)
{
	int len;

	fds->events = POLLIN;
	memset(buf, 0, BUFSIZE);
	for (len=0; len<=BUFSIZE-2;) {
		int rc = poll(fds, 1, D0_READ_TIMEOUT);
		if (rc < 0) { // error
			snprintf(buf, BUFSIZE, "poll error %d: %s", errno, strerror(errno));
			return -1;
		}
		if (rc == 0) { // timeout
			snprintf(buf, BUFSIZE, "poll timeout");
			return -1;
		}
		ssize_t got = read(fds->fd, buf+len, 1);
		if (got < 0) { // read error
			snprintf(buf, BUFSIZE, "read error %d: %s", errno, strerror(errno));
			return -1;
		}
		if (rc == 0) { // device error?
			snprintf(buf, BUFSIZE, "read nothing");
			return -1;
		}
		len += got;
		if (buf[len-1] == '\n' || (len > 1 && buf[0] == ETX))
			break;
	}
	return len;
}

static int readchar(struct pollfd * fds) {
	fds->events = POLLIN;
	int rc = poll(fds, 1, D0_READ_TIMEOUT);
	if (rc < 0) {
		return -1;
	} else if (rc == 0) { // timeout
		return -1;
	}
	unsigned char c;
	rc = read(fds->fd, &c, 1);
	if (rc != 1)
		return -1;
	return c;
}

// parse obis line into val (which should be cleared before)
int d0_parse_obis(char * buf, struct d0val * val) {
	// 0-0:C.1.0*255(47387161)
	// 1-0:1.8.0*255(000000.4 kWh)
	char * p1 = strchr(buf, '(');
	if (p1 == NULL || p1-buf >= sizeof(val->id))
		return 0;
	*p1 = 0;
	strncpy(val->id, buf, p1-buf);
	++p1;

	char * p2 = strpbrk(p1, " *)");
	if (p2 == NULL || p2-p1 >= sizeof(val->val))
		return 0;
	char c = *p2;
	*p2 = 0;
	strncpy(val->val, p1, p2-p1);
	if (c == ' ') {
		++p2;
		p1 = strchr(p2, ')');
		if (p1 == NULL || p1-p2 >= sizeof(val->unit))
			return 0;
		*p1 = 0;
		strncpy(val->unit, p2, p1-p2);
	}
	
	return 1;
}


/* exported lib functions *******************************************/

D0 * d0_open(const char* dev) {
	// open and configure serial port
//	int fd = open(dev, O_RDWR|O_NONBLOCK|O_NOCTTY); // |O_CLOEXEC);
	int fd = open(dev, O_RDWR|O_NOCTTY); // |O_CLOEXEC);
	if (fd < 0)
		return NULL;
	tcflush(fd, TCIFLUSH);
	struct termios tio;
	memset(&tio, 0, sizeof(tio));
	cfmakeraw(&tio);
	tio.c_cflag = CS7|PARENB;
	cfsetspeed(&tio, B300);
	//tio.c_cc[VMIN] = 32; // we'd need to split input for that
	//tio.c_cc[VTIME] = 1;
	tcsetattr(fd, TCSANOW, &tio);

	D0 * d0 = calloc(1, sizeof(D0));
	if (d0 == NULL) {
		close(fd);
		return NULL;
	}
	d0->_fds.fd = fd;

	return d0;
}

void d0_close(D0* d0) {
	if (d0 != NULL) {
		close(d0->_fds.fd);
		free(d0);
	}
}

int d0_read(D0 * d0) {
	struct pollfd * fds = & d0->_fds;
	int len;
	char buf[BUFSIZE];

	// clean previously read data	
	memset(d0->id, 0, sizeof(d0->id));
	memset(d0->serial, 0, sizeof(d0->serial));
	memset(d0->propid, 0, sizeof(d0->propid));

	txline(fds, "/?!");
	len = rxline(fds, buf); // /ISk5MT171-0222
	if (len <= 0 || buf[0] != '/') {
		if (len < 0)
			snprintf(d0->errstr, sizeof(d0->errstr), "com init: %s", buf);
		else
			snprintf(d0->errstr, sizeof(d0->errstr), "no or invalid response to init");
		return 0;
	}
	char * end = strpbrk(buf, "\r\n");
	if (end != NULL)
		*end = 0;
	strncat(d0->id, buf, sizeof(d0->id));

	txline(fds, PULLSEQ);
	if (readchar(fds) != STX)
		return 0;
	unsigned char chks = 0, chks_got = 0; // checksum
	for (d0->vals=0; d0->vals<DIM(d0->val);) {
		len = rxline(fds, buf);
		if (len < 0) {
			snprintf(d0->errstr, sizeof(d0->errstr), "com err [%d]: %s", d0->vals-1, buf);
			return 0;
		}
		if (len == 0)
			continue; // emtpy line??
		if (buf[0] == ETX) { // end of transmission
			if (len < 2) // no checksum!?
				return 0;
			chks ^= buf[0];
			chks_got = buf[1];
			break;
		}
		for (int i=0; i<len; ++i)
			chks ^= buf[i];
		struct d0val * val = &(d0->val[d0->vals]);
		memset(val, 0, sizeof(*val));
		if (d0_parse_obis(buf, val)) {
			++d0->vals;
			if (!strcmp(val->id, "0-0:C.1.0*255"))
				strncpy(d0->serial, val->val, sizeof(d0->serial));
			else if (!strcmp(val->id, "1-0:0.0.0*255"))
				strncpy(d0->propid, val->val, sizeof(d0->propid));
		}
	}
	if (chks != chks_got) {
		snprintf(d0->errstr, sizeof(d0->errstr) , "checksum mismatch: calculated %02hhx but got %02hhx", chks, chks_got);
		return 0;
	}

	return 1;
}

void d0_dump(D0 * d0) {
	printf("id %s serial %s propid %s\n", d0->id, d0->serial, d0->propid);
	for (int i=0; i<d0->vals; ++i) {
		struct d0val * val = &d0->val[i];
		printf("obis id '%s' val '%s' unit '%s'\n", val->id, val->val, val->unit);
	}
}


