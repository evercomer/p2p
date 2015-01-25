// p2pclt.exe.cpp : 定义控制台应用程序的入口点。
//

#include "app/p2psess.h"
#include "app/rtsp/rtspsvc.h"
#include "DAp2pcmd.h"
#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include "cltcmdwrapper.h"

//#define CURSE_CONSOLE

#ifdef CURSE_CONSOLE
#include <curses.h>
#endif

char *rstrip(char *s)
{
	char *e = s + strlen(s);
	while(e[-1] && isspace(e[-1])) e--;
	*e = '\0';
	return s;
}

struct P2pClient {
	char sn[32];
	P2pSession *pSess;

	P2PCONNINFO cinfo;

	int state;	//0: connecting; 1-connected; 2-failed
	int err;	//error code if state==2
	int frame_cnt[2];

	//for connecting progress
	PA_HTHREAD handle;
	int _break_conn;
	PA_EVENT evt_conn;
};

static P2pClient clients[32];
static int n_clients = 0;

#define SLVF_VALUE	SLVF_DONT_DECODE_VIDEO
//#define SLVF_VALUE	0
PA_THREAD_RETTYPE __STDCALL ThreadConn(void *p)
{
	int err, id = (int)p;
	P2pClient *pClt = &clients[id];


	pClt->frame_cnt[0] = pClt->frame_cnt[1] = 0;
	while(!pClt->_break_conn)
	{
		pClt->state = 0;
		if( (err = pClt->pSess->StartSession("svrshp.thingsnic.com", pClt->sn, "admin", "admin")) == 0 )
		{
			DAP2pConnGetInfo(pClt->pSess->GetConnectionHandle(), &pClt->cinfo);
			pClt->state = 1;
			if( (err = pClt->pSess->StartLiveVideo(NULL, &pClt->frame_cnt[0], QUALITY_AUTO, /*vstrm*/0, 0, 
							SLVF_VALUE
							)) )
				;
			break;
		}
		else
		{
			pClt->state = 2;
			pClt->err = err;
			PA_EventWaitTimed(pClt->evt_conn, 5000);
		}
	}

	return 0;
}

static int run_me = 1;
void start_conn(int i)
{
	clients[i].state = 0;
	clients[i].frame_cnt[0] = clients[i].frame_cnt[1] = 0;
#ifdef CURSE_CONSOLE
	mvaddstr(i,  36, "......                                ");
#endif
	clients[i]._break_conn = 0;
	clients[i].handle = PA_ThreadCreate(ThreadConn, (void*)i);
}

void stop_conn(P2pClient *pclt)
{
	if(pclt->handle)
	{
		pclt->_break_conn = 1;
		PA_EventSet(pclt->evt_conn);
		PA_ThreadWaitUntilTerminate(pclt->handle);
		PA_ThreadCloseHandle(pclt->handle);
		pclt->handle = NULL;
	}
}

void supportedCmds()
{
#ifdef CURSE_CONSOLE
	mvaddstr(22, 0, "[quit; Va:stop videos; va:start videos; C|c[n]:disconnect/connect; V|v[n]:stop/start video; A|a:stop/start audio]");
#else
	printf("[quit; Va:stop videos; va:start videos; C|c[n]:disconnect/connect; V|v[n]:stop/start video; A|a:stop/start audio]\n");
#endif
}

void processCmd(const char *scmd)
{
	int i;
	if(strcmp("quit", scmd) == 0)
	{
		for(i=0; i<n_clients; i++)
		{
			stop_conn(&clients[i]);
			clients[i].pSess->StopSession();
		}
		run_me = 0;
		return;
	}
	if(strcmp("Va", scmd) == 0)
	{
		for(i=0; i<n_clients; i++)
			clients[i].pSess->StopLiveVideo();
		return;
	}
	if(strcmp("va", scmd) == 0)
	{
		for(i=0; i<n_clients; i++)
			clients[i].pSess->StartLiveVideo(NULL, &clients[i].frame_cnt[0], QUALITY_AUTO, 0, 0, 
					SLVF_VALUE
					);
		return;
	}

	int id = atoi(scmd+1);
	if(id >= n_clients) return;
	switch(scmd[0])
	{
	case 'c': //connect
		start_conn(id);
		break;
	case 'C': //disconnect
		clients[id].pSess->StopSession();
		break;
	case 'v': //start video
		clients[id].pSess->StartLiveVideo(NULL, &clients[id].frame_cnt[0], QUALITY_AUTO, 0, 0, 0);
		break;
	case 'V': //stop video
		clients[id].pSess->StopLiveVideo();
		break;
	case 'a': //start audio
		clients[id].pSess->Vocalize();
		break;
	case 'A': //stop audio
		clients[id].pSess->Mute();
		break;
	}
}

