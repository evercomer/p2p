#include "FileStruct.h"
#include "apperr.h"
#include "FileAVSyncReader.h"
#include "misc.h"


#define MAX_FRAME_SIZE	200*1024

CFileAVSyncReader::CFileAVSyncReader()
{
	m_pVideoBuffer = m_pAudioBuffer = NULL;
	m_pReader = NULL;
	m_pReaderData = NULL;
	m_bStopWhenReachEof = TRUE;
	m_cb = NULL;
	m_pData = NULL;
}

DWORD CFileAVSyncReader::Initialize(const char *sFileName, PROGRESSCALLBACK cb, void *pData, READER* pReader, void* pRdrParam, BOOL bStopWhenReachEof)
{
	m_cb = cb;
	m_pData = pData;
	if(pReader == NULL)
	{
		m_pReader = FindSuitableReader(sFileName, &m_fileinfo);
		if(!m_pReader) return E_BADFMT;
		m_pReaderData = NULL;
	}
	else
	{
		m_pReader = pReader;
		m_pReaderData = pRdrParam;
	}
	m_bStopWhenReachEof = bStopWhenReachEof;

	DWORD dwRlt = m_pReader->BeginReading(sFileName, &m_pReaderData);
	if(dwRlt) { m_pReader = NULL; return dwRlt; }

	if( (dwRlt = m_pReader->GetFileInfo(&m_fileinfo, m_pReaderData)) )
	{
		m_pReader = NULL;
		return dwRlt;
	}

	//m_dwPlayedDuration = 0;
	m_pVideoBuffer = new CReceiverBuffer(MAX_FRAME_SIZE, 5);
	m_pAudioBuffer = new CReceiverBuffer(2048, 5);

	
	m_iXRate = 0;
	m_dwSysTsLastPlay_A = m_dwSysTsLastPlay_V = PA_GetTickCount();

	m_iPreVal = m_dwPlayedKMs =0;

#if 1
	FRAMENODE *f;
	m_dwTs0 = m_dwTsPlayed_V = m_dwTsPlayed_A = 0;

	while( ReadData() == E_WAITDATA);//预读数据以取得初始帧时间戳
	//while(ReadData() == E_OK);	//填满缓冲区
	if( (f = m_pVideoBuffer->GetDataFrame()) )
		m_dwTs0 = m_dwTsPlayed_A = m_dwTsPlayed_V = f->timeStamp;		//ReadData 可能只读出一种数据
	else if( (f = m_pAudioBuffer->GetDataFrame()) ) 
		m_dwTs0 = m_dwTsPlayed_V = m_dwTsPlayed_A = f->timeStamp;
#else
	m_dwTs0 = m_dwTsPlayed_A = m_dwTsPlayed_V = 0;
#endif
	return 0;
}

CFileAVSyncReader::~CFileAVSyncReader(void)
{
	if(GetActivityStatus() != THREADSTATE_STOPPED) Stop();
	else OnStopThread();
}

void CFileAVSyncReader::OnStopThread(void)
{
	if(m_pReader) 
	{
		m_pReader->EndReading(m_pReaderData);
		m_pReader = NULL;
		m_pReaderData = NULL;
	}
	SAFE_DELETE(m_pAudioBuffer);
	SAFE_DELETE(m_pVideoBuffer);
}

/////////////////////////////////////////////////////////////////////////////////////
// WORKER THREAD CLASS GENERATOR - Do not remove this method!
// MAIN THREAD HANDLER - The only method that must be implemented.
/////////////////////////////////////////////////////////////////////////////////////

void CFileAVSyncReader::CommandHandler(UINT cmd)
{
	switch (cmd)
	{
	case CThread::CMD_INITIALIZE:		// initialize the thread task
		ReaderThreadBeginning();
		m_dwSysTsLastPlay_A = m_dwSysTsLastPlay_V = PA_GetTickCount();
		PostCommand(CMD_RUN);	// fire CThread::CMD_RUN command after proper initialization
		break;

	case CThread::CMD_NONE:
		if(GetActivityStatus() == CThread::THREADSTATE_RUNNING)
			PostCommand(CMD_RUN);
		break;
		
	case CThread::CMD_RUN:				// handle 'OnRun' command
		if (GetActivityStatus() != CThread::THREADSTATE_PAUSED)
		{
			if(!SingleStep()) {
				PostCommand(m_bStopWhenReachEof?CMD_STOP:CMD_PAUSE);
				dbg_msg("EOF\n");
			}
		}
		break;

	case CThread::CMD_PAUSE:			// handle 'OnPause' command
		if (GetActivityStatus() != CThread::THREADSTATE_PAUSED)
		{
		};
		break;

	case CThread::CMD_CONTINUE:			// handle 'OnContinue' command
		PostCommand(CMD_RUN);	// fire CThread::CMD_RUN command
		m_dwSysTsLastPlay_A = m_dwSysTsLastPlay_V = PA_GetTickCount();
		break;

	case CThread::CMD_STOP:
		OnStopThread();
		ReaderThreadEnding();
		break;

	case CMD_USER:	//Seek
		m_dwTsPlayed_V = m_dwTsPlayed_A = m_pReader->SeekKeyFrame(m_tsToLocate, m_pReaderData);

		m_pVideoBuffer->ClearBuffer();
		m_pAudioBuffer->ClearBuffer();
		
		_ForwardLocateTimestamp(m_tsToLocate);

		if(m_cb && m_fileinfo.dwDuration) 
		{
			DWORD progress = (DWORD)(1000.0*(m_dwTsPlayed_V - m_dwTs0)/m_fileinfo.dwDuration);
			if(m_iPreVal != progress)
			{
				m_cb(0, progress, m_dwTsPlayed_V - m_dwTs0, m_pData);
				m_iPreVal = progress;
			}
		}
		//PostCommand(CThread::CMD_RUN);
		break;

	default:							// handle unknown commands...
		break;

	};
}

