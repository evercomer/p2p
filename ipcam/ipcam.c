#include "p2pmplx.h"
#include "ReadWriter.h"
#include "errdefs.h"
#include "ipccmd.h"
#include <ctype.h>

#define E_INVALID_SESS_INDEX  -50001
#define E_NULL_SESS           -50002

#define MAX_SESSION     16
static BOOL s_bRunIpc = FALSE;
static PA_MUTEX s_mutexSlots;
static HP2PMPLX s_hSesses[MAX_SESSION];
static PA_HTHREAD s_hThdAccept;
extern READER OurReader;

#define VIDEO	0x01
#define AUDIO	0x02

#define LIVE_VIDEO_CHNO	0
#define LIVE_AUDIO_CHNO	1
#define PLAYBACK_VIDEO_CHNO	2
#define PLAYBACK_AUDIO_CHNO	3

static PA_THREAD_RETTYPE __STDCALL commandHandleThread(void *p);

//the struct  
typedef struct _MyData {
	HP2PMPLX      hSess;     //the 
	int           iSlot;

	BOOL          bRunCmd;
	PA_HTHREAD    thdCmd;

	BOOL          bRunPb;
	PA_HTHREAD    thdPb;
	DWORD	      dwMediaMask;	//bit 0: send video; bit 1: send audio

	BOOL          bRunLive;
	PA_HTHREAD    thdLive;
	DWORD         dwLiveMediaMask;
} MYSESSDATA;


void _cleanSessData(HP2PMPLX hsess)
{
	MYSESSDATA *pMd;
	if((pMd = P2pMplxGetUserData(hsess)))
	{
		pMd->bRunPb = FALSE;
		pMd->bRunLive = FALSE;
		pMd->bRunCmd = FALSE;
		if(pMd->thdPb) { 
			PA_ThreadWaitUntilTerminate(pMd->thdPb);
			PA_ThreadCloseHandle(pMd->thdPb);
		}
		if(pMd->thdLive) 
		{
			PA_ThreadWaitUntilTerminate(pMd->thdLive);
			PA_ThreadCloseHandle(pMd->thdLive);
		}
		if(pMd->thdCmd)
		{
			PA_ThreadWaitUntilTerminate(pMd->thdCmd);
			PA_ThreadCloseHandle(pMd->thdCmd);
		}
		free(pMd);
		P2pMplxSetUserData(hsess, NULL);
	}
}

static BOOL putSessInSlots(HP2PMPLX hsess)
{
	BOOL b = FALSE;
	int i;
	PA_MutexLock(s_mutexSlots);
	for(i=0; i<MAX_SESSION; i++)
	{
		if(s_hSesses[i] == NULL) 
		{ 
			s_hSesses[i] = hsess; 
			b = TRUE; 
			break; 
		}
	}
	PA_MutexUnlock(s_mutexSlots);
	return i;
}
static void removeSessInSlots(HP2PMPLX hsess)
{
	PA_MutexLock(s_mutexSlots);
	MYSESSDATA *myDat = P2pMplxGetUserData(hsess);
	s_hSesses[myDat->iSlot] = NULL;
	PA_MutexUnlock(s_mutexSlots);
}

void initMySess(HP2PMPLX hsess)
{
	int iSlot = putSessInSlots(hsess);
	if(iSlot >= 0)
	{
		P2pMplxOpenChannel(hsess, SCT_CMD, 0, 3000, 1500);
		MYSESSDATA *myData = (MYSESSDATA*)calloc(sizeof(MYSESSDATA), 1);
		P2pMplxSetUserData(hsess, myData);
		myData->bRunCmd = TRUE;
		myData->iSlot = iSlot;
		myData->thdCmd = PA_ThreadCreate(commandHandleThread, hsess);
		printf("connection %d established.\n", iSlot);
	}
	else
	{
		fprintf(stderr, "Too many connections, connection discarded\n");
		P2pMplxDestroy(hsess);
	}
}



void PlayAudio(MEDIATYPE fmt, BYTE *pData, int len) 
{
	printf("date len: %d\n",len);
}
int Snapshot(int channel, int quality, BYTE **ppBuff, UINT *size)
{
	FILE *fp = fopen("1.jpg", "rb");
	if(fp) {
		fseek(fp, 0, SEEK_END);
		*size = ftell(fp);
		rewind(fp);

		*ppBuff = malloc(*size);
		fread(*ppBuff, 1, *size, fp);

		fclose(fp);
		return 0;
	}
	return -1;
}

