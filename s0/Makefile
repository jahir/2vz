CFLAGS+=-O2 -g -Wall -std=gnu99

all: ev2vzs

ev2vzs: ev2vzs.c
	date +'#define SOURCE_TS "%F %T"' -d @$$(stat -L -c %Y $<) > ev2vzs_ts.h
	date +'#define COMPILE_TS "%F %T"' >> ev2vzs_ts.h
	gcc -o $@ $(CFLAGS) -Werror $<

.PHONY: clean install

clean: 
	rm -f -- ev2vzs

install: /usr/local/bin/ev2vzs

/usr/local/bin/ev2vzs: ev2vzs
	install -D -p ev2vzs /usr/local/bin/

