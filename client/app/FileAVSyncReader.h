#pragma once
#include "Thread.h"
#include "ReadWriter.h"
#include "ReceiverBuffer.h"

#define PLAYING_PROGRESS	0
#define DOWNLOADING_PROGRESS	1
typedef void (*PROGRESSCALLBACK)(UINT act, UINT thousandth, UINT milliseconds, void *pData);

class EXTERN CFileAVSyncReader : public CThread
{
friend PA_ThreadRoutine FileSyncReaderThread;
public:
// Construction & Destruction
	CFileAVSyncReader();
	virtual ~CFileAVSyncReader(void);

	DWORD Initialize(const char *sFileName, PROGRESSCALLBACK cb, void *pData, READER* pReader = NULL, void* pRdrParam = NULL, BOOL bStopWhenReachEof = TRUE);
	
	void PlayOneFrame();
	void LocateTimestamp(DWORD timeStamp, BOOL abs = FALSE);
	
	int GetXRate();
	void SetXRate(int xRate);

	DWORD GetProgress();

	DWORD GetFileDuration();
	BOOL GetFileInfo(FILEINFO *pFi);

protected:
	/////////////////////////////////////////////////////////////////////////////////
	DWORD ReadData(BOOL ready=FALSE/*TRUE For _ForwardLocateTimestamp*/);
	void _ForwardLocateTimestamp(DWORD timeStamp);	//ms

	virtual BOOL OnFrameReady(UINT strmType, FRAMENODE *f);
	virtual BOOL ReaderThreadBeginning();
	virtual void ReaderThreadEnding();

	void CommandHandler(UINT cmd);
	void OnStopThread();
	BOOL SingleStep();

protected:
	PROGRESSCALLBACK m_cb;
	void		*m_pData;

protected:
	DWORD	m_tsToLocate;		//For LocateTimestamp

	BOOL	m_bStopWhenReachEof;


	DWORD	m_dwReadData;	//Result of ReadData;
/////////////////////
	DWORD	m_dwTs0;
	FILEINFO m_fileinfo;

	DWORD	m_dwPlayedKMs;
	DWORD	m_dwSysTsLastPlay_V;
	DWORD	m_dwTsPlayed_V;
	CReceiverBuffer *m_pVideoBuffer;
	DWORD	m_dwVFlag;
	
	DWORD	m_dwSysTsLastPlay_A;
	DWORD	m_dwTsPlayed_A;
	CReceiverBuffer *m_pAudioBuffer;
	
	READER	*m_pReader;
	void	*m_pReaderData;

	int		m_iXRate;		//倍速

	int			m_iPreVal;	//上一次发送UM_PROGRESS的进度值
};
