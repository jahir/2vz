#include <sys/types.h>

#define NUL 0x00
#define SOH 0x01
#define STX 0x02
#define ETX 0x03
#define ACK 0x06
#define DLE 0x10

#define REQ_HIST 0x09
#define REQ_GLOBAL   0xfb
#define REQ_TIMEDATE 0xfc
#define REQ_FIRMWARE 0xfd

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

/*********************************************************************************/
void reopen_com(const char * port);

void dump(char * pre, BUF * buf, ssize_t len);

int rx(BUF * buf, size_t bufsize);

int rxx(BUF * buf, size_t bufsize, int want);

int ack();

int ping();

BUF checksum(BUF * buf, size_t len);

int req(int cmd, BUF * outbuf, size_t bufsize);

double fp(BUF * buf, int decimals);

