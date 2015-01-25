#include "p2psess.h"
#include "cltcmdwrapper.h"
#include "apperr.h"
#include "mp4rwer.h"

#define LIVE_VIDEO_CHNO	0
#define LIVE_AUDIO_CHNO	1
#define PLAYBACK_VIDEO_CHNO	2
#define PLAYBACK_AUDIO_CHNO	3

static LIST_HEAD(s_SessList);
struct list_head *getSessioList()
{
	return &s_SessList;
}
P2pSession *P2pSession::FindP2pSession(const char *sn)
{
	struct list_head *p;
	list_for_each(p, &s_SessList)
	{
		P2pSession *sess = (P2pSession*)list_entry(p, P2pSession, sess_list);
		if(strcmp(sn, sess->m_SN) == 0)
			return sess;
	}
	return NULL;
}

//////////////////////////////////////////////////////////////
extern void RtspSenderSend(P2pSession *pSess, P2PFRAMEINFO *pFi);
void* __STDCALL ThreadMediaReceiver(void *p)
{
	P2pSession *pSess = (P2pSession*)p;
	pSess->m_JniLivePlayer.ThreadBeginning();
	while(pSess->m_dwMediaMask)
	{
		P2PFRAMEINFO frmInfo;
		int igot = 0;
 		if((pSess->m_dwMediaMask & P2pSession::MEDIA_VIDEO) && DAP2pConnGetFrame(pSess->m_hConn, LIVE_VIDEO_CHNO, &frmInfo, 0) > 0)
		{
			igot++;
			if(frmInfo.mt == MEDIATYPE_VIDEO_H264)
			{
				//LOG("frmInfo.pFrame:%d, frmInfo.len:%d, frmInfo.isKeyFrame:%d", frmInfo.pFrame, frmInfo.len, frmInfo.isKeyFrame);
				if(!(pSess->m_iFlags & SLVF_DONT_DECODE_VIDEO))
					pSess->m_JniLivePlayer.DecodeAndPlayVideo(frmInfo.pFrame, frmInfo.len);
				else
				{
#ifdef __SIMULATE__
					if(pSess->m_pFrameCnts) pSess->m_pFrameCnts[0] ++;
#endif
					RtspSenderSend(pSess, &frmInfo);
				}


				PA_MutexLock(pSess->m_hMutexWriter);
				if(pSess->m_pWData)
				{
					if(frmInfo.isKeyFrame)
						pSess->m_bSynced = TRUE;
					if(pSess->m_bSynced)
					{
						BYTE *pf;
						pf = (BYTE*)frmInfo.pFrame;
						if((pf[4]&0x1f) == 7)
						{
							BYTE *pfnext;
							unsigned len, frmLen;
							BOOL isKey = TRUE;
							len = frmInfo.len;
							do {
								if((pf[4] & 0x1f) == 5) 
								{
									pfnext = NULL;
									frmLen = len;
								}
								else
								{
									pfnext = (BYTE*)memmem(pf+4, len-4, "\x0\x0\x0\x01", 4);
									frmLen = pfnext?(pfnext - pf):len;
								}

								pSess->m_pWriter->Write(frmInfo.mt, frmInfo.ts, pf, frmLen, isKey, pSess->m_pWData);

								isKey = FALSE;
								pf = pfnext;
								len -= frmLen;
							} while(pf);
						}
						else
							pSess->m_pWriter->Write(frmInfo.mt, frmInfo.ts, frmInfo.pFrame, frmInfo.len, frmInfo.isKeyFrame, pSess->m_pWData);
					}
				}
				PA_MutexUnlock(pSess->m_hMutexWriter);
			}
			//if(pWD) pWriter->Write(frmInfo.mt, frmInfo.ts, frmInfo.pFrame, frmInfo.len, frmInfo.isKeyFrame, pWD);
			DAP2pConnReleaseFrame(pSess->m_hConn, LIVE_VIDEO_CHNO, &frmInfo);
		}

		
		while(/*(pSess->m_dwMediaMask & P2pSession::MEDIA_AUDIO) && */DAP2pConnGetFrame(pSess->m_hConn, LIVE_AUDIO_CHNO, &frmInfo, 0) > 0)
		{
			igot++;
			PA_MutexLock(pSess->m_hMutexWriter);
			if(pSess->m_pWData && pSess->m_bSynced)	//Convert to AAC for writing
			{
				if((frmInfo.mt != MEDIATYPE_AUDIO_AAC))
				{
					if(!pSess->m_hAacConv) pSess->m_hAacConv = AacConvBegin(8000, 1, RAW_STREAM);
					int nBytesOut;
					DWORD ts = frmInfo.ts;

					if(AacConv(pSess->m_hAacConv, (MEDIATYPE)frmInfo.mt, frmInfo.pFrame, frmInfo.len, ts) > 0)
					{
						BYTE *pAudioFrame;
						while( (nBytesOut = AacConvReadFrame(pSess->m_hAacConv, &ts, 
										(const BYTE**)&pAudioFrame, FALSE)) > 0 )
						{
							if(nBytesOut > 0)
							{
								if(pSess->m_pWriter->Write(MEDIATYPE_AUDIO_AAC, ts, pAudioFrame, nBytesOut,
											STREAMFLAG_KEYFRAME, pSess->m_pWData) != 0)
									;//return E_IOERROR;
							}
						}
					}
				}
				else
				{
					pSess->m_pWriter->Write(frmInfo.mt, frmInfo.ts, frmInfo.pFrame, 
							frmInfo.len, frmInfo.isKeyFrame, pSess->m_pWData);
				}
			}
			PA_MutexUnlock(pSess->m_hMutexWriter);

			DWORD tick = PA_GetTickCount();
			if(pSess->m_tickLiveAudio == 0) {
				pSess->m_tickLiveAudio = tick;
				pSess->m_tsLiveAudio = frmInfo.ts;
			}

			LOG("tick:%d pSess->m_tickLiveAudio:%d", tick, pSess->m_tickLiveAudio);
			if(!(pSess->m_iFlags & SLVF_DONT_DECODE_AUDIO)) {
				if(tick - pSess->m_tickLiveAudio < 200) {
					pSess->m_JniLivePlayer.DecodeAndPlayAudio((MEDIATYPE)frmInfo.mt, frmInfo.pFrame, frmInfo.len);
					pSess->m_tickLiveAudio = tick;
				}else {
					//discard delayed audio
					pSess->m_tickLiveAudio += frmInfo.ts - pSess->m_tsLiveAudio;
				}
				pSess->m_tsLiveAudio = frmInfo.ts;
			}
			else {
#ifdef __SIMULATE__
				if(pSess->m_pFrameCnts) pSess->m_pFrameCnts[1] ++;
#endif
			}

			DAP2pConnReleaseFrame(pSess->m_hConn, LIVE_AUDIO_CHNO, &frmInfo);
		}

		if(igot == 0) PA_Sleep(10);
	}
	pSess->m_JniLivePlayer.ThreadEnding();
	return 0;
}

