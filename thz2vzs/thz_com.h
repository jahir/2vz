#include <sys/types.h>

#define REQ_HIST 0x09
#define REQ_GLOBAL   0xfb
#define REQ_TIMEDATE 0xfc
#define REQ_FIRMWARE 0xfd

typedef unsigned char BUF;

/*********************************************************************************/
void reopen_com(const char * port);

void dump(char * pre, BUF * buf, ssize_t len);

int rx(BUF * buf, size_t bufsize);

int rxx(BUF * buf, size_t bufsize, int want);

int ack();

int ping();

BUF checksum(BUF * buf, size_t len);

int req(BUF, BUF *, size_t);
int thz_set(BUF, BUF *, size_t);

double fp(BUF * buf, int decimals);

