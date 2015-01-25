#include "p2pbase.h"
#include <signal.h>
#include <unistd.h>
#include "p2psvc.h"


void sig_handler(int sig)
{
	if(sig == SIGINT || sig == SIGSTOP || sig == SIGTERM || sig == SIGUSR1)
	{
		dbg_msg("signal SIGINT\n");
		signal_stop_service();
	}
}

int main(int argc, char *argv[])
{
	int i;
	for(i=1; i<argc; i++)
	{
		if(strcmp("-h", argv[i]) == 0 || strcmp("-?", argv[i]) == 0)
		{
			printf("%s [relay-server1 [relay-server2 [...]]]\n", argv[0]);
			exit(0);
		}
	}

	printf("%s, build at %s, %s\n", argv[0], __TIME__, __DATE__);

#ifndef _DEBUG
	if(fork()) return 0;
	setsid();
	chdir("/");
	umask(0);
	if(fork()) return 0;
#endif

	//signal(SIGPIPE, SIG_IGN);
	signal(SIGPIPE, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGUSR1, sig_handler);

	PA_NetLibInit();
	if(init_p2p_svc()) return -1;

	run_p2p_svc(FALSE);

	clean_p2p_svc();

	PA_NetLibUninit();

	return 0;
}

