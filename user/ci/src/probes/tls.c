#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

static __thread int probe_tls;

static void *probe_thread(void *argument)
{
	(void)argument;
	if (probe_tls != 0)
		return (void *)(intptr_t)-1;
	probe_tls = 27;
	return (void *)(intptr_t)probe_tls;
}

int main(int argc, char **argv)
{
	pthread_t thread;
	void *thread_result;
	FILE *output;

	if (argc != 2)
		return 2;
	if (probe_tls != 0)
		return 3;
	probe_tls = 11;
	if (pthread_create(&thread, NULL, probe_thread, NULL) != 0)
		return 4;
	if (pthread_join(thread, &thread_result) != 0)
		return 5;
	if ((intptr_t)thread_result != 27 || probe_tls != 11)
		return 6;
	output = fopen(argv[1], "w");
	if (!output)
		return 7;
	if (fputs("isolated\n", output) == EOF || fclose(output) < 0)
		return 8;
	return 0;
}
