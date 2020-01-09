#ifndef D0_H
#define D0_H

#include <poll.h>

#define BUFSIZE 64

char * PULLSEQ = "\006000";
#define STX '\002'
#define ETX '\003'
#define ACK '\006'


struct d0val {
	char id[16];
	char val[16];
	char unit[16];
};

struct d0dev {
	struct pollfd _fds;
	char id[32]; // /ISk5MT171-0222
	char serial[16]; // 0-0:C.1.0*255(12345678)
	char propid[16]; // 1-0:0.0.0*255(987654321)
	int vals;
 	struct d0val val[8];
	char errstr[BUFSIZE*2];
};

typedef struct d0dev D0;

#define DIM(vec) (sizeof(vec)/sizeof(vec[0]))


D0* d0_open(const char* dev);
void d0_close(D0* d0);
int d0_read(D0* d0);
void d0_dump(D0* d0);

#endif

