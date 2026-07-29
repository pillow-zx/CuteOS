#include <stddef.h>
#include <signal.h>

int main(void)
{
	struct sigaction action;

	if (sigaction(SIGUSR1, NULL, &action) < 0)
		return 2;
	return action.sa_handler == SIG_DFL ? 0 : 1;
}
