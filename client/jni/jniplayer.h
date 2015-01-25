#ifndef __jniplayer_h__
#define __jniplayer_h__

#include "h264dec.h"
#include "AudioCodec.h"
#include "platform_adpt.h"
#include "jni_adpt.h"
#ifdef __ANDROID__

/* Callback functions from Java Vm
 * Object of JavaCBMethods can be shared between multiple threads
 */
struct JavaCBMethods {
	jmethodID midFrameReady;
	jmethodID midDimensionChanged;
	jmethodID midGetColorBuffer;
	
	jmethodID midAllocateAudioBuffer;
	jmethodID midAudioReady;
	
	jmethodID midOnProgress;
	JavaVM *jvm;
};
struct JavaEventCBMethod {
	jmethodID midOnEvent;
};


/* JniPlayer will no be shared
*/
class JniPlayer {
public:
	JniPlayer();

	int InitJniPlayer(JNIEnv *env, /*IN*/jobject obj/*interface ColorArray*/);
	void DeinitJniPlayer();		// 与InitJniPlayer在同一个线程里调用
	
	BOOL ThreadBeginning();
	void ThreadEnding();
	int DecodeAndPlayVideo(BYTE *pData, UINT len);
	int DecodeAndPlayAudio(MEDIATYPE mt, BYTE *pAudio, UINT len);
	void stopAudio();
	void OnProgress(int thousandth, int milliseconds);
	
protected:
	struct JavaCBMethods	m_jmthds;

protected:
	JNIEnv *m_env;	//JNIEnv in the player thread
	jobject m_obj;	//interface object
	jintArray m_ColorArray;
	jint	*m_pColors;
	
	/* decode h264 */
	HDEC m_hDec;		//h264 decoder
	VDECOUTPUT m_Output;
	int m_width, m_height;
	
	/* decode audio */
	CAudioCodec	m_AudioCodec;
	jbyteArray m_AudioArray;
	jbyte	*m_pPCMAudio;

	
};

#else

class JniPlayer {
public:
	JniPlayer();

	int InitJniPlayer(JNIEnv *env, /*IN*/jobject obj/**/);
	void DeinitJniPlayer();		// 与InitJniPlayer在同一个线程里调用
	
	BOOL ThreadBeginning();
	void ThreadEnding();
	int DecodeAndPlayVideo(BYTE *pData, UINT len);
	int DecodeAndPlayAudio(MEDIATYPE mt, BYTE *pAudio, UINT len);
	void stopAudio();
	void OnProgress(int thousandth, int milliseconds);
	

protected:
	/* decode h264 */
	HDEC m_hDec;		//h264 decoder
	VDECOUTPUT m_Output;
	int m_width, m_height;
	int m_nVideoFrame, m_nAudioFrame;
	
	/* decode audio */
	CAudioCodec	m_AudioCodec;

	int	*m_piObjs;
};
#endif

#endif
