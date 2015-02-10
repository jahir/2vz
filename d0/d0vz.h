#ifndef D0VZ_H
#define D0VZ_H

#define PROGNAME "d0vz"

#define VZ_SPOOLFMT "%s%llu_%s_%s"

//#define DEBUG
#ifdef DEBUG
#define DPRINT(format, args...) printf("%s: "format"\n", __FUNCTION__, ##args)
#else
#define DPRINT(format, args...) do { /* nothing */ } while (0)
#endif

struct port_t {
	char *path;
	pid_t pid; // stores the pid of the child process
	struct port_t * next;
};

struct channel_t {
	char * oid; // 1-0:1.8.0*255
	char * uuid; // aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
	char value[32]; // last value read (set during runtime, not by config)
	struct channel_t * next;
};
struct device_t {
	char * oid; // e.g. 0-0:C.1.0*255
	char * serial; // 12345 from 0-0:C.1.0*255(12345)
	// channel 1-0:1.8.0*255 aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
	struct channel_t * channel;
	struct device_t * next;
};

struct config_t {
	char * log;
	char * spool;
	// port /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_xxxxxxxx-if00-port0
	struct port_t * port;
	// device 0-0:C.1.0*255 12345
	struct device_t * device;
};

#endif