BOOL CFileAVSyncReader::SingleStep(void)
{
	DWORD ts2 = PA_GetTickCount();
	FRAMENODE *f;

	while( (f = m_pVideoBuffer->GetDataFrame()) )
	{
		if(!f->ready && ( f->timeStamp < m_dwTsPlayed_V ||  
				m_iXRate >= 0 && ((ts2 - m_dwSysTsLastPlay_V) << m_iXRate) >= (f->timeStamp - m_dwTsPlayed_V) ||
				m_iXRate < 0 && ((ts2 - m_dwSysTsLastPlay_V) >> -m_iXRate) >= (f->timeStamp - m_dwTsPlayed_V)  ) )
		{
			f->ready = 1;
			if(m_iXRate == 0) m_dwSysTsLastPlay_V += f->timeStamp - m_dwTsPlayed_V;
			else m_dwSysTsLastPlay_V = ts2;
			m_dwTsPlayed_V = f->timeStamp;
		}
		if(f->ready)
		{
			if(OnFrameReady(RECORD_STREAM_VIDEO, f))			//返回TRUE时从队列中删除FRAME
			{
				m_pVideoBuffer->DequeueFrame(f);
				f = m_pVideoBuffer->GetDataFrame();
			}
			else
				f = m_pVideoBuffer->NextFrame(f);
		}

		break;
	}

	//TRACE("f = %p, f->timeStamp = %x, m_dwTsPlayed_V = %x\n", f, f?f->timeStamp:0, m_dwTsPlayed_V);
	while( (f = m_pAudioBuffer->GetDataFrame()) )
	{
		if( !f->ready && ( f->timeStamp < m_dwTsPlayed_A || 
				m_iXRate >= 0 && ((ts2 - m_dwSysTsLastPlay_A) << m_iXRate) >= (f->timeStamp - m_dwTsPlayed_A) ||
				m_iXRate < 0 && ((ts2 - m_dwSysTsLastPlay_A) >> -m_iXRate) >= (f->timeStamp - m_dwTsPlayed_A)  ) )
		{
			f->ready = 1;
			if(m_iXRate == 0) m_dwSysTsLastPlay_A += f->timeStamp - m_dwTsPlayed_A;
			else m_dwSysTsLastPlay_A = ts2;
			m_dwTsPlayed_A = f->timeStamp;
		}

		if(f->ready)
		{
			if(OnFrameReady(RECORD_STREAM_AUDIO, f))
			{
				m_pAudioBuffer->DequeueFrame(f);
				f = m_pAudioBuffer->GetDataFrame();
			}
			else
				f = m_pAudioBuffer->NextFrame(f);
		}

		break;
	}

	DWORD progress = m_fileinfo.dwDuration?(DWORD)(1000.0*(m_dwTsPlayed_V - m_dwTs0)/m_fileinfo.dwDuration):0;
	if(progress >= 1000) progress = 999;
	if(progress != m_iPreVal || m_dwTsPlayed_V - m_dwPlayedKMs >= 1000)
	{
		if(m_cb) m_cb(0 , progress, m_dwTsPlayed_V - m_dwTs0, m_pData);
		m_iPreVal = progress;
		m_dwPlayedKMs = (m_dwTsPlayed_V - m_dwTs0)/1000*1000;
	}

	if(m_pReader->LookAhead)
		while(ReadData() == E_OK);

	BOOL bDo = m_dwReadData != E_EOF || m_pVideoBuffer->GetDataFrame() != NULL || m_pAudioBuffer->GetDataFrame() != NULL;
	if(!bDo && m_cb) 
		m_cb(0, 1000, m_fileinfo.dwDuration, m_pData);

	return bDo;
}

BOOL CFileAVSyncReader::OnFrameReady(UINT strmType, FRAMENODE *f)
{
	return TRUE;
}
BOOL CFileAVSyncReader::ReaderThreadBeginning()
{
	return TRUE;
}

void CFileAVSyncReader::ReaderThreadEnding()
{
}