void *ipcMediaSendThread(void *p)
{
	void *pRdd;
	BYTE *pBuff;
	DWORD timestamp=0, media_type, size, isKeyFrame;
	DWORD tick0;
	MYSESSDATA *pMyData = (MYSESSDATA*)p;
	READER *pRd = GetDefaultReader();

	//read  the  record
	if(pRd->BeginReading("test.v264", &pRdd) != 0)
	{
		printf("cann't open file: test.v264\n");
		return NULL;
	}	

	pBuff = (BYTE*)malloc(300*1024);
	tick0 = PA_GetTickCount();
	while(pMyData->bRunPb)
	{
		size = 300*1024;
		int err = pRd->Read(&media_type, &timestamp, pBuff, &size, &isKeyFrame, pRdd);
		if(err == 0)
		{
			//dbg_msg("mt: %d, ts: %d\n", media_type, timestamp);
			while(PA_GetTickCount() - tick0 < timestamp)
				PA_Sleep(1); //sleep 1 ms
			{
				int chno, snd = 0;
				if(media_type == MEDIATYPE_VIDEO_H264)
				{
					chno = LIVE_VIDEO_CHNO;//0
					snd = (pMyData->dwMediaMask & 1);
				}
				else
				{
					chno = LIVE_AUDIO_CHNO;//1
					snd = (pMyData->dwMediaMask & 2);
				}
				if(snd)//send the message
				{
					err = P2pMplxSendMediaFrame(pMyData->hSess, chno, media_type, timestamp, isKeyFrame, pBuff, size, 0);
					if(err) fprintf(stderr, "P2pMplxSendMediaFrame: %d\n", err);
				}
			}
		}
		else if(err == E_EOF)
		{
			pRd->SeekKeyFrame(0, pRdd);
			tick0 = PA_GetTickCount();
			PA_Sleep(40);
		}
		else
		{
			printf("read file error: %u\n", err);
			break;
		}
	}
	OurReader.EndReading(pRdd);
	free(pBuff);
	return NULL;
}

//start live video or audio
void ipcStartMediaSend(MYSESSDATA *pMyData, int media)
{
	if((pMyData->dwMediaMask & media) == 0)
		P2pMplxOpenChannel(pMyData->hSess, SCT_MEDIA_SND, media==VIDEO?LIVE_VIDEO_CHNO:LIVE_AUDIO_CHNO, 0, 0);

	if(pMyData->dwMediaMask == 0)
	{
		pMyData->bRunPb = TRUE;
		pMyData->dwMediaMask |= media;
		pMyData->thdPb = PA_ThreadCreate(ipcMediaSendThread, pMyData);
	}
	else
		pMyData->dwMediaMask |= media;
}

//stop live video or audio
void ipcStopMediaSend(MYSESSDATA *pMyData, int media)
{
	if(pMyData->dwMediaMask & media)
		P2pMplxCloseChannel(pMyData->hSess, SCT_MEDIA_SND, media==VIDEO?LIVE_VIDEO_CHNO:LIVE_AUDIO_CHNO);

	if(pMyData->dwMediaMask)
	{
		pMyData->dwMediaMask &= ~media;
		if(pMyData->dwMediaMask == 0)
		{
			pMyData->bRunPb = FALSE;
			PA_ThreadWaitUntilTerminate(pMyData->thdPb);
			pMyData->thdPb = 0;
		}
	}
}

PA_THREAD_RETTYPE ipcListenThread(void *p)
{
	while(s_bRunIpc)
	{
		HP2PMPLX hsess;
		if(P2pMplxAccept(&hsess, 500) == 0)
		{
			initMySess(hsess);
		}
	}

	return (PA_THREAD_RETTYPE)0;
}
//-----------------------------------------
int commandHandler(HP2PMPLX hsess, CMDPKTINFO *pPkt)
{
	MYSESSDATA *pSessDat = P2pMplxGetUserData(hsess);
	if(pPkt->hdr.flags & CPF_IS_RESP)
	{
		switch(pPkt->hdr.cmd)
		{
		case CMD_ECHO:
			printf("echoed: %s\n", (char*)pPkt->pData);
			break;
		}
	}
	else
	{
		switch(pPkt->hdr.cmd)
		{
		case CMD_START_VIDEO:
			ipcStartMediaSend(pSessDat, VIDEO);
			break;
		case CMD_START_AUDIO:
			ipcStartMediaSend(pSessDat, AUDIO);
			break;
		case CMD_STOP_VIDEO:
			ipcStopMediaSend(pSessDat, VIDEO);
			break;
		case CMD_STOP_AUDIO:
			ipcStopMediaSend(pSessDat, AUDIO);
			break;
		case CMD_ECHO:
			printf("<<<: %s\n", (char*)pPkt->pData);
			P2pMplxSendResponse(hsess, 0, pPkt->hdr.cmd, pPkt->pData, pPkt->len, pPkt->hdr.trans_id);
			break;
		case CMD_PTZ_CTRL:
			break;
		}
	}
}

