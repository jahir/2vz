#include <stdio.h>
#include <stdlib.h>
#include "d0.h"

int main(int argc, char * argv[]) {
	if (argc<2) {
		fprintf(stderr, "usage: %s <device>\n", argv[0]);
		exit(1);
	}
	char * dev = argv[1];
	printf("open device %s\n", dev);
	D0 * d0 = d0_open(dev, 300);
	if (d0 == NULL) {
		perror("d0_init");
		exit(1);
	}
	if (d0_read(d0)) {
		printf("d0_read got %d values\n", d0->vals);
		d0_dump(d0);
	} else {
		printf("d0_read error\n");
	}
	d0_close(d0);
}

