CFLAGS=-O0 -g -Wall -Werror -std=gnu99

all: thz thz-errors thz2vzs

thz2vzs: thz2vzs.c
	date +'#define SOURCE_TS "%F %T"' -d @$$(stat -L -c %Y $<) > thz2vzs_ts.h
	date +'#define COMPILE_TS "%F %T"' >> thz2vzs_ts.h
	gcc -o $@ $(CFLAGS) $<

%: %.c
	gcc -o $@ $(CFLAGS) $<

.PHONY: clean install

clean:
	rm thz thz2vz thz-errors

install: /usr/local/bin/thz2vzs

/usr/local/bin/thz2vzs: thz2vzs
	install -D -p thz2vzs /usr/local/bin/

