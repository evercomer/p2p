#ifndef __FileDecoder_h__
#define __FlleDecoder_h__

#include "FileAVSyncReader.h"
#include "jniplayer.h"
#include "jni_adpt.h"

class CFileDecoder : public CFileAVSyncReader {
friend void ProgressCB(UINT act, UINT thousandth, UINT milliseconds, void *pData);
public:
	CFileDecoder();
	virtual ~CFileDecoder();
	DWORD Initialize(JNIEnv *env, jobject obj/*java interface: ColorArray*/, 
			const char *sFileName, READER* pReader = NULL, void* pRdrParam = NULL);
	
protected:
	virtual BOOL OnFrameReady(UINT strmType, FRAMENODE *f);
	virtual BOOL ReaderThreadBeginning();
	virtual void ReaderThreadEnding();

protected:
#ifdef __ANDROID__
	JniPlayer	m_JniPlayer;
#endif
};

#endif
