CFLAGS = -O2 -g -Wall -Werror -std=gnu99

OBJ = log.o thz_com.o

all: thz thz_errors thz_time thz2vzs thz_display

thz: thz.o $(OBJ)
	#$(CC) $(CFLAGS) -o $@ $<
	$(CC) $(CFLAGS) -o $@ $^

thz_errors: thz_errors.c $(OBJ)
	gcc $(CFLAGS) -o $@ $^

thz_time: thz_time.c $(OBJ)
	gcc $(CFLAGS) -o $@ $^

thz2vzs: thz2vzs.c $(OBJ)
	date +'#define SOURCE_TS "%F %T"' -d @$$(stat -L -c %Y $<) > thz2vzs_ts.h
	date +'#define COMPILE_TS "%F %T"' >> thz2vzs_ts.h
	gcc $(CFLAGS) -o $@ $^

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<
#	gcc -o $@ $(CFLAGS) $<

.PHONY: clean install

clean:
	rm -f -- thz thz-errors set_time thz2vzs *.o

install: /usr/local/bin/thz2vzs

/usr/local/bin/thz2vzs: thz2vzs
	install -D -p thz2vzs /usr/local/bin/