void* __STDCALL PlaybackThread(void *p)
{
	P2pSession *pSess = (P2pSession*)p;

	pSess->m_JniPBPlayer.ThreadBeginning();
	while(pSess->m_bRunPB)
	{
		P2PFRAMEINFO frmInfo;
		if(DAP2pConnGetFrame(pSess->m_hConn, PLAYBACK_VIDEO_CHNO, &frmInfo, 10) > 0)
		{
			if(frmInfo.mt == MEDIATYPE_VIDEO_H264)
			{
				pSess->m_JniPBPlayer.DecodeAndPlayVideo(frmInfo.pFrame, frmInfo.len);
				pSess->m_JniPBPlayer.OnProgress(frmInfo.ts/1000, frmInfo.ts);

				PA_MutexLock(pSess->m_hMutexWriter);
				if(pSess->m_pWData)
				{
					if(frmInfo.isKeyFrame)
						pSess->m_bSynced = TRUE;
					if(pSess->m_bSynced)
						pSess->m_pWriter->Write(frmInfo.mt, frmInfo.ts, frmInfo.pFrame, frmInfo.len, frmInfo.isKeyFrame, pSess->m_pWData);
				}
				PA_MutexUnlock(pSess->m_hMutexWriter);
			}
			DAP2pConnReleaseFrame(pSess->m_hConn, PLAYBACK_VIDEO_CHNO, &frmInfo);
		}

		if(DAP2pConnGetFrame(pSess->m_hConn, PLAYBACK_AUDIO_CHNO, &frmInfo, 10) > 0)
		{
			if(frmInfo.mt == MEDIATYPE_AUDIO_G711A)
			{
				//
			}
			else if(frmInfo.mt == MEDIATYPE_AUDIO_ADPCM)
			{

			}
			DAP2pConnReleaseFrame(pSess->m_hConn, PLAYBACK_AUDIO_CHNO, &frmInfo);
		}
	}
	pSess->m_JniPBPlayer.ThreadEnding();
	return 0;
}

