#include "p2psess.h"
#include "ReadWriter.h"
#include "errdefs.h"
#include "ipccmd.h"
#include <ctype.h>

#define E_INVALID_SESS_INDEX  -50001
#define E_NULL_SESS           -50002

#define MAX_SESSION     16
static BOOL s_bRunIpc = FALSE;
static PA_MUTEX s_mutexSlots;
static HP2PSESS s_hSesses[MAX_SESSION];

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
	HP2PSESS      hSess;     //the 
	BOOL          isCaller;
	int           iSlot;

	BOOL          bRunCmd;
	PA_HTHREAD    thdCmd;

	DWORD	      dwMediaMask;	//bit 0: send video; bit 1: send audio
	BOOL          bRunPb;
	PA_HTHREAD    thdPb;

	BOOL          bRunLive;
	PA_HTHREAD    thdLive;
	DWORD         dwLiveMediaMask;
} MYSESSDATA;


void _cleanSessData(HP2PSESS hsess)
{
	MYSESSDATA *pMd;
	if((pMd = P2pSessGetUserData(hsess)))
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
		P2pSessSetUserData(hsess, NULL);
	}
}

static BOOL putSessInSlots(HP2PSESS hsess)
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
static void removeSessInSlots(HP2PSESS hsess)
{
	PA_MutexLock(s_mutexSlots);
	MYSESSDATA *myDat = P2pSessGetUserData(hsess);
	s_hSesses[myDat->iSlot] = NULL;
	PA_MutexUnlock(s_mutexSlots);
}

void initMySess(HP2PSESS hsess, BOOL isCaller)
{
	int iSlot = putSessInSlots(hsess);
	if(iSlot >= 0)
	{
		P2pSessOpenChannel(hsess, SCT_CMD, 0, 3000, 1500);
		MYSESSDATA *myData = (MYSESSDATA*)calloc(sizeof(MYSESSDATA), 1);
		P2pSessSetUserData(hsess, myData);
		myData->bRunCmd = TRUE;
		myData->iSlot = iSlot;
		myData->isCaller = isCaller;
		myData->thdCmd = PA_ThreadCreate(commandHandleThread, hsess);
		printf("connection %d established.\n", iSlot);
	}
	else
	{
		fprintf(stderr, "Too many connections, connection discarded\n");
		P2pSessDestroy(hsess);
	}
}



void camPlayAudio(MEDIATYPE fmt, BYTE *pData, int len) 
{
	printf("date len: %d\n",len);
}
int camSnapshot(int channel, int quality, BYTE **ppBuff, UINT *size)
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

void *camMediaSendThread(void *p)
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
					err = P2pSessSendMediaFrame(pMyData->hSess, chno, media_type, timestamp, isKeyFrame, pBuff, size, 0);
					if(err) fprintf(stderr, "P2pSessSendMediaFrame: %d\n", err);
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
void camStartMediaSend(MYSESSDATA *pMyData, int media)
{
	if((pMyData->dwMediaMask & media) == 0)
		P2pSessOpenChannel(pMyData->hSess, SCT_MEDIA_SND, media==VIDEO?LIVE_VIDEO_CHNO:LIVE_AUDIO_CHNO, 0, 0);

	if(pMyData->dwMediaMask == 0)
	{
		pMyData->bRunPb = TRUE;
		pMyData->dwMediaMask |= media;
		pMyData->thdPb = PA_ThreadCreate(camMediaSendThread, pMyData);
	}
	else
		pMyData->dwMediaMask |= media;
}

