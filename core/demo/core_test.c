#include "p2pcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef __LINUX__
#include <unistd.h>
#elif defined(WIN32)
#include <io.h>
#endif
#include "pktextrct.h"

char *p2p_server = NULL, *id_bound = NULL, *id_connect = NULL;
HP2PCONN g_hconn = NULL;
struct cmd_header { char tag, cmd, cls, pad; int len; };
struct ft_header {
	struct cmd_header hd;
	char fn[32];
};
struct conn_data {
	HP2PCONN hconn;
	int cmd;
	int total_len;

	PKTEXTRACTOR pe;

	FILE *fp;
};

int echo(HP2PCONN hconn, const char *s);
int flood(HP2PCONN hconn, const char *s);
int flood2(HP2PCONN hconn, const char *s); //useless data
int connectTo(const char *p2pserv, const char *id);
int relayTo(const char *p2pserv, const char *id);
void printConnectionsState();
int getFile(HP2PCONN hconn, const char *remote_file);
int putFile(HP2PCONN hconn, const char *local_file);
char *lstrip(char *s);
char *sstrip(char *s);
int sendCmd(HP2PCONN hconn, int cmd, void *pData, int len);
int sendResp(HP2PCONN hconn, int cmd, void *pData, int len);

void printUsage(const char *prog)
{
	printf("%s -s p2p_server -c id_connect_to\n\tConnecct to id_connect_to\n", prog);
	printf("%s -s p2p_server -u id_bound\n\tBound to id_bound and wait for connection\n", prog);
	printf("%s -s p2p_server -u id_bound -c id_connect_to\nBound to id_bound and connect to id_connect", prog);
	exit(0);
}
void printHelp()
{
	printf("h. this menu\n");
	printf("c. connect to ...\n");
	printf("r. relay to ...\n");
	printf("g. get file\n");
	printf("p. put file\n");
	printf("e. echo\n");
	printf("f. flood\n");
	printf("q. quit\n");
}
void printErr(int err)
{
}
static int is_valid_pkt(unsigned char *pData)
{
	return *pData == '$';
}
static int fetch_pkt_len(unsigned char *pPkt)
{
	return ((struct cmd_header*)pPkt)->len + sizeof(struct cmd_header);
}
static void proc_pkt(unsigned char *pPkt, int pktLen, void *pUser)
{
	//printf("pkt len: %d\n", pktLen); return;
	struct conn_data *_data = (struct conn_data*)pUser;
	struct cmd_header *pch = (struct cmd_header*)pPkt;
	_data->cmd = pch->cmd;
	_data->total_len = pch->len;
	switch(pch->cmd)
	{
		case 'e': 
			if(pch->cls == 0)
			{
				sendResp(_data->hconn, 'e', pPkt + sizeof(struct cmd_header), pktLen - sizeof(struct cmd_header));
				fprintf(stdout, "<<<");
			}
			else
				fprintf(stdout, ">>>");
#define min(x, y) ((x)>(y)?(y):(x))
			fwrite(pPkt + sizeof(struct cmd_header), 1, min(30, pktLen-sizeof(struct cmd_header)), stdout);
			fprintf(stdout, "\n");
			break;

		case 'g':
			if(pch->cls == 0)
			{
				putFile(_data->hconn, (const char*)(pPkt + sizeof(struct cmd_header)));

				_data->cmd = _data->total_len = 0;
			}
			break;

		case 'p':
			if(pch->cls == 0)
			{
				struct ft_header *fth = (struct ft_header*)pPkt;
				_data->fp = fopen(fth->fn, "wb");
				_data->total_len -= (pktLen - sizeof(struct ft_header));
				if(_data->fp)
				{
					fwrite(fth+1, 1, pktLen - sizeof(struct ft_header), _data->fp);
					if(_data->total_len == 0) fclose(_data->fp);
				}
			}
			else
			{
				_data->total_len -= pktLen;
				if(_data->fp)
				{
					fwrite(pPkt, 1, pktLen, _data->fp);
					if(_data->total_len == 0)
					{
						fclose(_data->fp);
						_data->fp = NULL;
					}
				}
			}
			break;
	}
}

BOOL VerifyAuthString(const char *auth_str)
{
	printf("VerifyAuthString: %s\n", auth_str);
	return TRUE;
}

