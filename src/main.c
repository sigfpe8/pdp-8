#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pdp8.h"
#include "console.h"

void usage(char *name)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "%s [-m <kwords>]\n", name);
}

int main(int argc, char *argv[])
{
	size_t kwords = 4;
	char *pc;
	int i;

	for (i = 1; i < argc; ++i) {
		pc = argv[i];
		if (*pc == '-') {
			if (!strncmp(pc,"-m",2)) {
				if (strlen(pc) > 2) kwords = atoi(pc+2);
				else if ((i+1) < argc) { ++i; kwords = atoi(argv[i]); }
				else goto badop;
				if (kwords < 4 || kwords > 32) {
					fprintf(stderr, "Invalid memory size: %ld K words\n", kwords);
					fprintf(stderr, "Must be between 4 and 32 K words\n");
					return 1;
				}
				if (kwords & 3) {
					fprintf(stderr, "Invalid memory size: %ld K words \n", kwords);
					fprintf(stderr, "Must be a multiple of 4 (K words)\n");
					return 1;
				}
			} else if (!strncmp(pc,"-h",2)) {	/* Help */
				usage(argv[0]);
				return 1;
			} else goto badop;
		} else {
badop:		fprintf(stderr,"Invalid option: %s\n", pc);
			usage(argv[0]);
			return 1;
		}
	}
	
	printf("\nPDP-8 simulator version %d.%d\n",MAJVER,MINVER);
	printf("%ldK memory\n", kwords);

	cpu_init(kwords);

	console();

	cpu_deinit();

	return 0;
}