//stop live video or audio
void camStopMediaSend(MYSESSDATA *pMyData, int media)
{
	if(pMyData->dwMediaMask & media)
		P2pSessCloseChannel(pMyData->hSess, SCT_MEDIA_SND, media==VIDEO?LIVE_VIDEO_CHNO:LIVE_AUDIO_CHNO);

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

PA_THREAD_RETTYPE camListenThread(void *p)
{
	while(s_bRunIpc)
	{
		HP2PSESS hsess;
		if(P2pSessAccept(&hsess, 500) == 0)
		{
			initMySess(hsess, FALSE);
		}
	}

	return (PA_THREAD_RETTYPE)0;
}
//-----------------------------------------
// Data used for recording
//
static WRITER *g_pWriter = NULL;
struct DataForRec {
	PA_MUTEX mutex;
	BOOL bRecording;
	BOOL bSynced;
	void *pWrtD;
} RecData;
//------------------
int cltStartRecord(const char *fn)
{
	if(!g_pWriter) 
	{
		/* called in main */
		g_pWriter = GetDefaultWriter();
		PA_MutexInit(RecData.mutex);
	}

	if(RecData.bRecording) return -1;
	if(g_pWriter->BeginWriting(fn, 0/*ignored*/, (void**)&RecData.pWrtD))
	{
		RecData.bSynced = FALSE;
		RecData.bRecording = TRUE;
		return 0;
	}
	return -1;
}
void cltStopRecord()
{
	PA_MutexLock(RecData.mutex);
	if(RecData.bRecording)
	{
		RecData.bRecording = FALSE;
		g_pWriter->EndWriting(RecData.pWrtD);
		RecData.pWrtD = NULL;
	}
	PA_MutexUnlock(RecData.mutex);
}
//-----------------------------------------
int camCommandHandler(HP2PSESS hsess, CMDPKTINFO *pPkt)
{
	MYSESSDATA *pSessDat = P2pSessGetUserData(hsess);
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
			camStartMediaSend(pSessDat, VIDEO);
			break;
		case CMD_START_AUDIO:
			camStartMediaSend(pSessDat, AUDIO);
			break;
		case CMD_STOP_VIDEO:
			camStopMediaSend(pSessDat, VIDEO);
			break;
		case CMD_STOP_AUDIO:
			camStopMediaSend(pSessDat, AUDIO);
			break;
		case CMD_ECHO:
			printf("<<<: %s\n", (char*)pPkt->pData);
			P2pSessSendResponse(hsess, 0, pPkt->hdr.cmd, pPkt->pData, pPkt->len, pPkt->hdr.trans_id);
			break;
		}
	}
}

PA_THREAD_RETTYPE __STDCALL commandHandleThread(void *p)
{
	HP2PSESS hP2pSess = (HP2PSESS)p;
	MYSESSDATA *pSessData;
	pSessData = P2pSessGetUserData(hP2pSess);

	while(pSessData->bRunCmd)
	{
		int err;
		CMDPKTINFO pkt;
		switch( (err = P2pSessRecvPacket(hP2pSess, 0, &pkt, 1000)) )
		{
		case 0:
			camCommandHandler(hP2pSess, &pkt);
			P2pSessReleasePacket(hP2pSess, 0, &pkt);
			break;
		case P2PE_TIMEOUTED:
			break;
		case P2PE_CONN_ABORTED:
		case P2PE_CONN_TIMEOUTED:
			fprintf(stderr, "session %d aborted due to: %s\n", pSessData->iSlot, err == P2PE_CONN_ABORTED?"abort":"timeout");
			removeSessInSlots(hP2pSess);
			_cleanSessData(hP2pSess);
			P2pSessDestroy(hP2pSess);
			break;
		default:
			printf("connect %d receive packet failed with error: %d\n", pSessData->iSlot, err);
		}
		if(pSessData->isCaller)
		{
			//P2pSessSendRequest();
		}
	}

	return (PA_THREAD_RETTYPE)0;
}

