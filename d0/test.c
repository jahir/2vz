#include <stdlib.h>
#include <stdio.h>

int main(int argc, char * argv[]) {
	double f = atof(argv[1]);
//	printf("e: %e\n", f);
//	printf("f: %f\n", f);
	printf("g: %g\n", f);
	printf("%%.2g: '%.2g'\n", f);
}
