#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	const char *token;
	FILE *output;

	if (argc != 4)
		return 2;
	token = getenv("UTEST_EXEC_TOKEN");
	if (!token)
		return 3;
	output = fopen(argv[1], "w");
	if (!output)
		return 4;
	if (fprintf(output, "%s|%s|%s\n", argv[2], argv[3], token) < 0 ||
	    fclose(output) < 0)
		return 5;
	return 0;
}