PA_THREAD_RETTYPE __STDCALL cltMediaRecvThread(void *p)
{
	HP2PSESS hP2pSess = (HP2PSESS)p;
	MYSESSDATA *pSessData;
	pSessData = P2pSessGetUserData(hP2pSess);

	int vframe_no = 0, aframe_no = 0;
	while(pSessData->bRunLive)
	{
		P2PFRAMEINFO frmInfo;
		int iGot = 0, err;
		if( (err == P2pSessGetFrame(hP2pSess, LIVE_VIDEO_CHNO, &frmInfo, 0)) == 0)
		{
			iGot++;
			if(frmInfo.mt == MEDIATYPE_VIDEO_H264)
			{
				//Decode video frame
				//DecodeAndPlay(frmInfo.pData, frmInfo.len);
				printf("video frame %d, audio frame %d\r", ++vframe_no, aframe_no); fflush(stdout);

				/* Write Video. Wait until a keyframe is arrived */
				PA_MutexLock(RecData.mutex);
				if(RecData.bRecording)
				{
					if(!RecData.bSynced && frmInfo.isKeyFrame)
						RecData.bSynced = TRUE;
					if(RecData.bSynced)
						if(!g_pWriter->Write(frmInfo.mt, frmInfo.ts, frmInfo.pFrame, frmInfo.len, frmInfo.isKeyFrame, RecData.pWrtD))
							;//failed
				}
				PA_MutexUnlock(RecData.mutex);
			}
			P2pSessReleaseFrame(hP2pSess, LIVE_VIDEO_CHNO, &frmInfo);
		}
		if( (err = P2pSessGetFrame(hP2pSess, LIVE_AUDIO_CHNO, &frmInfo, 0)) == 0)
		{
			iGot++;
			printf("video frame %d, audio frame %d\r", vframe_no, ++aframe_no); fflush(stdout);
			if(frmInfo.mt == MEDIATYPE_AUDIO_ALAW)
			{
			}
			else if(frmInfo.mt == MEDIATYPE_AUDIO_ADPCM)
			{
			}

			/* Write audio */
			PA_MutexLock(RecData.mutex);
			if(RecData.bRecording && RecData.bSynced)
			{
				if(!g_pWriter->Write(frmInfo.mt, frmInfo.ts, frmInfo.pFrame, frmInfo.len, frmInfo.isKeyFrame, RecData.pWrtD))
						;//failed
			}
			PA_MutexUnlock(RecData.mutex);

			P2pSessReleaseFrame(hP2pSess, LIVE_AUDIO_CHNO, &frmInfo);
		}
		if(iGot == 0) PA_Sleep(10);
	}

	return (PA_THREAD_RETTYPE)0;
}

void printUsage(const char *prog)
{
	printf("%s -s p2p_server -c id_connect_to\n\tConnecct to id_connect_to\n", prog);
	printf("%s -s p2p_server -u id_bound\n\tBound to id_bound and wait for connection\n", prog);
	printf("%s -s p2p_server -u id_bound -c id_connect_to\nBound to id_bound and connect to id_connect\n", prog);
	exit(0);
}

void printHelp();
void printErr(const char *s, int err);
int openLiveVideo(HP2PSESS hsess, const char *s);
int cltStopLiveVideo(HP2PSESS hsess, const char *s);
int cltOpenLiveAudio(HP2PSESS hsess, const char *s);
int cltStopLiveAudio(HP2PSESS hsess, const char *s);
int cltGetFile(HP2PSESS hsess, const char *s);
int cltPutFile(HP2PSESS hsess, const char *s);
int echo(HP2PSESS hsess, const char *s);
int connectTo(const char *s);
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
	char *id_bound, *id_connect;

	if(argc < 4) printUsage(argv[0]);

	p2p_server = id_bound = id_connect = NULL;
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
		default:
			printUsage(argv[0]);
		}
	}
	if(!p2p_server || (!id_bound && !id_connect)) printUsage(argv[0]);


	PA_MutexInit(s_mutexSlots);
	P2pSessGlobalInitialize(p2p_server, NULL, id_bound, NULL);

	if(id_connect)
	{
		connectTo(id_connect);
	}
	else
	{
		//start a thread to accept connection
		s_bRunIpc = TRUE;
		PA_HTHREAD hThd = PA_ThreadCreate(camListenThread, NULL);
		PA_ThreadCloseHandle(hThd);
	}

	char line[512];
	printHelp();
	while(1)
	{
		err = 0;
		printf("Select: "); fflush(stdout);
		fgets(line, sizeof(line), stdin);
		sstrip(line);
		if(line[0] == 'q') { s_bRunIpc = FALSE; break; }
		else if(line[0] == 'h') printHelp();
		else if(line[0] == 'c') connectTo(line+1);
		else if(line[0] == 's') showConnections();
		else
		{
			//L[index{,| }]other_parameters...
			//L=command letter
			char *ptr;
			int slot = strtoul(line+1, &ptr, 0);
			if(slot >= MAX_SESSION) { printf("Invalid session index\n"); continue; }
			if(!*ptr && *ptr == ',' || isspace(*ptr)) ptr++;
			HP2PSESS hsess = s_hSesses[slot];
			if(!hsess) { printf("NULL session handle\n"); continue; }
			switch(line[0])
			{
				case 'v': err = openLiveVideo(hsess, ptr); break;
				case 'V': err = cltStopLiveVideo(hsess, ptr); break;
				case 'a': err = cltOpenLiveAudio(hsess, ptr); break;
				case 'A': err = cltStopLiveAudio(hsess, ptr); break;
				case 'g': err = cltGetFile(hsess, ptr); break;
				case 'p': err = cltPutFile(hsess, ptr); break;
				case 'e': err = echo(hsess, ptr); break;
				default:
						  break;
			}
		}

		if(err)
		{
			printErr("*", err);
		}
	}

	int i;
	for(i=0; i<MAX_SESSION; i++)
	{
		if(s_hSesses[i]) 
		{
			_cleanSessData(s_hSesses[i]);
			P2pSessDestroy(s_hSesses[i]);
		}
	}

	P2pSessGlobalUninitialize();
	PA_MutexUninit(s_mutexSlots);

	return 0;
}

