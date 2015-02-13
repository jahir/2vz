CFLAGS = -O2 -g -Wall -Werror -std=gnu99

OBJ = log.o thz-serial.o

all: thz thz-errors thz2vzs

thz: thz.o $(OBJ)
	#$(CC) $(CFLAGS) -o $@ $<
	$(CC) $(CFLAGS) -o $@ $^

thz-errors: thz-errors.c thz-serial.o log.o
	gcc -o $@ $(CFLAGS) $<

thz2vzs: thz2vzs.c
	date +'#define SOURCE_TS "%F %T"' -d @$$(stat -L -c %Y $<) > thz2vzs_ts.h
	date +'#define COMPILE_TS "%F %T"' >> thz2vzs_ts.h
	gcc -o $@ $(CFLAGS) $<

prog: $(OBJ)
	$(CC) $(CFLAGS) -o prog $(OBJ) $(LDFLAGS)


%.o: %.c
	$(CC) $(CFLAGS) -c $<
#	gcc -o $@ $(CFLAGS) $<

.PHONY: clean install

clean:
	rm thz thz2vz thz-errors

install: /usr/local/bin/thz2vzs

/usr/local/bin/thz2vzs: thz2vzs
	install -D -p thz2vzs /usr/local/bin/