P2pSession::P2pSession()
{
	m_dwMediaMask = 0;
	m_bRunPB = FALSE;
	m_thdLive = m_thdPB = PA_HTHREAD_NULL;
	m_hConn = NULL;
	m_pWriter = GetMP4Writer();
	m_pWData = NULL;
	m_bSynced = FALSE;
	PA_MutexInit(m_hMutexWriter);
	m_audioType = 0;
	m_tickLiveAudio = m_tsLiveAudio = 0;
	
	m_hAacConv = NULL;
	m_iFlags = 0;

	m_SN[0] = '\0';
	INIT_LIST_HEAD(&sess_list);
	list_add_tail(&sess_list, &s_SessList);
	m_pRtspSender = NULL;

	PA_EventInit(m_hEvtConn);
	m_hThreadConn = PA_HTHREAD_NULL;
	m_bQuit = FALSE;
	m_iConnError = 0;
#ifdef __SIMULATE__
	m_pFrameCnts = NULL;
#endif
}

P2pSession::~P2pSession()
{
	if(m_hConn)
	{
		//Mute();
		//TerminatePlayback();
		//StopLiveVideo();
		//DAP2pConnDestroy(m_hConn);
		//if(m_hAacConv) { AacConvEnd(m_hAacConv); m_hAacConv = NULL; }
	}
	
	PA_MutexUninit(m_hMutexWriter);
	PA_EventUninit(m_hEvtConn);
	list_del(&sess_list);
}
int P2pSession::StopSession()
{
	if(PA_HTHREAD_NULL != m_hThreadConn)
	{
		m_bQuit = TRUE;
		PA_EventSet(m_hEvtConn);
		PA_ThreadWaitUntilTerminate(m_hThreadConn);
		m_hThreadConn = PA_HTHREAD_NULL;
	}

	if(m_hConn)
	{
		Mute();
		TerminatePlayback();
		StopLiveVideo();
		DAP2pConnDestroy(m_hConn);
		m_hConn = NULL;
	}

	extern void RtspSenderDestroy(P2pSession *pSess);
	RtspSenderDestroy(this);
	//PA_MutexUninit(m_hMutexWriter);
}

int P2pSession::StartSession(const char *svr,const char *sn, const char *user, const char *pswd)
{
}

BOOL P2pSession::StartSessionAsyn(const char *svr, const char *sn, const char *user, const char *pswd)
{
}

