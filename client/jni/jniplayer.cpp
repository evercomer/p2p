#include "jniplayer.h"
#include "YUV2RGB.h"

#ifdef __ANDROID__
//////////////////////////////////////////////////////////////
/* class JniPlayer */
JniPlayer::JniPlayer()
{
	m_hDec = NULL;
	m_width = m_height = 0;
	m_env = NULL;
	m_obj = NULL;
	m_ColorArray = NULL;
	m_pColors = NULL;
	m_AudioArray = NULL;
	m_pPCMAudio = NULL;
	memset(&m_jmthds, 0, sizeof(m_jmthds));
}
int JniPlayer::InitJniPlayer(JNIEnv *env, jobject obj/*interface ColorArray*/)
{
	if(obj)
	{
		m_obj = env->NewGlobalRef(obj);
		if(!m_jmthds.jvm) {
			env->GetJavaVM(&m_jmthds.jvm);
		}
		m_hDec = CreateDecoder();
	}
	return 0;
}
void JniPlayer::DeinitJniPlayer()
{
	if(m_hDec) {
		DestroyDecoder(m_hDec);
		m_hDec = NULL;
	}
	if(m_obj) {
		JNIEnv *env;
		m_jmthds.jvm->GetEnv((void**)&env, JNI_VERSION_1_2);
		env->DeleteGlobalRef(m_obj);
		m_obj = NULL;
	}
	m_width = m_height = 0;
	m_env = NULL;
	m_ColorArray = NULL;
	m_pColors = NULL;
	m_AudioArray = NULL;
	m_pPCMAudio = NULL;
}

BOOL JniPlayer::ThreadBeginning()
{
	m_jmthds.jvm->AttachCurrentThread(&m_env, NULL);
	if(m_obj)
	{
		jclass cls = m_env->GetObjectClass(m_obj);
		m_jmthds.midFrameReady = m_env->GetMethodID(cls, "frameReady", "()V");
		m_jmthds.midDimensionChanged = m_env->GetMethodID(cls, "dimensionChanged", "(II)Z");
		m_jmthds.midGetColorBuffer = m_env->GetMethodID(cls, "getColorBuffer", "()[I");
		m_jmthds.midAllocateAudioBuffer = m_env->GetMethodID(cls, "allocateAudioBuffer", "()[B");
		m_jmthds.midAudioReady = m_env->GetMethodID(cls, "audioReady", "(I)V");
		m_jmthds.midOnProgress = m_env->GetMethodID(cls, "onProgress", "(II)V");
	}
	return TRUE;
}
void JniPlayer::ThreadEnding()
{
	if(m_ColorArray) {
		m_env->ReleaseIntArrayElements(m_ColorArray, m_pColors, 0);
	}
	m_jmthds.jvm->DetachCurrentThread();
}
int JniPlayer::DecodeAndPlayVideo(BYTE *pData, UINT len)
{	
	int rlt = Decode(m_hDec, pData, len, &m_Output);
	//LOG("Packet length: %d, Decode: %d, Displyable: %d", len, rlt, m_Output.bDisplay);
	if(m_Output.bDisplay) {
		if(m_Output.width != m_width || m_Output.height != m_height) {
			if(m_pColors) {
				m_env->ReleaseIntArrayElements(m_ColorArray, m_pColors, 0);
				m_pColors = NULL;
			}
			//Call java method to (re)alloc space
			jboolean b = m_env->CallBooleanMethod(m_obj, m_jmthds.midDimensionChanged, m_Output.width, m_Output.height);
			m_width = m_Output.width;
			m_height = m_Output.height;
			m_ColorArray = (jintArray)m_env->CallObjectMethod(m_obj, m_jmthds.midGetColorBuffer);
		    
			if(m_ColorArray) {
				jboolean isCopy;
				m_pColors = m_env->GetIntArrayElements(m_ColorArray, &isCopy);
			}
		}

		//void YUV420_to_RGB565(unsigned char *y, unsigned char *u, unsigned char *v, int width, int height, int src_ystride, int src_uvstride, unsigned int *pdst, int dst_ystride);
		//void UYVY2RGB24(const unsigned char *psrc, int srcPitch, int width, int height, unsigned char *pdst, int dstPitch);
		#if 0
		YUV420_to_RGB32(m_Output.pY, m_Output.pU, m_Output.pV, 
				m_Output.width, m_Output.height, m_Output.uYStride, m_Output.uUVStride,
				m_pColors, m_Output.width/*<<2*/);
		#else
		YUV420_to_RGB565(m_Output.pY, m_Output.pU, m_Output.pV, 
				m_Output.width, m_Output.height, m_Output.uYStride, m_Output.uUVStride,
				(unsigned int*)m_pColors, m_Output.width);
		#endif

		/* Notify the SurfaceView thread to draw on canvas */
		m_env->CallVoidMethod(m_obj, m_jmthds.midFrameReady);
	}
	return 0;
}

int JniPlayer::DecodeAndPlayAudio(MEDIATYPE mt, BYTE *pData, UINT len)
{
	if(!m_AudioArray)	{
		m_AudioArray = (jbyteArray)m_env->CallObjectMethod(m_obj, m_jmthds.midAllocateAudioBuffer);
		m_AudioCodec.AudioDecReset(mt);
		if(m_AudioArray) {
			jboolean isCopy;	//must no copy
			m_pPCMAudio = m_env->GetByteArrayElements(m_AudioArray, &isCopy);
		}
	}
	if (m_AudioArray != NULL && m_pPCMAudio != NULL){
		int len_pcm = m_AudioCodec.AudioDecode(pData, len, (BYTE*)m_pPCMAudio);
		if(len_pcm > 0){
			m_env->CallVoidMethod(m_obj, m_jmthds.midAudioReady, len_pcm);
		}
	}
	return 0;
}

void JniPlayer::stopAudio()
{
	m_AudioArray = NULL;
	m_pPCMAudio = NULL;
}

void JniPlayer::OnProgress(int thousandth, int milliseconds)
{
	m_env->CallVoidMethod(m_obj, m_jmthds.midOnProgress, thousandth, milliseconds);
}
#else
JniPlayer::JniPlayer():m_nVideoFrame(0), m_nAudioFrame(0), m_piObjs(NULL) {}

int JniPlayer::InitJniPlayer(JNIEnv *env, /*IN*/jobject obj/*interface ColorArray*/) { if(obj) m_piObjs = (int*)obj; return 0; }
void JniPlayer::DeinitJniPlayer() {}
	
BOOL JniPlayer::ThreadBeginning() { return TRUE; }
void JniPlayer::ThreadEnding() {}

int JniPlayer::DecodeAndPlayVideo(BYTE *pData, UINT len) 
{ 
	if(m_piObjs) m_piObjs[0]++;
	m_nVideoFrame++; 
	//usleep(500000);
	return 0; 
}

int JniPlayer::DecodeAndPlayAudio(MEDIATYPE mt, BYTE *pAudio, UINT len) { if(m_piObjs) m_piObjs[1]++; return 0; }
void JniPlayer::stopAudio() {}
void JniPlayer::OnProgress(int thousandth, int milliseconds) {}
#endif

