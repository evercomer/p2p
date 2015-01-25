#ifndef __p2psess_h__
#define __p2psess_h__

#include "platform_adpt.h"
#include "jniplayer.h"
#include "ipccmd.h"
#include "p2pmplx.h"
#include "ReadWriter.h"
#include "to_aac.h"
#include "linux_list.h"

/* Definition should be same as in JNI.java */
#define SLVF_DONT_DECODE_VIDEO	0x01
#define SLVF_DONT_DECODE_AUDIO	0x02

struct ConnParam {
	char server[48];
	char sn[32];
	char user[20];
	char passwd[20];
};


class P2pSession {
friend void* __STDCALL ThreadMediaReceiver(void *p);
friend void* __STDCALL PlaybackThread(void *p);
friend PA_THREAD_RETTYPE __STDCALL ConnectThread(void *p);
public:
	P2pSession();
	~P2pSession();

	int StartSession(const char *svr,const char *sn, const char *user, const char *pswd);
	int StartSessionAsyn(const char *svr,const char *sn, const char *user, const char *pswd);
	int StopSession();
	int StartLiveVideo(JNIEnv *env, jobject obj/*ColorArray*/, VIDEOQUALITY quality, int vstrm, int channel, 
			int flags/* 0 or combination of SLVF_xxx */);
	void StopLiveVideo();
	int InitTalkBack();
	int GetTalkBackAudioType();
	int TalkBack(BYTE *buff,int len);
	int StopTalkBack();
	int Vocalize();
	void Mute();
	void PtzControl(int act, int param1, int param2);

	int StartRecord(const char *fn, const char *name);
	void StopRecord();

	int InitPlayback(JNIEnv *env, jobject obj/*ColorArray*/);
	int LocateAndPlay(const struct dcs_datetime *pDt);
	void TerminatePlayback();
	int SetXRate(int xrate);
	int ChangePassword(const char *pswd);

	HDAP2PCONN GetConnectionHandle() { return m_hConn; }

public:
	void	*m_pRtspSender;	//used by rtsp service

public:
	static P2pSession *FindP2pSession(const char *sn);

private:
	struct list_head sess_list;
	char	m_SN[32];
#ifdef __SIMULATE__
	int	*m_pFrameCnts;
#endif
private:
	//For asynchronous connecting
	struct ConnParam m_ConnParam;
	BOOL	m_bQuit;
	PA_EVENT m_hEvtConn;
	PA_HTHREAD m_hThreadConn;
	int	m_iConnError;

protected:
	HDAP2PCONN	m_hConn;
	enum { MEDIA_VIDEO = 0x01, MEDIA_AUDIO = 0x02 };

	DWORD	m_dwMediaMask;
	pthread_t	m_thdLive;

	JniPlayer	m_JniLivePlayer;

	PA_MUTEX	m_hMutexWriter;
	WRITER *m_pWriter;
	void *m_pWData;
	BOOL	m_bSynced;

	DWORD	m_tickLiveAudio, m_tsLiveAudio;
	int m_audioType;

	BOOL	m_bRunPB;
	pthread_t m_thdPB;

	JniPlayer	m_JniPBPlayer;

	HAACCONV	m_hAacConv;

	int	m_iFlags;
};

struct list_head *getSessioList();
P2pSession *findP2pSession(const char *sn);

#endif