int P2pSession::StartLiveVideo(JNIEnv *env, jobject obj/*ColorArray*/, VIDEOQUALITY quality, int vstrm, int channel, int flags)
{
	int err;
	CmdStartVideoReq reqV;

	if(m_hConn==NULL) return 1;

	reqV.channel = channel;
	reqV.devType = DEVTYPE_PHONE;
	reqV.quality = quality;
	reqV.mediaChn = LIVE_VIDEO_CHNO;
	reqV.vstrm = vstrm;

	//Open media channel 0 for video
	DAP2pConnOpenChannel(m_hConn, DAP2P_MEDIA_CHANNEL, LIVE_VIDEO_CHNO, 2*1024*1024, 300*1024);
	if( (err = DAP2pCmdStartLiveVideo(m_hConn, &reqV)) && err != DCSE_TIMEOUTED )
	{
		DAP2pConnCloseChannel(m_hConn, DAP2P_MEDIA_CHANNEL, LIVE_VIDEO_CHNO);
		return err;
	}

	m_iFlags = flags;
#ifdef __SIMULATE__
	m_pFrameCnts = (int*)obj;
#endif
	if(m_dwMediaMask == 0)
	{
		m_dwMediaMask |= MEDIA_VIDEO;
		m_JniLivePlayer.InitJniPlayer(env, obj);
		m_thdLive = PA_ThreadCreate(ThreadMediaReceiver, this);
	}
	else
		m_dwMediaMask |= MEDIA_VIDEO;

	return 0;
}

void P2pSession::StopLiveVideo()
{
	if(m_dwMediaMask & MEDIA_VIDEO)
	{
		//Mute();
		DAP2pConnSendCommand(m_hConn, 0, CMD_STOP_VIDEO, NULL, 0);
		m_dwMediaMask &= ~MEDIA_VIDEO;
		if(m_dwMediaMask == 0)
		{
			PA_ThreadWaitUntilTerminate(m_thdLive);
			m_thdLive = PA_HTHREAD_NULL;
			m_JniLivePlayer.DeinitJniPlayer();
			DAP2pConnCloseChannel(m_hConn, DAP2P_MEDIA_CHANNEL, LIVE_VIDEO_CHNO);
		}
	}
}

int P2pSession::StartRecord(const char *fn, const char *name)
{
	PA_MutexLock(m_hMutexWriter);
	if(m_pWData)
	{
		m_pWriter->EndWriting(m_pWData);
		m_pWData = NULL;
		m_bSynced = FALSE;
	}
	BOOL b = m_pWriter->BeginWriting(fn, 3, &m_pWData) == 0;
	if(b)
	{
		if(name) m_pWriter->WriteTag(TAG_DEVICENAME, name, strlen(name), m_pWData);
		struct dcs_chnvideo cv = { 0, 0 };
		//DAP2pCmdRequestIFrame(m_hConn, &cv);
	}
	PA_MutexUnlock(m_hMutexWriter);
	return b?0:E_CANNOTCREATEFILE;
}

void P2pSession::StopRecord()
{
	PA_MutexLock(m_hMutexWriter);
	if(m_pWData)
	{
		m_pWriter->EndWriting(m_pWData);
		m_pWData = NULL;
		m_bSynced = false;
		if(m_hAacConv) { AacConvEnd(m_hAacConv); m_hAacConv = NULL; }
	}
	PA_MutexUnlock(m_hMutexWriter);
}

int P2pSession::Vocalize()
{
	if(m_dwMediaMask & MEDIA_AUDIO) return 0;

	CmdStartAudioReq reqA;
	int err;

	//Open media channel 1 for audio
	DAP2pConnOpenChannel(m_hConn, DAP2P_MEDIA_CHANNEL, LIVE_AUDIO_CHNO, 4*2048, 2048);

	reqA.mediaChn = LIVE_AUDIO_CHNO;
	m_tickLiveAudio = m_tsLiveAudio = 0;
	if( (err = DAP2pConnSendCommand(m_hConn, 0, CMD_START_AUDIO, &reqA, sizeof(reqA))) < 0 )
	//if( (err = DAP2pCmdStartLiveAudio(m_hConn, &reqA)) && err != DCSE_TIMEOUTED )
	{
		DAP2pConnCloseChannel(m_hConn, DAP2P_MEDIA_CHANNEL, LIVE_AUDIO_CHNO);
		return err;
	}

	if(m_dwMediaMask == 0)
	{
		m_dwMediaMask |= MEDIA_AUDIO;
		//m_JniLivePlayer.InitJniPlayer(env, obj);
		m_thdLive = PA_ThreadCreate(ThreadMediaReceiver, this);
	}
	else m_dwMediaMask |= MEDIA_AUDIO;

	return 0;
}