void sig_handler(int sig)
{
	const char *sct[] = { "local", "p2p", "relay" };
	char ss[64];

	if(sig == SIGALRM)
	{
		int i;
#ifdef CURSE_CONSOLE
		int x, y, cursv;

		getsyx(y, x);
		for(i=0; i<n_clients; i++)
		{
			switch(clients[i].state)
			{
				case 0:
					mvaddstr(i,  36, "......");
					break;
				case 1:
					sprintf(ss, "ct:%s", sct[clients[i].cinfo.ct-1]);
					mvaddstr(i, 36, ss);
					clients[i].state = 10;
					break;
				case 2:
					sprintf(ss, "E%d", clients[i].err);
					mvaddstr(i, 36, ss);
					clients[i].state = -1;
					break;
				case 10:
					mvprintw(i, 50, "v:%d, a:%d", clients[i].frame_cnt[0], clients[i].frame_cnt[1]);
					break;
			}
#if 1
			if(clients[i].state == 10 && clients[i].pSess->GetConnectionHandle() &&
					DAP2pConnGetState(clients[i].pSess->GetConnectionHandle()) != 0)
			{
				clients[i].pSess->StopSession();
				start_conn(i);
			}
#endif
		}
		//setsyx(y, x);
		move(y, x);
		refresh();
#else
		i = 0;
		printf("\r%s       ", clients[i].sn);
		switch(clients[i].state)
		{
			case 0:
				printf(".......\n");
				break;
			case 1:
				printf("ct:%s", sct[clients[i].cinfo.ct-1]);
				clients[i].state = 10;
				break;
			case 2:
				sprintf(ss, "E%d\n", clients[i].err);
				printf(ss);
				clients[i].state = -1;
				break;
			case 10:
				printf("ct:%s   v:%d, a:-", sct[clients[i].cinfo.ct-1], clients[i].frame_cnt[0], clients[0].frame_cnt[1]);
				break;
		}
		fflush(stdout);
#if 1
		if(clients[i].state == 10 && DAP2pConnGetState(clients[i].pSess->GetConnectionHandle()) != 0)
		{
			printf("session terminated: %d\n", DAP2pConnGetState(clients[i].pSess->GetConnectionHandle()));
			clients[i].pSess->StopSession();
			clients[i].state = 0;
#ifdef CURSE_CONSOLE
			mvaddstr(i,  36, "......                                ");
#endif
			clients[i]._break_conn = 0;
			clients[i].handle = PA_ThreadCreate(ThreadConn, (void*)i);
		}
#endif
#endif
	}
}

int main(int argc, char* argv[])
{
	FILE *fp;
	char ss[64];

	if(!(fp = fopen("ids.txt", "r"))) { printf("Cann't open id file: ids.txt\n"); exit(1); };
#ifdef CURSE_CONSOLE
	WINDOW *mainwin;
	if( !(mainwin = initscr()) )
	{
		fprintf(stderr, "cann't init screen.\n");
		exit(101);
	}
	noecho();
	keypad(mainwin, TRUE);
	//start_color();
#endif

	memset(clients, 0, sizeof(clients));
	DAP2pClientInit("202.181.198.100", NULL, NULL, NULL);
	LaunchRtspService();


	n_clients = 0;
	while(fgets(ss, sizeof(ss), fp))
	{
		if(isalnum(ss[0]))
		{
			clients[n_clients].pSess = new P2pSession();
			strcpy(clients[n_clients].sn, rstrip(ss));
			clients[n_clients]._break_conn = 0;
			PA_EventInit(clients[n_clients].evt_conn);

			clients[n_clients].handle = PA_ThreadCreate(ThreadConn, (void*)n_clients);
#ifdef CURSE_CONSOLE
			sprintf(ss, "%2d. %-30s  ......", n_clients, clients[n_clients].sn);
			mvaddstr(n_clients, 0, ss);
#endif
			n_clients++;
		}
	}
	fclose(fp);
#ifdef CURSE_CONSOLE
	supportedCmds();
	mvaddstr(24, 0, "Command:");
	refresh();
#endif

	struct sigaction sa;
	sa.sa_handler = sig_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);

	struct itimerval it;
	timerclear(&it.it_interval);
	timerclear(&it.it_value);
	it.it_interval.tv_usec = it.it_value.tv_usec = 500000;
	setitimer(ITIMER_REAL, &it, NULL);


	char scmd[64];
#ifdef CURSE_CONSOLE
	int cnt = 0;
	while(run_me)
	{
		int ch = getch();
		if(ch != ERR)
		{
			if(isalnum(ch))
			{
				scmd[cnt++] = ch;
				move(24, 8+cnt);
				echochar(ch);
			}
			else if((ch == 0x0A || ch == 0x0D) && cnt > 0)
			{
				scmd[cnt] = '\0';
				processCmd(scmd);

				cnt = 0;
				move(24, 0);
				deleteln();
				mvaddstr(24, 0, "Command:");
			}
		}
	}

	delwin(mainwin);
	endwin();
	refresh();
#else
	while(run_me)
	{
		if(fgets(scmd, sizeof(scmd), stdin))
		{
			rstrip(scmd);
			processCmd(scmd);
		}
	}
#endif

	int i;
	for(i=0; i<n_clients; i++)
	{
		PA_EventUninit(clients[i].evt_conn);
		delete clients[i].pSess;
	}

	return 0;
}

