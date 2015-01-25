#include "FileDecoder.h"
	
#ifdef __ANDROID__

CFileDecoder::CFileDecoder() : CFileAVSyncReader()
{
}

CFileDecoder::~CFileDecoder()
{
	m_JniPlayer.DeinitJniPlayer();
}

void ProgressCB(UINT act, UINT thousandth, UINT milliseconds, void *pData) {
	CFileDecoder *pFD = (CFileDecoder*)pData;
	pFD->m_JniPlayer.OnProgress(thousandth, milliseconds);
}
DWORD CFileDecoder::Initialize(JNIEnv *env, jobject obj/*java interface: ColorArray*/, 
	const char *sFileName, READER* pReader, void* pRdrParam)
{
	m_JniPlayer.InitJniPlayer(env, obj);
	DWORD rlt = CFileAVSyncReader::Initialize(sFileName, ProgressCB, this, pReader, pRdrParam, FALSE);
	if(rlt) {
		m_JniPlayer.DeinitJniPlayer();
	}
	return rlt;
}

BOOL CFileDecoder::OnFrameReady(UINT strmType, FRAMENODE *f)
{
	if(strmType == RECORD_STREAM_VIDEO) {
		m_JniPlayer.DecodeAndPlayVideo(f->pData, f->len);
	}
	return TRUE;
}

BOOL CFileDecoder::ReaderThreadBeginning()
{
	return m_JniPlayer.ThreadBeginning();
}

void CFileDecoder::ReaderThreadEnding()
{
	m_JniPlayer.ThreadEnding();
}

#else
CFileDecoder::CFileDecoder() {}
CFileDecoder::~CFileDecoder() {}
DWORD CFileDecoder::Initialize(JNIEnv *env, jobject obj, const char *sFileName, READER* pReader, void* pRdrParam) { return 0; }
	
BOOL CFileDecoder::OnFrameReady(UINT strmType, FRAMENODE *f) { return TRUE; }
BOOL CFileDecoder::ReaderThreadBeginning() { return TRUE; }
void CFileDecoder::ReaderThreadEnding() {}
#endif

