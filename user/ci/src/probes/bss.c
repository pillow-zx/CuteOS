#include <stddef.h>
#include <stdio.h>

static unsigned char probe_bss[8192];

int main(int argc, char **argv)
{
	FILE *output;
	size_t index;

	if (argc != 2)
		return 2;
	for (index = 0; index < sizeof(probe_bss); index++) {
		if (probe_bss[index] != 0)
			return 3;
	}
	output = fopen(argv[1], "w");
	if (!output)
		return 4;
	if (fputs("zero\n", output) == EOF || fclose(output) < 0)
		return 5;
	return 0;
}