PA_THREAD_RETTYPE __STDCALL commandHandleThread(void *p)
{
	HP2PMPLX hP2pMplx = (HP2PMPLX)p;
	MYSESSDATA *pSessData;
	pSessData = P2pMplxGetUserData(hP2pMplx);

	while(pSessData->bRunCmd)
	{
		int err;
		CMDPKTINFO pkt;
		switch( (err = P2pMplxRecvPacket(hP2pMplx, 0, &pkt, 1000)) )
		{
		case 0:
			commandHandler(hP2pMplx, &pkt);
			P2pMplxReleasePacket(hP2pMplx, 0, &pkt);
			break;
		case P2PE_TIMEOUTED:
			break;
		case P2PE_CONN_ABORTED:
		case P2PE_CONN_TIMEOUTED:
			fprintf(stderr, "session %d aborted due to: %s\n", pSessData->iSlot, err == P2PE_CONN_ABORTED?"abort":"timeout");
			removeSessInSlots(hP2pMplx);
			_cleanSessData(hP2pMplx);
			P2pMplxDestroy(hP2pMplx);
			break;
		default:
			printf("connect %d receive packet failed with error: %d\n", pSessData->iSlot, err);
		}
	}

	return (PA_THREAD_RETTYPE)0;
}

void printUsage(const char *prog)
{
	printf("%s -s p2p_server -u id_bound\n\tBound to id_bound and wait for connection\n", prog);
	exit(0);
}

void printHelp();
void printErr(const char *s, int err);
int showConnections();

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

static char *p2p_server;
int main(int argc, char *argv[])
{
	int opt, err;
	char *id_bound;

	if(argc < 5) printUsage(argv[0]);

	p2p_server = id_bound = id_connect = NULL;
	while((opt = getopt(argc, argv, "s:u:")) != -1)
	{
		switch(opt)
		{
		case 's':
			p2p_server = optarg;
			break;
		case 'u':
			id_bound = optarg;
			break;
		default:
			printUsage(argv[0]);
		}
	}
	if(!p2p_server || !id_bound ) printUsage(argv[0]);


	PA_MutexInit(s_mutexSlots);
	P2pMplxGlobalInitialize(p2p_server, NULL, id_bound, NULL);

	//start a thread to accept connection
	s_bRunIpc = TRUE;
	s_hThdAccept = PA_ThreadCreate(ipcListenThread, NULL);

	char line[512];
	printHelp();
	while(1)
	{
		printf("Select: "); fflush(stdout);
		fgets(line, sizeof(line), stdin);
		sstrip(line);
		if(line[0] == 'q') { s_bRunIpc = FALSE; break; }
		else if(line[0] == 'h') printHelp();
		else if(line[0] == 's') showConnections();
	}

	int i;
	for(i=0; i<MAX_SESSION; i++)
	{
		if(s_hSesses[i]) 
		{
			_cleanSessData(s_hSesses[i]);
			P2pMplxDestroy(s_hSesses[i]);
		}
	}

	PA_ThreadCloseHandle(s_hThdAccept);

	P2pMplxGlobalUninitialize();
	PA_MutexUninit(s_mutexSlots);

	return 0;
}

void printHelp()
{
	printf("h. this menu\n");
	printf("s. show connection id\n");
	printf("q. quit\n");
}
void printErr(const char *s, int err)
{
	fprintf(stderr, "%s: %d\n", s, err);
	switch(err)
	{
	}
}

int getFile(HP2PMPLX hsess, const char *s)
{
	return 0;
}

int putFile(HP2PMPLX hsess, const char *s)
{
	return 0;
}

int showConnections()
{
	const char *sct[] = { "local", "p2p", "relay" };
	int i;
	PA_MutexLock(s_mutexSlots);
	for(i=0; i<MAX_SESSION; i++)
	{
		if(s_hSesses[i]) 
		{
			P2PCONNINFO info;
			P2pConnGetInfo(P2pMplxGetConn(s_hSesses[i]), &info);
			printf("%d. %s, %s:%d\n", i, sct[info.ct], inet_ntoa(info.peer.sin_addr), htons(info.peer.sin_port));
		}
	}
	PA_MutexUnlock(s_mutexSlots);
	return 0;
}