void printHelp()
{
	printf("h. this menu\n");
	printf("ca. asynchronously connect to. [ca[ ]id_connect_to]\n");
	printf("c. connect to. [c[ ]id_connect_to]\n");
	printf("s. show connection id\n");
	printf("q. quit\n");
	printf("-------- following command need an connection id N------\n");
	printf("v. open live video\n");
	printf("V. stop live video\n");
	printf("a. open live audio\n");
	printf("A. close live audio\n");
	printf("g. get file. [gN[ ]remote_file]\n");
	printf("p. put file. [pN[ ]local_file]\n");
	printf("e. echo. [eN[ ]any_text]\n");
}
void printErr(const char *s, int err)
{
	fprintf(stderr, "%s: %d\n", s, err);
	switch(err)
	{
	}
}

int openLiveVideo(HP2PSESS hsess, const char *s)
{
	MYSESSDATA *pSessData;
	int q = 0, err;

	pSessData = P2pSessGetUserData(hsess);
	if(pSessData->dwLiveMediaMask & VIDEO) return 0;

	if(!P2pSessOpenChannel(hsess, SCT_MEDIA_RCV, LIVE_VIDEO_CHNO, 2<<20, 384<<10))
	{
		fprintf(stderr, "Cann't open channel LIVE_VIDEO_CHNO\n");
		return -1;
	}

	err = P2pSessSendRequest(hsess, 0, CMD_START_VIDEO, &q, sizeof(int));
	if(err)
	{
		printErr("P2pSessSendRequest, CMD_START_VIDEO", err);
		P2pSessCloseChannel(hsess, SCT_MEDIA_RCV, LIVE_VIDEO_CHNO);
		return -1;
	}

	if(pSessData->dwLiveMediaMask == 0)
		pSessData->thdLive = PA_ThreadCreate(cltMediaRecvThread, (void*)hsess);
	pSessData->dwLiveMediaMask |= VIDEO;
	return 0;
}

int cltStopLiveVideo(HP2PSESS hsess, const char *s)
{
	MYSESSDATA *pSessData;

	pSessData = P2pSessGetUserData(hsess);
	if(pSessData->dwLiveMediaMask & VIDEO)
	{
		int err = P2pSessSendRequest(hsess, 0, CMD_STOP_VIDEO, NULL, 0);
		if(err)
		{
			printErr("P2pSessSendRequest, CMD_STOP_VIDEO", err);
		}
		P2pSessCloseChannel(hsess, SCT_MEDIA_RCV, LIVE_VIDEO_CHNO);
		pSessData->dwLiveMediaMask &= ~VIDEO;
		if(pSessData->dwLiveMediaMask == 0)
		{
			pSessData->bRunLive = FALSE;
			PA_ThreadWaitUntilTerminate(pSessData->thdLive);
			PA_ThreadCloseHandle(pSessData->thdLive);
		}
	}

	return 0;
}

