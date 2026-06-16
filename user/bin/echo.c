#include <ulib.h>

int main(int argc, char **argv)
{
	int newline = 1;
	int i = 1;
	int first = 1;

	while (i < argc && streq(argv[i], "-n")) {
		newline = 0;
		i++;
	}

	for (; i < argc; i++) {
		if (!first)
			printf(" ");
		printf("%s", argv[i]);
		first = 0;
	}
	if (newline)
		printf("\n");
	return 0;
}
