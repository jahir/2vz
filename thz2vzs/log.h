
#define EPRINT(format, args...) mylog("%s: "format, __FUNCTION__, ##args)

void mylog(char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
void mylog_logpath(const char *);
void mylog_progname(const char *);

