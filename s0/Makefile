CFLAGS+=-O2 -g -Wall -Werror -std=gnu99

all: ev2vzs

ev2vzs: ev2vzs.c
	date +'#define SOURCE_TS "%F %T"' -d @$$(stat -L -c %Y $<) > ev2vzs_ts.h
	date +'#define COMPILE_TS "%F %T"' >> ev2vzs_ts.h
	$(CC) -o $@ $(CFLAGS) $<

debug: ev2vzs.c
	CFLAGS+=-DDEBUG
	date +'#define SOURCE_TS "%F %T"' -d @$$(stat -L -c %Y $<) > ev2vzs_ts.h
	date +'#define COMPILE_TS "%F %T"' >> ev2vzs_ts.h
	$(CC) -o $(basename $<) -Og -g -Wall -Werror -std=gnu99 -DDEBUG $<

.PHONY: clean install setdebug debug

clean: 
	rm -f -- ev2vzs

install: /usr/local/bin/ev2vzs

/usr/local/bin/ev2vzs: ev2vzs
	install -D -p ev2vzs /usr/local/bin/