void P2pSession::Mute()
{
	if(m_dwMediaMask & MEDIA_AUDIO)
	{
		DAP2pConnSendCommand(m_hConn, 0, CMD_STOP_AUDIO, NULL, 0);
		DAP2pConnCloseChannel(m_hConn, DAP2P_MEDIA_CHANNEL, LIVE_AUDIO_CHNO);

		m_JniLivePlayer.stopAudio();
		m_dwMediaMask &= ~MEDIA_AUDIO;
	}
}

int P2pSession::InitTalkBack(){
	int p1 = 101;
	int ret = DAP2pCmdGetDefault(m_hConn, &p1, &m_audioType);
	//LOG("m_audioType:%d, ret:%d", m_audioType, ret);
	if (ret == 0){
		if (m_audioType == 0){
			return DCSS_LACK_OF_RESOURCE;
		}else if (m_audioType < MEDIATYPE_AUDIO_G711A && m_audioType >= MEDIATYPE_AUDIO_MAX){
			return -1;
		}
	}
	return ret;
}

int P2pSession::GetTalkBackAudioType(){
	return m_audioType;
}

 int P2pSession::TalkBack(BYTE *buff,int len)
{
	return DAP2pConnSendCommand(m_hConn, 0, CMD_TALKBACK, buff, len);
}

int P2pSession::StopTalkBack()
{
	m_audioType = 0;
	return DAP2pCmdStopTalkBack(m_hConn);
}

void P2pSession::PtzControl(int act, int param1, int param2)
{
	CmdPtzCtrlReq ptzCtrl;
	
	ptzCtrl.code = act;
	ptzCtrl.channel = 0;
	ptzCtrl.para1 = param1;
	ptzCtrl.para2 = param2;

	DAP2pCmdPTZControl(m_hConn, &ptzCtrl);
}

int P2pSession::ChangePassword(const char *pswd)
{
	return DAP2pCmdChangePassword(m_hConn,pswd);
}

int P2pSession::InitPlayback(JNIEnv *env, jobject obj/*ColorArray*/)
{
	m_JniPBPlayer.InitJniPlayer(env, obj);
	return 0;
}

int P2pSession::LocateAndPlay(const struct dcs_datetime *pDt)
{
	if(!m_bRunPB)
	{
		DAP2pConnOpenChannel(m_hConn, DAP2P_MEDIA_CHANNEL, PLAYBACK_VIDEO_CHNO, 2*1024*1024, 300*1024);
		DAP2pConnOpenChannel(m_hConn, DAP2P_MEDIA_CHANNEL, PLAYBACK_AUDIO_CHNO, 4*1500, 1500);
		
		m_bRunPB = TRUE;
		m_thdPB = PA_ThreadCreate(PlaybackThread, (void*)this);
		//DAP2pCmdLocateAndPlay(m_hConn, pDt);
	}
	return 0;
}

void P2pSession::TerminatePlayback()
{
	if(m_bRunPB)
	{
		m_bRunPB = FALSE;
		DAP2pConnSendCommand(m_hConn, 0, CMD_PB_TERMINATE, NULL, 0);

		PA_ThreadWaitUntilTerminate(m_thdPB);
		m_thdPB = PA_HTHREAD_NULL;
		m_JniPBPlayer.DeinitJniPlayer();
		DAP2pConnCloseChannel(m_hConn, DAP2P_MEDIA_CHANNEL, PLAYBACK_VIDEO_CHNO);
		DAP2pConnCloseChannel(m_hConn, DAP2P_MEDIA_CHANNEL, PLAYBACK_AUDIO_CHNO);
	}
}

int P2pSession::SetXRate(int xrate)
{
	if(m_bRunPB)
	{
		return 1;//DAP2pCmdSetXRate(m_hConn, xrate);
	}
	return 0;
}