int cltOpenLiveAudio(HP2PSESS hsess, const char *s)
{
	MYSESSDATA *pSessData;
	int err;

	pSessData = P2pSessGetUserData(hsess);
	if(pSessData->dwLiveMediaMask & AUDIO) return 0;

	if(!P2pSessOpenChannel(hsess, SCT_MEDIA_SND, LIVE_AUDIO_CHNO, 2<<20, 384<<10))
	{
		fprintf(stderr, "Cann't open channel LIVE_AUDIO_CHNO\n");
		return -1;
	}

	err = P2pSessSendRequest(hsess, 0, CMD_START_AUDIO, NULL, 0);
	if(err)
	{
		printErr("P2pSessSendRequest, CMD_START_AUDIO", err);
		P2pSessCloseChannel(hsess, SCT_MEDIA_SND, LIVE_AUDIO_CHNO);
		return -1;
	}

	if(pSessData->dwLiveMediaMask == 0)
		pSessData->thdLive = PA_ThreadCreate(cltMediaRecvThread, (void*)hsess);
	pSessData->dwLiveMediaMask |= AUDIO;
	return 0;
}

int cltStopLiveAudio(HP2PSESS hsess, const char *s)
{
	MYSESSDATA *pSessData;

	pSessData = P2pSessGetUserData(hsess);
	if(pSessData->dwLiveMediaMask & AUDIO)
	{
		int err = P2pSessSendRequest(hsess, 0, CMD_STOP_AUDIO, NULL, 0);
		if(err)
		{
			printErr("P2pSessSendRequest, CMD_STOP_AUDIO", err);
		}
		P2pSessCloseChannel(hsess, SCT_MEDIA_RCV, LIVE_AUDIO_CHNO);
		pSessData->dwLiveMediaMask &= ~AUDIO;

		if(pSessData->dwLiveMediaMask == 0)
		{
			pSessData->bRunLive = FALSE;
			PA_ThreadWaitUntilTerminate(pSessData->thdLive);
			PA_ThreadCloseHandle(pSessData->thdLive);
		}
	}

	return 0;
}

int cltGetFile(HP2PSESS hsess, const char *s)
{
	return 0;
}

int cltPutFile(HP2PSESS hsess, const char *s)
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
			P2pConnGetInfo(P2pSessGetConn(s_hSesses[i]), &info);
			printf("%d. %s, %s:%d\n", i, sct[info.ct], inet_ntoa(info.peer.sin_addr), htons(info.peer.sin_port));
		}
	}
	PA_MutexUnlock(s_mutexSlots);
	return 0;
}

int echo(HP2PSESS hsess, const char *s)
{
	if(hsess && *s)
	{
		int err;
		if( (err = P2pSessSendRequest(hsess, 0, CMD_ECHO, s, strlen(s)+1)) < 0 )
		{
			removeSessInSlots(hsess);
			_cleanSessData(hsess);
			P2pSessDestroy(hsess);
			fprintf(stderr, "P2pSessSendRequest error: %d, connection will be destroied\n", err);
			return err;
		}
	}
	return 0;
}

static void sess_creat_cb(HP2PSESS hsess, int err, void *pUser)
{
	if(hsess) //ok
	{
		printf("connect to %s ok\n", (char*)pUser);
		initMySess(hsess, TRUE);
	}
	else
	{
		fprintf(stderr, "connect to %s failed with error: %d\n", (char*)pUser, err);
	}
	free(pUser);
}

int connectTo(const char *s)
{
	int async = 0;
	if(*s == 'a') { s++; async = 1; }
	while(*s && isspace(*s)) s++;

	if(async)
	{
		P2pSessCreateAsync(p2p_server, s, NULL, NULL, sess_creat_cb, strdup(s));
	}
	else
	{
		HP2PSESS hsess;
		int err = P2pSessCreate(p2p_server, s, NULL/*sident*/, NULL/*auth_str*/, &hsess);
		if(err)
		{
			printf("P2pSessCreate to %s failed: %d\n", s, err);
			return err;
		}

		initMySess(hsess, TRUE);
	}
	return 0;
}