int ConnCreated(HP2PCONN hconn)
{
	char *id = NULL;

	P2pConnGetUserData(hconn, (void**)&id);
	if(id) {
		printf("Connect to %s success, hconn=%p.\n", id, hconn);
		free(id); 
	} else {
		printf("An incoming connection %p is accepted.\n", (void*)hconn);
	}

	struct conn_data *_data = (struct conn_data*)calloc(sizeof(struct conn_data), 1);
	_data->hconn = hconn;
	PktExtractorInit(&_data->pe, sizeof(struct cmd_header), 1600, is_valid_pkt, fetch_pkt_len, proc_pkt, _data);

	P2pConnSetUserData(hconn, (void*)_data);

	g_hconn = hconn;

	return 0;
}

void ConnAborted(HP2PCONN hconn, int err)
{
	struct conn_data *ptr = NULL;
	
	P2pConnGetUserData(hconn, (void**)&ptr);
	if(ptr)
	{
		PktExtractorDeinit(&ptr->pe);
		free(ptr);
	}
	
	printf("Connection %p aborted.\n", (void*)hconn);
	if(g_hconn == hconn) g_hconn = NULL;
}

void ConnFailed(int err, void *pUser)
{
	printf("Connect to %s failed with error %d\n", (char*)pUser, err);
	free(pUser);
	//if(!id_bound) exit(err);
}

void OnData(HP2PCONN hconn, BYTE *pData, int len)
{
	struct conn_data *_data = NULL;

	P2pConnGetUserData(hconn, (void**)&_data);
	PktExtractorProc(&_data->pe, pData, len);
}

void NetworkStateChanged(int state)
{
	//printf("Network state: %d\n", state);
}

P2PCORECBFUNCS cbs = {
	VerifyAuthString,
	ConnCreated,
	ConnAborted,
	ConnFailed,
	OnData,
	NULL,  //on heart beat
	NetworkStateChanged
};

char *lstrip(char *s)
{
	while(*s && isspace(*s)) s++;
	return s;
}

char *sstrip(char *s)
{
	char *t;
	while(*s && isspace(*s)) s++;
	t = s + strlen(s) - 1;
	while(t > s && isspace(*t)) t--;
	t[1] = '\0';
	return s;
}


int main(int argc, char **argv)
{
	if(argc < 4) printUsage(argv[0]);

	p2p_server = id_bound = id_connect = NULL;
#ifdef __LINUX__
	int opt;
	while((opt = getopt(argc, argv, "s:u:c:")) != -1)
	{
		switch(opt)
		{
		case 's':
			p2p_server = optarg;
			break;
		case 'u':
			id_bound = optarg;
			break;
		case 'c':
			id_connect = optarg;
			break;
		case 'n':
			break;
		default:
			printUsage(argv[0]);
		}
	}
#elif defined(WIN32)
	int i;
	for(i=1; i<argc; i++)
	{
		if(strcmp(argv[i], "-s") == 0)
			p2p_server = argv[++i];
		else if(strcmp(argv[i], "-u") == 0)
			id_bound = argv[++i];
		else if(strcmp(argv[i], "-c") == 0)
			id_connect = argv[++i];
		else
			printUsage(argv[0]);
	}
#endif

	if(!p2p_server || (!id_bound && !id_connect)) printUsage(argv[0]);

	const char *svrs[] = { p2p_server };

	PA_NetLibInit();

	P2pCoreInitialize(svrs, 1, id_bound, &cbs);
	printf("Waiting for initialization....\n");
	PA_Sleep(2000);

	if(id_connect) connectTo(p2p_server, id_connect);

	char line[512];
	int err;
	printHelp();
	while(1)
	{
		err = 0;
		printf("Select: "); fflush(stdout);
		if(fgets(line, sizeof(line), stdin) == NULL) 
		{
			printf("EOF of stdin\n");
			break;
		}
		if(line[0] == 'q') break;
		else
		switch(line[0])
		{
		case 'h': printHelp(); break;
		case 'c': err = connectTo(p2p_server, sstrip(line+1)); break;
		case 'r': err = relayTo(p2p_server, sstrip(line+1)); break;
		case 's': printConnectionsState(); break;
		case 'g': err = getFile(g_hconn, sstrip(line+1)); break;
		case 'p': err = putFile(g_hconn, sstrip(line+1)); break;
		case 'e': echo(g_hconn, lstrip(line+1)); break;
		case 'f': flood(g_hconn, lstrip(line+1)); break;
		case 'F': flood2(g_hconn, lstrip(line+1)); break;
		default:
			if(!isspace(line[0]))
				printHelp();
			break;
		}

		if(err)
		{
			printErr(err);
		}
	}

	if(g_hconn) 
	{
		void *ptr;
		P2pConnGetUserData(g_hconn, &ptr);
		free(ptr);
		P2pConnClose(g_hconn);
	}
	P2pCoreTerminate();
	P2pCoreCleanup();

	return 0;
}