#define E_BUFFERFULL	100
//功能: 读取一帧
//参数: ready, 快进时为TRUE，此时读出的帧被设为ready=TRUE, 并且已播放音/视频时标被设为刚读出帧的timeStamp
//返回: 0 -- 读取的帧不支持(被忽略)
//		E_EOF		--- 
//		E_BUFFERFULL --
//		Other		---	装有数据的 FRAMENODE 结构地址
DWORD CFileAVSyncReader::ReadData(BOOL ready)
{
	DWORD st, ts, flag, size;
	if( (m_dwReadData = m_pReader->LookAhead(&st, &ts, &flag, &size, m_pReaderData))) 
		return m_dwReadData;

	FRAMENODE *f;
	if(st == RECORD_STREAM_VIDEO) 
	{
		if( !(f = m_pVideoBuffer->GetFrameToWrite())) return m_dwReadData = E_BUFFERFULL;
		if(!m_pVideoBuffer->ReserveSpace(size)) return m_dwReadData = E_BUFFERFULL;
		f->len = size;
		if(m_pReader->Read(&st, &f->timeStamp, f->pData, &f->len, &f->flags, m_pReaderData) == E_OK)
		{
			m_pVideoBuffer->QueueFrame(f);
			f->isKeyFrame = f->flags & STREAMFLAG_KEYFRAME;
			f->ready = ready;
			if(ready)
				m_dwTsPlayed_V = m_dwTsPlayed_A = f->timeStamp;
		}
	}
	else if(st == RECORD_STREAM_AUDIO) 
	{
		if( !(f = m_pAudioBuffer->GetFrameToWrite())) return m_dwReadData = E_BUFFERFULL;
		if( !m_pAudioBuffer->ReserveSpace(size) ) return m_dwReadData = E_BUFFERFULL;
		f->len = size;
		if(m_pReader->Read(&st, &f->timeStamp, f->pData, &f->len, &f->flags, m_pReaderData) == E_OK)
		{
			m_pAudioBuffer->QueueFrame(f);
			f->isKeyFrame = flag;
		}
	}
	else
	{
		BYTE *buf;
		DWORD len;
		m_pReader->Read(&st, &ts, NULL, &len, &flag, m_pReaderData);
		buf = (BYTE*)malloc(len);
		m_pReader->Read(&st, &ts, buf, &len, &flag, m_pReaderData);
		free(buf);
		return m_dwReadData = ReadData();
	}
	return m_dwReadData = 0;
}

int CFileAVSyncReader::GetXRate()
{
	return m_iXRate;
}
//播入速度为 (1 << xRate) 倍
void CFileAVSyncReader::SetXRate(int xRate)
{
	m_iXRate = xRate;
}

void CFileAVSyncReader::_ForwardLocateTimestamp(DWORD timeStamp)	//ms
{
	if(m_pReader->GetDownloadProgress)
	{
		DWORD ts, len;
		m_pReader->GetDownloadProgress(&ts, &len, m_pReaderData);
		if(ts < timeStamp) timeStamp = ts;
	}
	
	for(;;)
	{
		DWORD rlt = ReadData(TRUE);
		if(rlt == E_EOF || rlt == E_WAITDATA) break;
		m_pAudioBuffer->ClearBuffer();
		FRAMENODE *f = m_pVideoBuffer->GetDataFrame();
		if(f)
		{
//			if(OnFrameReady(RECORD_STREAM_VIDEO, f))
				m_pVideoBuffer->DequeueFrame(f);
		}
		if(m_dwTsPlayed_V >= timeStamp) break;
		if(rlt == E_BUFFERFULL) 
			PA_Sleep(100);		//等待消费线程腾出空间。如果没有消费线程即同步方式，则不会出现缓冲区满的情况
	}
}

void CFileAVSyncReader::LocateTimestamp(DWORD ts, BOOL abs)
{
	if(abs)	{
		if(ts < m_fileinfo.tmStart) ts = 0;
		else ts = 1000 * (ts - m_fileinfo.tmStart);
	}
	m_tsToLocate = ts;

	PostCommand(CThread::CMD_USER);
}

BOOL CFileAVSyncReader::GetFileInfo(FILEINFO *pFi)
{
	memcpy(pFi, &m_fileinfo, sizeof(FILEINFO));
	return TRUE;
}

DWORD CFileAVSyncReader::GetFileDuration()
{
	return m_fileinfo.dwDuration;
}

DWORD CFileAVSyncReader::GetProgress()
{
	return m_fileinfo.dwDuration?(DWORD)(1000.0*(m_dwTsPlayed_V - m_dwTs0)/m_fileinfo.dwDuration):0;
}
void CFileAVSyncReader::PlayOneFrame()
{
	ReadData(TRUE);
	FRAMENODE *f = m_pVideoBuffer->GetDataFrame();
	if(!f) return;
	do {
		if(!SingleStep()) break;
	} while(m_pVideoBuffer->GetDataFrame() == f);
}
//----------------------------------------------------------------------
