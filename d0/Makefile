CFLAGS += -std=gnu99 -fpic -g -Wall -Werror

all: libd0.so test d0vz


d0test: d0test.c
	$(CC) $(CFLAGS) -L. -ld0 -o $@ $<

test: test.c
	$(CC) $(CFLAGS) -o $@ $<

d0vz: d0vz.c d0vz.h
	date +'#define SOURCE_TS "%F %T"' -d @$$(stat -L -c %Y $<) > d0vz_ts.h
	date +'#define COMPILE_TS "%F %T"' >> d0vz_ts.h
	$(CC) $(CFLAGS) -L. -ld0 -lconfig -o $@ $<

d0.o: d0.c d0.h
	$(CC) $(CFLAGS) -c $<

libd0.so: d0.o
	$(CC) -shared -o $@ $<


.PHONY: clean install
clean:
	rm -f *.o *.so test

install: libd0.so d0vz
	install -D -p libd0.so /usr/local/lib/
	install -D -p d0vz /usr/local/bin/