int sendCmd(HP2PCONN hconn, int cmd, void *pData, int len)
{
	PA_IOVEC v[2];
	struct cmd_header hd;

	hd.tag = '$';
	hd.cmd = cmd;
	hd.cls = 0;
	hd.pad = 0;
	hd.len = len;
	PA_IoVecSetPtr(&v[0], &hd);
	PA_IoVecSetLen(&v[0], sizeof(hd));
	PA_IoVecSetPtr(&v[1], pData);
	PA_IoVecSetLen(&v[1], len);
	return P2pConnSendV(hconn, 0, v, 2, 2000);
}
int sendResp(HP2PCONN hconn, int cmd, void *pData, int len)
{
	PA_IOVEC v[2];
	struct cmd_header hd;

	hd.tag = '$';
	hd.cmd = cmd;
	hd.cls = 1;
	hd.pad = 0;
	hd.len = len;
	PA_IoVecSetPtr(&v[0], &hd);
	PA_IoVecSetLen(&v[0], sizeof(hd));
	PA_IoVecSetPtr(&v[1], pData);
	PA_IoVecSetLen(&v[1], len);
	return P2pConnSendV(hconn, 0, v, 2, 0);
}

int echo(HP2PCONN hconn, const char *s)
{
	if(!hconn) return 0;

	return sendCmd(hconn, 'e', (void*)s, strlen(s)+1);
}

int flood(HP2PCONN hconn, const char *l)
{
	char *s;
	int j, m = atoi(l);
	s = (char*)malloc(1024);
	for(j=0; j<m*512; j++)
	{
		sprintf(s, "%-10d, ", j);
		memset(s+12, 'a', 1012);
		s[1023] = 0;
		int r = sendCmd(hconn, 'e', s, 1024);
		//printf("%d\n", r);
		//usleep(10);
	}
	return 0;
}
int flood2(HP2PCONN hconn, const char *l)
{
	char *s;
	int j, m = atoi(l);
	s = (char*)malloc(1024);
	for(j=0; j<m*1024; j++)
	{
		sprintf(s, "%-10d, ", j);
		memset(s+12, 'a', 1021);
		s[1023] = 0;
		sendCmd(hconn, '1', s, 1024);
		usleep(10*1000);
	}
	return 0;
}

int connectTo(const char *p2pserv, const char *id)
{
	return P2pConnInit(p2pserv, id, NULL, "admin:admin", 12, strdup(id));
}
int relayTo(const char *p2pserv, const char *id)
{
	return P2pConnInitEx(p2pserv, id, NULL, "admin:admin", 12, strdup(id), P2P_CONNTYPE_RELAY);
}

BOOL printState(HP2PCONN hconn, void *pUser)
{
	return TRUE;
}
void printConnectionsState()
{
	P2pCoreEnumConn(printState, NULL);
}

int getFile(HP2PCONN hconn, const char *remote_file)
{
	if(!hconn) return 0;
	return sendCmd(hconn, 'g', (void*)remote_file, strlen(remote_file)+1);
}

int putFile(HP2PCONN hconn, const char *local_file)
{
	FILE *fp;
	char buff[4096];
	struct ft_header fhd;
	int flen;

	if(!hconn) return 0;

	fp = fopen(local_file, "rb");
	if(fp)
	{
		fseek(fp, 0, SEEK_END);
		flen = ftell(fp);
		rewind(fp);

		const char *slash = strrchr(local_file, '/');
		fhd.hd.cmd = 'p';
		fhd.hd.len = 32 + flen;
		memset(fhd.fn, 0, 32);
		strcpy(fhd.fn, slash?slash+1:local_file);
		P2pConnSend(hconn, 0, &fhd, sizeof(fhd), 0);

		int len;
		while( (len = fread(buff, 1, sizeof(buff), fp)) > 0 )
		{
			P2pConnSend(hconn, 0, buff, len, 0);
		}
	}
	return 0;
}

