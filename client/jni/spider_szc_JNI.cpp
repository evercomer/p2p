#include "spider_szc_JNI.h"
#include "cltsess.h"
#include "cltcmdwrapper.h"
#include "mediatyp.h"
#include "misc.h"
#include "EnumDevice.h"
#include "jniplayer.h"
#include "p2psess.h"
#include "YUV2RGB.h"
#include "apperr.h"
#include "FileDecoder.h"
#include "AudioCodec.h"
#include "mp4rwer.h"
#include "rtsp/rtspsvc.h"

#ifdef __ANDROID__

char *copyJStringToNative(char *native, unsigned int size, JNIEnv *env, jstring jstr)
{
	const char *str;
	str = env->GetStringUTFChars(jstr, NULL);
	if(!str)
		return NULL;
	strncpyz(native, (const char*)str, size);
	env->ReleaseStringUTFChars(jstr, str);
	return native;
}

/*
 * Class:     spider_szc_JNI
 * Method:    init
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_init(JNIEnv *env, jclass, jstring server1, jstring server2)
{
	char s1[32], s2[32] = "";
	copyJStringToNative(s1, sizeof(s1), env, server1);
	copyJStringToNative(s2, sizeof(s2), env, server2);
	//int err = DAP2pClientInit(s1, s2[0]?s2:NULL, NULL, NULL);
	int err = DAP2pClientInit(s1,NULL, NULL, NULL);
	CreateYUVTab();	
	LaunchRtspService();
	return err;
}

/*
 * Class:     spider_szc_JNI
 * Method:    uninit
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_uninit(JNIEnv *, jclass)
{
	ExitRtspService();
	DAP2pClientDeinit();
	//DeleteYUVTab();
}

/*
 * Class:     spider_szc_JNI
 * Method:    enumDevices
 * Signature: ()[Lspider/szc/JNI/Device;
 */
JNIEXPORT jobjectArray JNICALL Java_spider_szc_JNI_enumDevices(JNIEnv *env, jclass)
{
	jclass clsEd;
	jfieldID fidName, fidSN, fidIp, fidPort,fidMac,fidNetmask,fidMdns,fidSdns,fidDdnsuser,fidDdnsp,fidDdn,fidGw,fidVersion,fidDhcpen; //fidAutoDns;
	jmethodID construct;
	ENUMDEVICE *pEd;
	UINT nEd;
	
	clsEd = env->FindClass("spider/szc/JNI$Device");
	fidName = env->GetFieldID(clsEd, "name", "Ljava/lang/String;");
	fidSN = env->GetFieldID(clsEd, "sn", "Ljava/lang/String;");
	fidIp = env->GetFieldID(clsEd, "ip", "Ljava/lang/String;");
	fidPort = env->GetFieldID(clsEd, "port", "I");
	fidMac = env->GetFieldID(clsEd, "mac", "Ljava/lang/String;");
	fidNetmask = env->GetFieldID(clsEd, "netmask", "Ljava/lang/String;");
	fidMdns = env->GetFieldID(clsEd, "mdns", "Ljava/lang/String;");
	fidSdns = env->GetFieldID(clsEd, "sdns", "Ljava/lang/String;");
	fidDdnsuser = env->GetFieldID(clsEd, "ddnsuser", "Ljava/lang/String;");
	fidDdnsp = env->GetFieldID(clsEd, "ddnsp", "Ljava/lang/String;");
	fidDdn = env->GetFieldID(clsEd, "ddn", "Ljava/lang/String;");
	fidGw = env->GetFieldID(clsEd, "gw", "Ljava/lang/String;");
	fidVersion = env->GetFieldID(clsEd, "version", "Ljava/lang/String;");
	fidDhcpen = env->GetFieldID(clsEd, "dhcpen", "I");
	construct = env->GetMethodID(clsEd,"<init>","()V");

	if(EnumDevices(&pEd, &nEd, NULL)&& nEd)
	{	
		int ok = 0;
		jobjectArray result = env->NewObjectArray(nEd, clsEd, NULL);
		if(result) {
			int i;
			for(i=0; i<nEd; i++) {
				jobject obj = env->NewObject(clsEd,construct);
				jstring str = env->NewStringUTF(pEd[i].cDevName);
				env->SetObjectField(obj, fidName, str);

				str = env->NewStringUTF(pEd[i].cSN);
				env->SetObjectField(obj, fidSN, str);

				str = env->NewStringUTF(pEd[i].cIp);
				env->SetObjectField(obj, fidIp, str);

				env->SetIntField(obj, fidPort, pEd[i].iPortCtp);

				str = env->NewStringUTF(pEd[i].cMac);
				env->SetObjectField(obj, fidMac, str);

				str = env->NewStringUTF(pEd[i].cNetMask);
				env->SetObjectField(obj, fidNetmask, str);

				str = env->NewStringUTF(pEd[i].cMDns);
				env->SetObjectField(obj, fidMdns, str);

				str = env->NewStringUTF(pEd[i].cSDns);
				env->SetObjectField(obj, fidSdns, str);

				str = env->NewStringUTF(pEd[i].cDdnsUser);
				env->SetObjectField(obj, fidDdnsuser, str);

				str = env->NewStringUTF(pEd[i].cDdnsSvr);
				env->SetObjectField(obj, fidDdnsp, str);

				str = env->NewStringUTF(pEd[i].cDdn);
				env->SetObjectField(obj, fidDdn, str);

				str = env->NewStringUTF(pEd[i].cGateway);
				env->SetObjectField(obj, fidGw, str);

				str = env->NewStringUTF(pEd[i].cVersion);
				env->SetObjectField(obj, fidVersion, str);

				env->SetIntField(obj, fidDhcpen, pEd[i].bDhcp);
				env->SetObjectArrayElement(result, i, obj);
			}
			ok = (i >= nEd);
		}
		free(pEd);
		return ok?result:NULL;
	}
	LOGW("EnumDevice failed\n");
	return NULL;
}


struct LanDeviceNetInfoClass{
	jclass clazz;
	jfieldID isDhcp, ip, netmask, gateway, mDns, sDns;
};

struct LanDeviceNetInfoClass getLanDeviceNetInfoClassClass(JNIEnv *env, jobject obj)
{
	struct LanDeviceNetInfoClass clazz;
	clazz.clazz = env->GetObjectClass(obj);
	clazz.isDhcp = env->GetFieldID(clazz.clazz, "isDhcp", "I");
	clazz.ip = env->GetFieldID(clazz.clazz, "ip", "Ljava/lang/String;");
	clazz.netmask = env->GetFieldID(clazz.clazz, "netmask", "Ljava/lang/String;");
	clazz.gateway = env->GetFieldID(clazz.clazz, "gateway", "Ljava/lang/String;");
	clazz.mDns = env->GetFieldID(clazz.clazz, "mDns", "Ljava/lang/String;");
	clazz.sDns = env->GetFieldID(clazz.clazz, "sDns", "Ljava/lang/String;");
	return clazz;
}

/*
 * Class:     spider_szc_JNI
 * Method:    setLanDeviceNet
 * Signature: (Ljava/lang/String;Lspider/szc/JNI/LanDeviceNet;)Z
 */
JNIEXPORT jboolean JNICALL Java_spider_szc_JNI_setLanDeviceNet(JNIEnv *env, jclass jclazz, jstring sn, jobject deviceNet){

	struct LanDeviceNetInfoClass netInfoClass;
	DEVICENET *deviceNetData = (DEVICENET *)malloc(sizeof(DEVICENET));
	
	netInfoClass = getLanDeviceNetInfoClassClass(env, deviceNet);
	deviceNetData->dhcp = env->GetIntField(deviceNet, netInfoClass.isDhcp);

	jstring str = (jstring)env->GetObjectField(deviceNet, netInfoClass.ip);
	const char *str_ip = env->GetStringUTFChars(str, NULL);
	strcpy(deviceNetData->ip, str_ip);
	env->ReleaseStringUTFChars(str, str_ip);

	str = (jstring)env->GetObjectField(deviceNet, netInfoClass.netmask);
	const char *str_netmask = env->GetStringUTFChars(str, NULL);
	strcpy(deviceNetData->cNetMask, str_netmask);
	env->ReleaseStringUTFChars(str, str_netmask);

	str = (jstring)env->GetObjectField(deviceNet, netInfoClass.gateway);
	const char *str_gateway = env->GetStringUTFChars(str, NULL);
	strcpy(deviceNetData->cGateway, str_gateway);
	env->ReleaseStringUTFChars(str, str_gateway);

	str = (jstring)env->GetObjectField(deviceNet, netInfoClass.mDns);
	const char *str_mDns = env->GetStringUTFChars(str, NULL);
	strcpy(deviceNetData->cMDns, str_mDns);
	env->ReleaseStringUTFChars(str, str_mDns);

	str = (jstring)env->GetObjectField(deviceNet, netInfoClass.sDns);
	const char *str_sDns = env->GetStringUTFChars(str, NULL);
	strcpy(deviceNetData->cSDns, str_sDns);
	env->ReleaseStringUTFChars(str, str_sDns);

	const char *str_sn = env->GetStringUTFChars(sn, NULL);
	BOOL ret = SetCameraNet((char*)str_sn, deviceNetData);
	env->ReleaseStringUTFChars(sn, str_sn);

	return ret;
}

/*
 * Class:     spider_szc_JNI
 * Method:    connType
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_connType(JNIEnv *env, jclass,jstring server, jstring sn, jstring user, jstring pswd,jint netType)
{
	char _svr[48],_sn[32], _user[20], _pswd[20];
	const jbyte *str;
#if 1
	if(!copyJStringToNative(_svr, sizeof(_svr), env, server))
		return -1;
	if(!copyJStringToNative(_sn, sizeof(_sn), env, sn))
		return -1;
	if(!copyJStringToNative(_user, sizeof(_user), env, user))
		return -1;
	if(!copyJStringToNative(_pswd, sizeof(_pswd), env, pswd))
		return -1;
#else
	strcpy(_svr, "211.154.137.24");
	strcpy(_sn, "TV200000159");
	strcpy(_user, "admin");
	strcpy(_pswd, "admin");
#endif

	int err;
	P2pSession *pSess = new P2pSession();
	err = pSess->StartDetectType(_svr,_sn, _user, _pswd,netType);
	if(err)
	{
		delete pSess;
		return err;
	}
		
	return (jint)pSess;

}
/*
 * Class:     spider_szc_JNI
 * Method:    createConn
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_createConn(JNIEnv *env, jclass, jstring server,jstring sn, jstring user, jstring pswd)
{
	char _svr[48],_sn[32], _user[20], _pswd[20];
	const jbyte *str;
#if 1
	if(!copyJStringToNative(_svr, sizeof(_svr), env, server))
		return -1;
	if(!copyJStringToNative(_sn, sizeof(_sn), env, sn))
		return -1;
	if(!copyJStringToNative(_user, sizeof(_user), env, user))
		return -1;
	if(!copyJStringToNative(_pswd, sizeof(_pswd), env, pswd))
		return -1;
#else
	strcpy(_svr, "211.154.137.24");
	strcpy(_sn, "TV200000159");
	strcpy(_user, "admin");
	strcpy(_pswd, "admin");
#endif

	int err;
	P2pSession *pSess = new P2pSession();
	err = pSess->StartSession(_svr,_sn, _user, _pswd);
	if(err)
	{
		delete pSess;
		return err;
	}
		
	return (jint)pSess;
}
/*
 * Class:     spider_szc_JNI
 * Method:    NewcreateConn
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_NewcreateConn(JNIEnv *env, jclass, jstring server,jstring sn, jstring user, jstring pswd, jint conntype)
{
	char _svr[64],_sn[32], _user[32], _pswd[32];
	const jbyte *str;
#if 1
	if(!copyJStringToNative(_svr, sizeof(_svr), env, server))
		return -1;
	if(!copyJStringToNative(_sn, sizeof(_sn), env, sn))
		return -1;
	if(!copyJStringToNative(_user, sizeof(_user), env, user))
		return -1;
	if(!copyJStringToNative(_pswd, sizeof(_pswd), env, pswd))
		return -1;
#else
	strcpy(_svr, "211.154.137.24");
	strcpy(_sn, "TV200000159");
	strcpy(_user, "admin");
	strcpy(_pswd, "admin");
#endif

	int err;
	P2pSession *pSess = new P2pSession();
	err = pSess->NewStartSession(_svr,_sn, _user, _pswd, conntype);

	if(err)
	{
		delete pSess;
		return err;
	}
		
	return (jint)pSess;
}
/*
 * Class:     spider_szc_JNI
 * Method:    destroyConn
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_destroyConn(JNIEnv *env, jclass, jint _pSess)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	if(pSess)
	{
	    pSess->StopSession();
		delete pSess;
	}
	return 0;
}

/*
 * Class:     spider_szc_JNI
 * Method:    getConnInfo
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_getConnInfo(JNIEnv *env, jclass, jint _pSess)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	P2PCONNINFO ci;
	if(DAP2pConnGetInfo(pSess->GetConnectionHandle(), &ci) == 0)
	{
		return ci.ct;
	}
	return -1;
}

#define LIVE_VIDEO_CHNO	0
#define LIVE_AUDIO_CHNO	1
#define PLAYBACK_VIDEO_CHNO	2
#define PLAYBACK_AUDIO_CHNO	3


/*
 * Class:     spider_szc_JNI
 * Method:    startSession
 * Signature: (IILspider/szc/ColorArray;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_startLiveVideo(JNIEnv *env, jclass, jint _pSess, jint quality, jint vstrm, jint channel, jobject obj/*ColorArray*/, int flags)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	return pSess->StartLiveVideo(env, obj, (VIDEOQUALITY)quality, vstrm, channel, flags);
}


/*
 * Class:     spider_szc_JNI
 * Method:    stopSession
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_stopLiveVideo(JNIEnv *, jclass, jint _pSess)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	pSess->StopLiveVideo();
}

/*
 * Class:     spider_szc_JNI
 * Method:    vocalize
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_startLiveAudio(JNIEnv *, jclass, jint _pSess)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	return pSess->Vocalize();
}

/*
 * Class:     spider_szc_JNI
 * Method:    mute
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_stopLiveAudio(JNIEnv *, jclass, jint _pSess)
{	
	P2pSession *pSess = (P2pSession*)_pSess;
	pSess->Mute();
}

/*
 * Class:     spider_szc_JNI
 * Method:    setEventCallback
 * Signature: (Lspider/szc/EventCallback;)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_setEventCallback(JNIEnv *env, jclass, jobject obj)
{
	jmethodID s_midEventCB = 0;
	if(!s_midEventCB) {
		//JavaVM *jvm;
		//env->GetJavaVM(&jvm);
		jclass cls = env->GetObjectClass(obj);
		s_midEventCB = env->GetMethodID(cls, "onEventCB", "(II)V");
	}
}

/*
 * Class:     spider_szc_JNI
 * Method:    ptzControl
 * Signature: (IIII)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_ptzControl(JNIEnv *, jclass, jint  _pSess, jint act, jint param1, jint param2)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	pSess->PtzControl(act, param1, param2);
}

/*
 * Class:     tastech_camview_JNI
 * Method:    startRecord
 * Signature: (ILjava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_startRecord
  (JNIEnv *env, jclass, jint _pSess, jstring jstrFileName, jstring jstrCamName)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	const char *fname, *cname;
	fname = env->GetStringUTFChars(jstrFileName, NULL);
	if(!fname)
		return E_INVALID_PARAM;

	cname = env->GetStringUTFChars(jstrCamName, NULL);
	if(!cname) {
		env->ReleaseStringUTFChars(jstrFileName, fname);
		return E_INVALID_PARAM;
	}
	int rlt = pSess->StartRecord(fname, cname);
	env->ReleaseStringUTFChars(jstrFileName, fname);
	if(cname) env->ReleaseStringUTFChars(jstrCamName, cname);
	return rlt;
}

/*
 * Class:     tastech_camview_JNI
 * Method:    stopRecord
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_stopRecord
  (JNIEnv *, jclass, jint _pSess)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	pSess->StopRecord();
}



/*
 * Class:     spider_szc_JNI
 * Method:    startPlayFile
 * Signature: (ILjava/lang/String;Lspider/szc/ColorArray;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_startPlayFile(JNIEnv *env, jclass, jint _pSess, jstring filepath, jobject obj)
{
	char path[256];
	CFileDecoder *pFD = new CFileDecoder();
	copyJStringToNative(path, sizeof(path), env, filepath);
	if(pFD->Initialize(env, obj, path, strcasecmp(path+strlen(path)-5, ".v264")==0?GetDefaultReader():GetMP4Reader()) == 0) {
		pFD->Start();
		return (jint)pFD;
	}
	else {
		delete pFD;
		return 0;
	}
}

/*
 * Class:     spider_szc_JNI
 * Method:    stopPlayFile
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_stopPlayFile(JNIEnv *, jclass, jint handle)
{
	CFileDecoder *pFD = (CFileDecoder*)handle;
	if(pFD) {
		pFD->Stop();
		delete pFD;
	}
}

/*
 * Class:     spider_szc_JNI
 * Method:    pausePlayFile
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_pausePlayFile(JNIEnv *, jclass, jint handle)
{
	CFileDecoder *pFD = (CFileDecoder*)handle;
	if(pFD)
		pFD->Pause();
}

/*
 * Class:     spider_szc_JNI
 * Method:    continuePlayFile
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_continuePlayFile(JNIEnv *, jclass, jint handle)
{
	CFileDecoder *pFD = (CFileDecoder*)handle;
	if(pFD)
		pFD->Resume();
}

/*
 * Class:     spider_szc_JNI
 * Method:    setPlaybackRate
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_setPlaybackRate(JNIEnv *, jclass, jint handle, jint irate)
{
	CFileDecoder *pFD = (CFileDecoder*)handle;
	if(pFD)
		pFD->SetXRate(irate);
}

/*
 * Class:     spider_szc_JNI
 * Method:    seekPlayback
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_spider_szc_JNI_seekPlayback(JNIEnv *, jclass, jint handle, jint millisecond)
{
	CFileDecoder *pFD = (CFileDecoder*)handle;
	if(pFD)
		pFD->LocateTimestamp(millisecond);
}


//---------------------------------------------------------------------------

JNIEXPORT jint Java_spider_szc_JNI_getRotation(JNIEnv *, jclass, jint _pSess, int vchn)
{
	P2pSession *pSess = (P2pSession*)_pSess;

	int rot, err;
	err = DAP2pCmdGetRotation(pSess->GetConnectionHandle(), vchn, &rot);
	if(err < 0) return err;
	return rot;
}

JNIEXPORT jint Java_spider_szc_JNI_setRotation(JNIEnv *, jclass, jint _pSess, int vchn, jint rotation)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	return DAP2pCmdSetRotation(pSess->GetConnectionHandle(), vchn, rotation);

}

JNIEXPORT jint Java_spider_szc_JNI_getPowerFreq(JNIEnv *, jclass, jint _pSess)
{
	P2pSession *pSess = (P2pSession*)_pSess;

	int freq, err;
	err = DAP2pCmdGetPowerFreq(pSess->GetConnectionHandle(), &freq);
	if(err < 0) return err;
	return freq;
}

JNIEXPORT jint Java_spider_szc_JNI_setPowerFreq(JNIEnv *, jclass, jint _pSess, jint freq)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	struct CmdSetPowerFreqReq req;
	req.vchn = 0;
	req.freq = freq;
	return DAP2pCmdSetPowerFreq(pSess->GetConnectionHandle(), &req);
}

struct WifiApIntf {
	jclass clsId;
	jfieldID fidEssid, fidEncType, fidQuality;
};
static BOOL _wifiApIntf(JNIEnv *env, struct WifiApIntf *pIntf)
{
	if(!(pIntf->clsId = env->FindClass("spider/szc/JNI$WifiAp"))) return FALSE;
	if(!(pIntf->fidEssid = env->GetFieldID(pIntf->clsId, "essid", "Ljava/lang/String;"))) return FALSE;
	if(!(pIntf->fidQuality = env->GetFieldID(pIntf->clsId, "quality", "I"))) return FALSE;
	if(!(pIntf->fidEncType = env->GetFieldID(pIntf->clsId, "encType", "I"))) return FALSE;
	return TRUE;
}

JNIEXPORT jobjectArray Java_spider_szc_JNI_listWifiAp(JNIEnv *env, jclass, jint _pSess)
{
	struct WifiApIntf intf;
	_wifiApIntf(env, &intf);
	P2pSession *pSess = (P2pSession*)_pSess;
	struct CmdListWifiApResp *pResp = (struct CmdListWifiApResp*)malloc(1024);
	UINT nBuff = 1024;
	int err = DAP2pCmdListWifiAp(pSess->GetConnectionHandle(), pResp, &nBuff);
	jobjectArray result = NULL;
	if(err == 0)
	{
		int i;
		result = env->NewObjectArray(pResp->nAP, intf.clsId, NULL);
		jmethodID cid = env->GetMethodID(intf.clsId, "<init>", "()V");
		for(i=0; i<pResp->nAP; i++)
		{
			jobject obj = env->NewObject(intf.clsId, cid);
			env->SetObjectField(obj, intf.fidEssid, env->NewStringUTF(pResp->aps[i].essid));
			env->SetIntField(obj, intf.fidEncType, pResp->aps[i].enctype);
			env->SetIntField(obj, intf.fidQuality, pResp->aps[i].quality);
			env->SetObjectArrayElement(result, i, obj);
		}
	}
	free(pResp);
	return result;
}

struct WifiCfgIntf {
	jclass clsId;
	jfieldID fidEssid, fidEncType, fidPassword;
};
static BOOL _wifiCfgIntf(JNIEnv *env, struct WifiCfgIntf *pIntf)
{
	if(!(pIntf->clsId = env->FindClass("spider/szc/JNI$WifiCfg"))) return FALSE;
	if(!(pIntf->fidEssid = env->GetFieldID(pIntf->clsId, "essid", "Ljava/lang/String;"))) return FALSE;
	if(!(pIntf->fidEncType = env->GetFieldID(pIntf->clsId, "encType", "I"))) return FALSE;
	if(!(pIntf->fidPassword = env->GetFieldID(pIntf->clsId, "password", "Ljava/lang/String;"))) return FALSE;
	return TRUE;	
}
JNIEXPORT jobject Java_spider_szc_JNI_getWifi(JNIEnv *env, jclass, jint _pSess)
{
	struct CmdGetWifiResp wifi;
	struct WifiCfgIntf intf;
	P2pSession *pSess = (P2pSession*)_pSess;
	_wifiCfgIntf(env, &intf);
	if(DAP2pCmdGetWifi(pSess->GetConnectionHandle(), &wifi) == 0)
	{
		jmethodID cid = env->GetMethodID(intf.clsId, "<init>", "()V");
		jobject obj = env->NewObject(intf.clsId, cid);
		env->SetObjectField(obj, intf.fidEssid, env->NewStringUTF(wifi.essid));
		env->SetIntField(obj, intf.fidEncType, wifi.enctype);
		env->SetObjectField(obj, intf.fidPassword, env->NewStringUTF(wifi.key));
		return obj;
	}
	return NULL;
}

JNIEXPORT jint Java_spider_szc_JNI_setWifi(JNIEnv *env, jclass, jint _pSess, jobject cfg)
{
	struct CmdSetWifiReq wifi;
	struct WifiCfgIntf intf;
	P2pSession *pSess = (P2pSession*)_pSess;
	_wifiCfgIntf(env, &intf);

	copyJStringToNative(wifi.essid, sizeof(wifi.essid), env, (jstring)env->GetObjectField(cfg, intf.fidEssid));
	wifi.enctype = (WIFI_ENCTYPE)env->GetIntField(cfg, intf.fidEncType);
	copyJStringToNative(wifi.key, sizeof(wifi.key), env, (jstring)env->GetObjectField(cfg, intf.fidPassword));

	return DAP2pCmdSetWifi(pSess->GetConnectionHandle(), &wifi);
}

struct VideoColorIntf {
	jclass clsId;
	jfieldID fidBrightness, fidContrast, fidSaturation, fidHue;
};
static BOOL _videoColorIntf(JNIEnv *env, struct VideoColorIntf *pIntf)
{
	if(!(pIntf->clsId = env->FindClass("spider/szc/JNI$VideoColor"))) return FALSE;
	if(!(pIntf->fidBrightness = env->GetFieldID(pIntf->clsId, "brightness", "I"))) return FALSE;
	if(!(pIntf->fidContrast = env->GetFieldID(pIntf->clsId, "contrast", "I"))) return FALSE;
	if(!(pIntf->fidSaturation = env->GetFieldID(pIntf->clsId, "saturation", "I"))) return FALSE;	
	if(!(pIntf->fidHue = env->GetFieldID(pIntf->clsId, "hue", "I"))) return FALSE;
	return TRUE;
}
JNIEXPORT jobject Java_spider_szc_JNI_getVideoColor(JNIEnv *env, jclass, jint _pSess, jint vchn)
{
	int err;
	struct dcs_chnvideo cv;
	struct dcs_video_color clr;
	struct VideoColorIntf intf;
	P2pSession *pSess = (P2pSession*)_pSess;
	if(!_videoColorIntf(env, &intf)) return NULL;
	cv.channel = vchn;
	cv.video = 0;

	if( (err = DAP2pCmdGetVideoColor(pSess->GetConnectionHandle(), &cv, &clr)) == 0)
	{
		jmethodID cid = env->GetMethodID(intf.clsId, "<init>", "()V");

		jobject obj = env->NewObject(intf.clsId, cid);
		if(!obj) return NULL;

		env->SetIntField(obj, intf.fidBrightness, clr.brightness);
		env->SetIntField(obj, intf.fidContrast, clr.contrast);
		env->SetIntField(obj, intf.fidSaturation, clr.saturation);
		env->SetIntField(obj, intf.fidHue, clr.hue);

		return obj;
	}
	return NULL;
}

JNIEXPORT jint Java_spider_szc_JNI_setVideoColor(JNIEnv *env, jclass, jint _pSess, jint vchn, jobject color)
{
	struct CmdSetVideoColorReq req;
	struct VideoColorIntf intf;
	P2pSession *pSess = (P2pSession*)_pSess;
	_videoColorIntf(env, &intf);
	req.chnvid.channel = vchn;
	req.chnvid.video = 0;
	req.color.brightness = env->GetIntField(color, intf.fidBrightness);
	req.color.contrast = env->GetIntField(color, intf.fidContrast);
	req.color.saturation = env->GetIntField(color, intf.fidSaturation);
	req.color.hue = env->GetIntField(color, intf.fidHue);

	return DAP2pCmdSetVideoColor(pSess->GetConnectionHandle(), &req);
}

struct VideoEncParamIntf {
	jclass clsId;
	jfieldID fidRes, fidFps, fidKbps, fidIGops, fidResMask;
};
static BOOL _videoEncParamIntf(JNIEnv *env, struct VideoEncParamIntf *pIntf)
{
	if(!(pIntf->clsId = env->FindClass("spider/szc/JNI$VideoEncParam"))) return FALSE;
	if(!(pIntf->fidRes = env->GetFieldID(pIntf->clsId, "resolution", "I"))) return FALSE;
	if(!(pIntf->fidFps = env->GetFieldID(pIntf->clsId, "fps", "I"))) return FALSE;
	if(!(pIntf->fidKbps = env->GetFieldID(pIntf->clsId, "kbps", "I"))) return FALSE;
	if(!(pIntf->fidIGops = env->GetFieldID(pIntf->clsId, "i_gops", "I"))) return FALSE;
	if(!(pIntf->fidResMask = env->GetFieldID(pIntf->clsId, "supported_res", "I"))) return FALSE;
	return TRUE;
}

JNIEXPORT jobject Java_spider_szc_JNI_getVideoEncParam(JNIEnv *env, jclass, jint _pSess, jint vchn, jint vstream)
{
	struct dcs_chnvideo cv;
	struct dcs_video_param param;
	struct VideoEncParamIntf intf;
	int err;
	
	P2pSession *pSess = (P2pSession*)_pSess;
	if(!_videoEncParamIntf(env, &intf)) return NULL;
	cv.channel = vchn;
	cv.video = vstream;

	if( (err = DAP2pCmdGetVideoParam(pSess->GetConnectionHandle(), &cv, &param)) == 0)
	{

		jmethodID cid = env->GetMethodID(intf.clsId, "<init>", "()V");

		jobject obj = env->NewObject(intf.clsId, cid);

		if(!obj) return NULL;

		env->SetIntField(obj, intf.fidRes, param.res);
		env->SetIntField(obj, intf.fidFps, param.fps);
		env->SetIntField(obj, intf.fidKbps, param.kbps);
		env->SetIntField(obj, intf.fidIGops, param.gop);
		env->SetIntField(obj, intf.fidResMask, param.supported_res);
		return obj;
	}
	return NULL;
}

JNIEXPORT jint Java_spider_szc_JNI_setVideoEncParam(JNIEnv *env, jclass, jint _pSess, jint vchn, jint vstream, jobject param)
{
	struct CmdSetVideoParameterReq req;
	struct VideoEncParamIntf intf;
	
	P2pSession *pSess = (P2pSession*)_pSess;
	_videoEncParamIntf(env, &intf);
	
	req.chnvid.channel = vchn;
	req.chnvid.video = vstream;
	req.video_param.res = env->GetIntField(param, intf.fidRes);
	req.video_param.fps = env->GetIntField(param, intf.fidFps);
	req.video_param.kbps = env->GetIntField(param, intf.fidKbps);
	req.video_param.gop = env->GetIntField(param, intf.fidIGops);
	return DAP2pCmdSetVideoParam(pSess->GetConnectionHandle(), &req);
}

JNIEXPORT jint Java_spider_szc_JNI_changePassword(JNIEnv *env, jclass, jint _pSess, jstring newPassword)
{
	char pswd[32];
	P2pSession *pSess = (P2pSession*)_pSess;
	copyJStringToNative(pswd, sizeof(pswd), env, newPassword);
	return DAP2pCmdChangePassword(pSess->GetConnectionHandle(), pswd);
}

JNIEXPORT jint Java_spider_szc_JNI_PlayBackRecord(JNIEnv *, jclass,jint _pSess, jobject para)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	return pSess->LocateAndPlay((dcs_datetime*)para);

}

JNIEXPORT jint Java_spider_szc_JNI_initTalk(JNIEnv *env, jclass, jint _pSess)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	return pSess->InitTalkBack();
}

JNIEXPORT jint Java_spider_szc_JNI_sendTalk(JNIEnv *env, jclass, jint _pSess, jbyteArray in_buff, jint len)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	int audioType = pSess->GetTalkBackAudioType();
	if (audioType >= MEDIATYPE_AUDIO_G711A && audioType < MEDIATYPE_AUDIO_MAX){
		jbyte *jbarray = (jbyte *)malloc(len * sizeof(jbyte));
		env->GetByteArrayRegion(in_buff, 0, len, jbarray);
		BYTE *in_buf = (BYTE*)jbarray;
		BYTE *out_buf  = (BYTE*)malloc((len+1) * sizeof(BYTE));
		CAudioCodec m_audio;
		m_audio.AudioEncReset((MEDIATYPE)audioType);
		out_buf[0] = audioType;
		int buff_len = m_audio.AudioEncode((BYTE*)in_buf, len, (BYTE *)(out_buf+1));
		int ret = pSess->TalkBack((BYTE *)out_buf, buff_len+1);
		free(in_buf);
		free(out_buf);
		return ret;
	}else{
		return -1;
	}
}

JNIEXPORT jint Java_spider_szc_JNI_stopTalk(JNIEnv *env, jclass, jint _pSess)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	pSess->StopTalkBack();
 	return 0;
}

struct BaseInfoIntf {
	jclass clsId;
	jfieldID fiddaID, fidversionApi, fidmainuser,fidsSn,fidmac,fidname,fidversion,fidtype,fidchnum,fidAllSize,fidFreeSize;	

};
static BOOL _GetBaseInfoIntf(JNIEnv *env, struct BaseInfoIntf*pIntf)
{
	if(!(pIntf->clsId = env->FindClass("spider/szc/JNI$BaseInfo"))) return FALSE;
	if(!(pIntf->fiddaID = env->GetFieldID(pIntf->clsId, "daID", "Ljava/lang/String;"))) return FALSE;
	if(!(pIntf->fidversionApi = env->GetFieldID(pIntf->clsId, "versionApi", "Ljava/lang/String;"))) return FALSE;
	if(!(pIntf->fidmainuser = env->GetFieldID(pIntf->clsId, "mainuser", "Ljava/lang/String;"))) return FALSE;
	if(!(pIntf->fidsSn = env->GetFieldID(pIntf->clsId, "sSn", "Ljava/lang/String;"))) return FALSE;
	if(!(pIntf->fidmac = env->GetFieldID(pIntf->clsId, "mac", "Ljava/lang/String;"))) return FALSE;
	if(!(pIntf->fidname = env->GetFieldID(pIntf->clsId, "name", "Ljava/lang/String;"))) return FALSE;
	if(!(pIntf->fidversion = env->GetFieldID(pIntf->clsId, "version", "Ljava/lang/String;"))) return FALSE;
	if(!(pIntf->fidtype = env->GetFieldID(pIntf->clsId, "type", "I"))) return FALSE;
	if(!(pIntf->fidchnum = env->GetFieldID(pIntf->clsId, "chnum", "I"))) return FALSE;
	if(!(pIntf->fidAllSize = env->GetFieldID(pIntf->clsId, "AllSize", "I"))) return FALSE;
	if(!(pIntf->fidFreeSize = env->GetFieldID(pIntf->clsId, "FreeSize", "I"))) return FALSE;
	return TRUE;	
}

JNIEXPORT jobject Java_spider_szc_JNI_GetBaseInfo(JNIEnv *env, jclass, jint _pSess)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	struct CmdGetBaseInfo resp;
	struct BaseInfoIntf intf;

	if(!_GetBaseInfoIntf(env,&intf)) return FALSE;
	if(DAP2pCmdGetBaseInfo(pSess->GetConnectionHandle(),&resp)==0)
	{
		jmethodID cid = env->GetMethodID(intf.clsId, "<init>", "()V");
		jobject obj = env->NewObject(intf.clsId, cid);
		env->SetObjectField(obj, intf.fiddaID, env->NewStringUTF(resp.daID));
		env->SetObjectField(obj, intf.fidversionApi,env->NewStringUTF(resp.versionApi));
		env->SetObjectField(obj,intf.fidmainuser,env->NewStringUTF(resp.mainuser));
		env->SetObjectField(obj,intf.fidsSn,env->NewStringUTF(resp.sSn));
		env->SetObjectField(obj,intf.fidmac,env->NewStringUTF(resp.mac));
		env->SetObjectField(obj,intf.fidname,env->NewStringUTF(resp.name));
		env->SetObjectField(obj,intf.fidversion,env->NewStringUTF(resp.version));
		env->SetIntField(obj,intf.fidtype,resp.type);
		env->SetIntField(obj,intf.fidchnum,resp.chnum);
		env->SetIntField(obj,intf.fidAllSize,resp.AllSize);
		env->SetIntField(obj,intf.fidFreeSize,resp.FreeSize);
		return obj;		
	}
	return NULL;
}


//videoparamNew
struct videoparamIntf{
	jclass clsId;
	jfieldID fidres,fidquality,fidfps,fidbrmode,fidsave,fidkbps,fidgop,fidmax_fps;
};

static BOOL _VideoParaIntf(JNIEnv *env, struct videoparamIntf *pIntf)
{
	if(!(pIntf->clsId = env->FindClass("spider/szc/JNI$VideoParam"))) return FALSE;
	if(!(pIntf->fidres = env->GetFieldID(pIntf->clsId, "res", "I"))) return FALSE;
	if(!(pIntf->fidquality = env->GetFieldID(pIntf->clsId, "quality", "I"))) return FALSE;
	if(!(pIntf->fidfps = env->GetFieldID(pIntf->clsId, "fps", "I"))) return FALSE;
	if(!(pIntf->fidbrmode = env->GetFieldID(pIntf->clsId, "brmode", "I"))) return FALSE;
	if(!(pIntf->fidsave = env->GetFieldID(pIntf->clsId, "save", "I"))) return FALSE;
	if(!(pIntf->fidkbps = env->GetFieldID(pIntf->clsId, "kbps", "I"))) return FALSE;
	if(!(pIntf->fidgop = env->GetFieldID(pIntf->clsId, "gop", "I"))) return FALSE;
	if(!(pIntf->fidmax_fps = env->GetFieldID(pIntf->clsId, "max_fps", "I"))) return FALSE;
	return TRUE;	
}

JNIEXPORT jobjectArray Java_spider_szc_JNI_GetVideoParamNew(JNIEnv *env, jclass, jint _pSess)
{

	P2pSession *pSess = (P2pSession*)_pSess;
	struct CmdListVideoPara *resp;
	struct videoparamIntf intf;
	if(!_VideoParaIntf(env,&intf)) return FALSE;
	jobjectArray result=NULL;
	if(!DAP2pCmdGetVideoParamNew(pSess->GetConnectionHandle(),&resp))
	{
			int i;
			result = env->NewObjectArray(resp->n_num, intf.clsId, NULL);
			jmethodID cid = env->GetMethodID(intf.clsId, "<init>", "()V");
			for(i=0;i<resp->n_num;i++)
			{
					jobject obj = env->NewObject(intf.clsId,cid );
					env->SetIntField(obj, intf.fidres, resp->dcs[i].res);
					env->SetIntField(obj, intf.fidquality, resp->dcs[i].quality);
					env->SetIntField(obj, intf.fidfps, resp->dcs[i].fps);
					env->SetIntField(obj, intf.fidbrmode, resp->dcs[i].brmode);
					env->SetIntField(obj, intf.fidsave, resp->dcs[i].save);
					env->SetIntField(obj, intf.fidkbps, resp->dcs[i].kbps);
					env->SetIntField(obj, intf.fidgop, resp->dcs[i].gop);
					env->SetIntField(obj, intf.fidmax_fps, resp->dcs[i].max_fps);
					env->SetObjectArrayElement(result, i, obj);
			}
			return result;
	}
	return NULL;
}
JNIEXPORT jint Java_spider_szc_JNI_SetVideoParamNew(JNIEnv *env, jclass, jint _pSess,jobject param)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	struct dcs_video_param_new req;
	struct videoparamIntf intf;
	if(!_VideoParaIntf(env,&intf)) return FALSE;
	req.res= env->GetIntField(param, intf.fidres);
	req.quality= env->GetIntField(param, intf.fidquality);
	req.fps= env->GetIntField(param, intf.fidfps);
	req.brmode= env->GetIntField(param, intf.fidbrmode);
	req.save= env->GetIntField(param, intf.fidsave);
	req.kbps= env->GetIntField(param, intf.fidkbps);
	req.gop= env->GetIntField(param, intf.fidgop);
	req.max_fps= env->GetIntField(param, intf.fidmax_fps);

	return DAP2pCmdSetVideoParamNew(pSess->GetConnectionHandle(),&req);
}

JNIEXPORT jint Java_spider_szc_JNI_DefaultConn(JNIEnv *env, jclass, jint _pSess,jint param)
{
	P2pSession *pSess = (P2pSession*)_pSess;
	int rlt;
	if(param==1001)
	{
		if(!DAP2pCmdGetDefault(pSess->GetConnectionHandle(),&rlt,&param))
		{
			return rlt;
		}
	}
	return 105;
	
}


/*
 * Class:     spider_szc_JNI
 * Method:    getNetInfo
 * Signature: (ILspider/szc/JNI/NetInfo;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_getNetInfo(JNIEnv *env, jclass clazz, jint conn, jobject netInfo){
	P2pSession *pSess = (P2pSession*)conn;
	DANetInfo netInfoData;

	if (netInfo == NULL)
		return -1;
	
	int result =  DAP2pCmdGetNetInfo(pSess->GetConnectionHandle(), &netInfoData);
	if (result != 0){
		return result;
	}

	jclass cls = env->GetObjectClass(netInfo);
	jfieldID ipType = env->GetFieldID(cls, "ipType", "I");
	jfieldID dnsType = env->GetFieldID(cls, "dnsType", "I");
	jfieldID ip = env->GetFieldID(cls, "ip", "Ljava/lang/String;");
	jfieldID netmask = env->GetFieldID(cls, "netmask", "Ljava/lang/String;");
	jfieldID gateway = env->GetFieldID(cls, "gateway", "Ljava/lang/String;");
	jfieldID mainDns = env->GetFieldID(cls, "mainDns", "Ljava/lang/String;");
	jfieldID secondDns = env->GetFieldID(cls, "secondDns", "Ljava/lang/String;");
	jfieldID port = env->GetFieldID(cls, "port", "I");

	env->SetIntField(netInfo, ipType, netInfoData.IpType);
	env->SetIntField(netInfo, dnsType, netInfoData.DnsType);
	env->SetObjectField(netInfo, ip, env->NewStringUTF(netInfoData.cIp));
	env->SetObjectField(netInfo, netmask, env->NewStringUTF(netInfoData.cNetMask));
	env->SetObjectField(netInfo, gateway, env->NewStringUTF(netInfoData.cGateway));
	env->SetObjectField(netInfo, mainDns, env->NewStringUTF(netInfoData.cMDns));
	env->SetObjectField(netInfo, secondDns, env->NewStringUTF(netInfoData.cSDns));
	env->SetIntField(netInfo, port, netInfoData.chttpPort);

	return 0;
	
}

/*
 * Class:     spider_szc_JNI
 * Method:    setNetInfo
 * Signature: (ILspider/szc/JNI/NetInfo;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_setNetInfo(JNIEnv *env, jclass clazz, jint conn, jobject netInfo){
	P2pSession *pSess = (P2pSession*)conn;
	DANetInfo netInfoData;

	if (netInfo == NULL)
		return -1;
	
	jclass cls = env->GetObjectClass(netInfo);
	jfieldID ipType = env->GetFieldID(cls, "ipType", "I");
	jfieldID dnsType = env->GetFieldID(cls, "dnsType", "I");
	jfieldID ip = env->GetFieldID(cls, "ip", "Ljava/lang/String;");
	jfieldID netmask = env->GetFieldID(cls, "netmask", "Ljava/lang/String;");
	jfieldID gateway = env->GetFieldID(cls, "gateway", "Ljava/lang/String;");
	jfieldID mainDns = env->GetFieldID(cls, "mainDns", "Ljava/lang/String;");
	jfieldID secondDns = env->GetFieldID(cls, "secondDns", "Ljava/lang/String;");
	jfieldID port = env->GetFieldID(cls, "port", "I");

	netInfoData.IpType = env->GetIntField(netInfo, ipType);
	netInfoData.DnsType = env->GetIntField(netInfo, dnsType);
	netInfoData.chttpPort = env->GetIntField(netInfo, port);

	jstring str = (jstring)env->GetObjectField(netInfo, ip);
	const char *str_ip = env->GetStringUTFChars(str, NULL);
	strcpy(netInfoData.cIp, str_ip);
	env->ReleaseStringUTFChars(str, str_ip);

	str = (jstring)env->GetObjectField(netInfo, netmask);
 	const char *str_netmask = env->GetStringUTFChars(str, NULL);
	strcpy(netInfoData.cNetMask, str_netmask);
	env->ReleaseStringUTFChars(str, str_netmask);

	str = (jstring)env->GetObjectField(netInfo, gateway);
	const char *str_gateway = env->GetStringUTFChars(str, NULL);
	strcpy(netInfoData.cGateway, str_gateway);
	env->ReleaseStringUTFChars(str, str_gateway);

	str = (jstring)env->GetObjectField(netInfo, mainDns);
	const char *str_maindns = env->GetStringUTFChars(str, NULL);
	strcpy(netInfoData.cMDns, str_maindns);
	env->ReleaseStringUTFChars(str, str_maindns);

	str = (jstring)env->GetObjectField(netInfo, secondDns);
	const char *str_seconddns = env->GetStringUTFChars(str, NULL);
	strcpy(netInfoData.cSDns, str_seconddns);
	env->ReleaseStringUTFChars(str, str_seconddns);

	return DAP2pCmdSetNetInfo(pSess->GetConnectionHandle(), &netInfoData);

}



struct TimeInfoClass{
	jclass clazz;
	jfieldID nowTime,timeMode,timeArea,setTime,timeServer,interval, gmtInfo;
};

struct TimeInfoClass getTimeInfoClass(JNIEnv *env, jobject obj)
{
	struct TimeInfoClass timeInfoClass;
	timeInfoClass.clazz = env->GetObjectClass(obj);
	timeInfoClass.nowTime = env->GetFieldID(timeInfoClass.clazz, "nowTime", "Lspider/szc/JNI$DateTime;");
	timeInfoClass.setTime = env->GetFieldID(timeInfoClass.clazz, "setTime", "Lspider/szc/JNI$DateTime;");
	timeInfoClass.timeMode = env->GetFieldID(timeInfoClass.clazz, "timeMode", "I");
	timeInfoClass.timeArea = env->GetFieldID(timeInfoClass.clazz, "timeArea", "I");
	timeInfoClass.timeServer = env->GetFieldID(timeInfoClass.clazz, "timeServer", "Ljava/lang/String;");
	timeInfoClass.interval = env->GetFieldID(timeInfoClass.clazz, "interval", "I");
	timeInfoClass.gmtInfo = env->GetFieldID(timeInfoClass.clazz, "gmtInfo", "Ljava/lang/String;");
	return timeInfoClass;
}

struct DateTimeClass{
	jclass clazz;
	jfieldID year,mon,day,hour,min,sec;
	jmethodID constructor;
};

struct DateTimeClass getDateTimeClass(JNIEnv *env)
{
	struct DateTimeClass dateTimeClass;

	dateTimeClass.clazz = env->FindClass("spider/szc/JNI$DateTime");
	dateTimeClass.year = env->GetFieldID(dateTimeClass.clazz, "year", "I");
	dateTimeClass.mon = env->GetFieldID(dateTimeClass.clazz, "mon", "I");
	dateTimeClass.day = env->GetFieldID(dateTimeClass.clazz, "day", "I");
	dateTimeClass.hour = env->GetFieldID(dateTimeClass.clazz, "hour", "I");
	dateTimeClass.min = env->GetFieldID(dateTimeClass.clazz, "min", "I");
	dateTimeClass.sec = env->GetFieldID(dateTimeClass.clazz, "sec", "I");
 	dateTimeClass.constructor = env->GetMethodID(dateTimeClass.clazz, "<init>", "()V");  
	return dateTimeClass;
}


/*
 * Class:     spider_szc_JNI
 * Method:    getTimeInfo
 * Signature: (ILspider/szc/JNI/TimeInfo;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_getTimeInfo(JNIEnv *env, jclass clazz, jint conn, jobject timeInfo){
	P2pSession *pSess = (P2pSession*)conn;
	struct DATimeInfo timeInfoData;
	struct TimeInfoClass timeInfoClass;
	struct DateTimeClass dateTimeClass;

	if (timeInfo == NULL)
		return -1;

	int result =  DAP2pCmdGetTimeInfo(pSess->GetConnectionHandle(), &timeInfoData);
	if (result != 0){
		return result;
	}

	timeInfoClass = getTimeInfoClass(env, timeInfo);

	env->SetIntField(timeInfo, timeInfoClass.timeMode, (int)timeInfoData.timeMode);
	env->SetIntField(timeInfo, timeInfoClass.timeArea, (int)timeInfoData.timeArea);
	env->SetIntField(timeInfo, timeInfoClass.interval, timeInfoData.Interval);
	env->SetObjectField(timeInfo, timeInfoClass.timeServer, env->NewStringUTF(timeInfoData.server));
	env->SetObjectField(timeInfo, timeInfoClass.gmtInfo, env->NewStringUTF(timeInfoData.gmtInf));

	dateTimeClass = getDateTimeClass(env);

	jobject nowTime = env->NewObject(dateTimeClass.clazz, dateTimeClass.constructor);
	env->SetIntField(nowTime, dateTimeClass.year, timeInfoData.nowTime.year);
	env->SetIntField(nowTime, dateTimeClass.mon, timeInfoData.nowTime.mon);
	env->SetIntField(nowTime, dateTimeClass.day, timeInfoData.nowTime.mday);
	env->SetIntField(nowTime, dateTimeClass.hour, timeInfoData.nowTime.hour);
	env->SetIntField(nowTime, dateTimeClass.min, timeInfoData.nowTime.min);
	env->SetIntField(nowTime, dateTimeClass.sec, timeInfoData.nowTime.sec);
	env->SetObjectField(timeInfo, timeInfoClass.nowTime, nowTime);

	jobject setTime = env->NewObject(dateTimeClass.clazz, dateTimeClass.constructor);
	env->SetIntField(setTime, dateTimeClass.year, timeInfoData.SetTime.year);
	env->SetIntField(setTime, dateTimeClass.mon, timeInfoData.SetTime.mon);
	env->SetIntField(setTime, dateTimeClass.day, timeInfoData.SetTime.mday);
	env->SetIntField(setTime, dateTimeClass.hour, timeInfoData.SetTime.hour);
	env->SetIntField(setTime, dateTimeClass.min, timeInfoData.SetTime.min);
	env->SetIntField(setTime, dateTimeClass.sec, timeInfoData.SetTime.sec);
	env->SetObjectField(timeInfo, timeInfoClass.setTime, setTime);

	return 0;
}

/*
 * Class:     spider_szc_JNI
 * Method:    setTimeInfo
 * Signature: (ILspider/szc/JNI/TimeInfo;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_setTimeInfo(JNIEnv *env, jclass clazz, jint conn, jobject timeInfo){
	P2pSession *pSess = (P2pSession*)conn;
	struct DATimeInfo timeInfoData;
	struct TimeInfoClass timeInfoClass;
	struct DateTimeClass dateTimeClass;

	if (timeInfo == NULL)
		return -1;
	
	timeInfoClass = getTimeInfoClass(env, timeInfo);

	timeInfoData.timeMode= (TimeMode)env->GetIntField(timeInfo, timeInfoClass.timeMode);
	timeInfoData.timeArea= (TimeArea)env->GetIntField(timeInfo, timeInfoClass.timeArea);
	timeInfoData.Interval= env->GetIntField(timeInfo, timeInfoClass.interval);

	jstring timeServer = (jstring)env->GetObjectField(timeInfo, timeInfoClass.timeServer);
	const char *str_timeServer = env->GetStringUTFChars(timeServer, NULL);
	strcpy(timeInfoData.server, str_timeServer);
	env->ReleaseStringUTFChars(timeServer, str_timeServer);

	jstring gtmInfo = (jstring)env->GetObjectField(timeInfo, timeInfoClass.gmtInfo);
	const char *str_gtmInfo = env->GetStringUTFChars(gtmInfo, NULL);
	strcpy(timeInfoData.gmtInf, str_gtmInfo);
	env->ReleaseStringUTFChars(gtmInfo, str_gtmInfo);

	dateTimeClass = getDateTimeClass(env);
	jobject nowTime = env->GetObjectField(timeInfo, timeInfoClass.nowTime);
	timeInfoData.nowTime.year = env->GetIntField(nowTime, dateTimeClass.year);
	timeInfoData.nowTime.mon = env->GetIntField(nowTime, dateTimeClass.mon);
	timeInfoData.nowTime.mday= env->GetIntField(nowTime, dateTimeClass.day);
	timeInfoData.nowTime.hour= env->GetIntField(nowTime, dateTimeClass.hour);
	timeInfoData.nowTime.min= env->GetIntField(nowTime, dateTimeClass.min);
	timeInfoData.nowTime.sec= env->GetIntField(nowTime, dateTimeClass.sec);

	jobject setTime = env->GetObjectField(timeInfo, timeInfoClass.setTime);
	timeInfoData.SetTime.year = env->GetIntField(setTime, dateTimeClass.year);
	timeInfoData.SetTime.mon = env->GetIntField(setTime, dateTimeClass.mon);
	timeInfoData.SetTime.mday= env->GetIntField(setTime, dateTimeClass.day);
	timeInfoData.SetTime.hour= env->GetIntField(setTime, dateTimeClass.hour);
	timeInfoData.SetTime.min= env->GetIntField(setTime, dateTimeClass.min);
	timeInfoData.SetTime.sec= env->GetIntField(setTime, dateTimeClass.sec);

	return DAP2pCmdSetTimeInfo(pSess->GetConnectionHandle(), &timeInfoData);

}

/*
 * Class:     spider_szc_JNI
 * Method:    setInitInfo
 * Signature: (II)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_setInitInfo(JNIEnv *env, jclass clazz, jint conn, jint initInfo){
	P2pSession *pSess = (P2pSession*)conn;
	struct DAInitSet initInfoData;
	initInfoData.Type = initInfo;
	DAP2pCmdSetInitInfo(pSess->GetConnectionHandle(), &initInfoData);
}


struct WifiInfoClass{
	jclass clazz;
	jfieldID ssid,key,enctype,keyindex,dhcp,ip,netmask,gateway,dns1,dns2;
};

struct WifiInfoClass getWifiInfoClass(JNIEnv *env, jobject obj)
{
	struct WifiInfoClass wifiInfoClass;
	wifiInfoClass.clazz = env->GetObjectClass(obj);
	wifiInfoClass.ssid = env->GetFieldID(wifiInfoClass.clazz, "ssid", "Ljava/lang/String;");
	wifiInfoClass.key = env->GetFieldID(wifiInfoClass.clazz, "key", "Ljava/lang/String;");
	wifiInfoClass.enctype = env->GetFieldID(wifiInfoClass.clazz, "enctype", "I");
	wifiInfoClass.keyindex = env->GetFieldID(wifiInfoClass.clazz, "keyindex", "I");
	wifiInfoClass.dhcp = env->GetFieldID(wifiInfoClass.clazz, "dhcp", "Z");
	wifiInfoClass.ip = env->GetFieldID(wifiInfoClass.clazz, "ip", "Ljava/lang/String;");
	wifiInfoClass.netmask = env->GetFieldID(wifiInfoClass.clazz, "netmask", "Ljava/lang/String;");
	wifiInfoClass.gateway = env->GetFieldID(wifiInfoClass.clazz, "gateway", "Ljava/lang/String;");
	wifiInfoClass.dns1 = env->GetFieldID(wifiInfoClass.clazz, "dns1", "Ljava/lang/String;");
	wifiInfoClass.dns2 = env->GetFieldID(wifiInfoClass.clazz, "dns2", "Ljava/lang/String;");
	return wifiInfoClass;
}


/*
 * Class:     spider_szc_JNI
 * Method:    setApWifi
 * Signature: (ILspider/szc/JNI/WifiInfo;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_setApWifi (JNIEnv *env, jclass clazz, jint conn, jobject wifiInfo){
	struct WifiInfoClass wifiInfoClass;
	struct AP_Wifi pReq;
	P2pSession *pSess = (P2pSession*)conn;

	if (wifiInfo == NULL){
		return -1;
	}

	wifiInfoClass = getWifiInfoClass(env, wifiInfo);

	jstring str = (jstring)env->GetObjectField(wifiInfo, wifiInfoClass.ssid);
	const char *str_ssid = env->GetStringUTFChars(str, NULL);
	strcpy(pReq.essid, str_ssid);
	env->ReleaseStringUTFChars(str, str_ssid);

	str = (jstring)env->GetObjectField(wifiInfo, wifiInfoClass.key);
	const char *str_key = env->GetStringUTFChars(str, NULL);
	strcpy(pReq.key, str_key);
	env->ReleaseStringUTFChars(str, str_key);

	pReq.enctype = (WIFI_ENCTYPE)env->GetIntField(wifiInfo, wifiInfoClass.enctype);
	pReq.keyindex = (uint8)env->GetIntField(wifiInfo, wifiInfoClass.keyindex);
	pReq.isDHCP = env->GetBooleanField(wifiInfo, wifiInfoClass.dhcp)? 1: 0;
	
	str = (jstring)env->GetObjectField(wifiInfo, wifiInfoClass.ip);
	const char *str_ip = env->GetStringUTFChars(str, NULL);
	strcpy(pReq.cIp, str_ip);
	env->ReleaseStringUTFChars(str, str_ip);

	str = (jstring)env->GetObjectField(wifiInfo, wifiInfoClass.netmask);
	const char *str_netmask = env->GetStringUTFChars(str, NULL);
	strcpy(pReq.cNetMask, str_netmask);
	env->ReleaseStringUTFChars(str, str_netmask);

	str = (jstring)env->GetObjectField(wifiInfo, wifiInfoClass.gateway);
	const char *str_gateway = env->GetStringUTFChars(str, NULL);
	strcpy(pReq.cGateway, str_gateway);
	env->ReleaseStringUTFChars(str, str_gateway);

	str = (jstring)env->GetObjectField(wifiInfo, wifiInfoClass.dns1);
	const char *str_dns1 = env->GetStringUTFChars(str, NULL);
	strcpy(pReq.cMDns, str_dns1);
	env->ReleaseStringUTFChars(str, str_dns1);

	str = (jstring)env->GetObjectField(wifiInfo, wifiInfoClass.dns2);
	const char *str_dns2 = env->GetStringUTFChars(str, NULL);
	strcpy(pReq.cSDns, str_dns2);
	env->ReleaseStringUTFChars(str, str_dns2);

	return DAP2pCmdSetAPWifi(pSess->GetConnectionHandle(), &pReq);
}

/*
 * Class:     spider_szc_JNI
 * Method:    getSnapshot
 * Signature: (ILjava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_getSnapshot(JNIEnv *env, jclass jclazz, jint conn, jstring filePath){
	P2pSession *pSess = (P2pSession*)conn;
	char path[256];
	
	if (filePath == NULL)
		return -1;

	copyJStringToNative(path, sizeof(path), env, filePath);
	return DAP2pCmdSnapshot(pSess->GetConnectionHandle(), path);
}

struct CloudAlarmInfoClass{
	jclass clazz;
	jfieldID isOpenMotion, motionLevel, isOpenSound, soundLevel, isOpenIO, ioLevel, isOpenOther, otherLevel;
};

struct CloudAlarmInfoClass getCloudAlarmInfoClass(JNIEnv *env, jobject obj)
{
	struct CloudAlarmInfoClass clazz;
	clazz.clazz = env->GetObjectClass(obj);
	clazz.isOpenMotion = env->GetFieldID(clazz.clazz, "isOpenMotion", "I");
	clazz.motionLevel = env->GetFieldID(clazz.clazz, "motionLevel", "I");
	clazz.isOpenSound = env->GetFieldID(clazz.clazz, "isOpenSound", "I");
	clazz.soundLevel = env->GetFieldID(clazz.clazz, "soundLevel", "I");
	clazz.isOpenIO = env->GetFieldID(clazz.clazz, "isOpenIO", "I");
	clazz.ioLevel = env->GetFieldID(clazz.clazz, "ioLevel", "I");
	clazz.isOpenOther = env->GetFieldID(clazz.clazz, "isOpenOther", "I");
	clazz.otherLevel = env->GetFieldID(clazz.clazz, "otherLevel", "I");
	return clazz;
}


/*
 * Class:     spider_szc_JNI
 * Method:    getCloudAlarmInfo
 * Signature: (ILspider/szc/JNI/CloudAlarmInfo;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_getCloudAlarmInfo(JNIEnv *env, jclass jclazz, jint conn, jobject cloudAlarmInfo){
	P2pSession *pSess = (P2pSession*)conn;
	struct CloudAlarmInfoClass cloudAlarmInfoClass;
	struct CloudAlarm cloudAlarmData;

	if (cloudAlarmInfo == NULL)
		return -1;

	int result =  DAP2pCmdGetCloudAlarmInfo(pSess->GetConnectionHandle(), &cloudAlarmData);
	if (result != 0){
		return result;
	}
	
	cloudAlarmInfoClass = getCloudAlarmInfoClass(env, cloudAlarmInfo);
	env->SetIntField(cloudAlarmInfo, cloudAlarmInfoClass.isOpenMotion, cloudAlarmData.IsOpenMotionDetection);
	env->SetIntField(cloudAlarmInfo, cloudAlarmInfoClass.motionLevel, cloudAlarmData.MontionDetectionLevel);
	env->SetIntField(cloudAlarmInfo, cloudAlarmInfoClass.isOpenSound, cloudAlarmData.IsOpenSoundDetection);
	env->SetIntField(cloudAlarmInfo, cloudAlarmInfoClass.soundLevel, cloudAlarmData.SoundDetectionLevel);
	env->SetIntField(cloudAlarmInfo, cloudAlarmInfoClass.isOpenIO, cloudAlarmData.IsOpenI2ODetection);
	env->SetIntField(cloudAlarmInfo, cloudAlarmInfoClass.ioLevel, cloudAlarmData.I2ODetectionLevel);
	env->SetIntField(cloudAlarmInfo, cloudAlarmInfoClass.isOpenOther, cloudAlarmData.IsOpenOther);
	env->SetIntField(cloudAlarmInfo, cloudAlarmInfoClass.otherLevel, cloudAlarmData.OtherDetectionLevel);
	return 0;
}

/*
 * Class:     spider_szc_JNI
 * Method:    setCloudAlarmInfo
 * Signature: (ILspider/szc/JNI/CloudAlarmInfo;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_setCloudAlarmInfo(JNIEnv *env, jclass jclass, jint conn, jobject cloudAlarmInfo){
	P2pSession *pSess = (P2pSession*)conn;
	struct CloudAlarmInfoClass cloudAlarmInfoClass;
	struct CloudAlarm cloudAlarmData;

	if (cloudAlarmInfo == NULL)
		return -1;

	cloudAlarmInfoClass = getCloudAlarmInfoClass(env, cloudAlarmInfo);
	
	cloudAlarmData.IsOpenMotionDetection =  env->GetIntField(cloudAlarmInfo, cloudAlarmInfoClass.isOpenMotion);
	cloudAlarmData.MontionDetectionLevel = env->GetIntField(cloudAlarmInfo, cloudAlarmInfoClass.motionLevel);
	cloudAlarmData.IsOpenSoundDetection = env->GetIntField(cloudAlarmInfo, cloudAlarmInfoClass.isOpenSound);
	cloudAlarmData.SoundDetectionLevel = env->GetIntField(cloudAlarmInfo, cloudAlarmInfoClass.soundLevel);
	cloudAlarmData.IsOpenI2ODetection = env->GetIntField(cloudAlarmInfo, cloudAlarmInfoClass.isOpenIO);
	cloudAlarmData.I2ODetectionLevel = env->GetIntField(cloudAlarmInfo, cloudAlarmInfoClass.ioLevel);
	cloudAlarmData.IsOpenOther = env->GetIntField(cloudAlarmInfo, cloudAlarmInfoClass.isOpenOther);
	cloudAlarmData.OtherDetectionLevel = env->GetIntField(cloudAlarmInfo, cloudAlarmInfoClass.otherLevel);

	return DAP2pCmdSetCloudAlarmInfo(pSess->GetConnectionHandle(), &cloudAlarmData);

}

//////////////////////////////////////////////////////////////////////////////

struct SearchRecordRequestClass{
	jclass clazz;
	jfieldID pageNum, currentPage, flag, channel, recordType, from, to;
};

struct SearchRecordRequestClass getSearchRecordRequestClass(JNIEnv *env, jobject obj)
{
	struct SearchRecordRequestClass clazz;
	clazz.clazz = env->GetObjectClass(obj);
	//clazz.clazz = env->FindClass("spider/szc/JNI$SearchRecordRequest");
	clazz.pageNum = env->GetFieldID(clazz.clazz, "pageNum", "I");
	clazz.currentPage = env->GetFieldID(clazz.clazz, "currentPage", "I");
	clazz.flag = env->GetFieldID(clazz.clazz, "flag", "I");
	clazz.channel = env->GetFieldID(clazz.clazz, "channel", "I");
	clazz.recordType = env->GetFieldID(clazz.clazz, "recordType", "I");
	clazz.from = env->GetFieldID(clazz.clazz, "from", "Lspider/szc/JNI$DateTime;");
	clazz.to = env->GetFieldID(clazz.clazz, "to", "Lspider/szc/JNI$DateTime;");
	return clazz;
}

struct SearchRecordResponse{
	jclass clazz;
	jmethodID init;
	jfieldID totalNum, firstNum, list;
};

struct SearchRecordResponse getSearchRecordResponse(JNIEnv *env, jobject obj)
{
	struct SearchRecordResponse clazz;
	clazz.clazz = env->GetObjectClass(obj);
	//clazz.clazz = env->FindClass("spider/szc/JNI$SearchRecordResponse");
	clazz.totalNum = env->GetFieldID(clazz.clazz, "totalNum", "I");
	clazz.firstNum = env->GetFieldID(clazz.clazz, "firstNum", "I");
	clazz.list = env->GetFieldID(clazz.clazz, "list", "Ljava/util/List;");
	clazz.init = env->GetMethodID(clazz.clazz , "<init>", "()V"); 
	return clazz;
}

struct SDCardRecordClass{
	jclass clazz;
	jmethodID init;
	jfieldID channel, recordType, from, timeLen;
};

struct SDCardRecordClass getSDCardRecordClass(JNIEnv *env)
{
	struct SDCardRecordClass clazz;
	clazz.clazz = env->FindClass("spider/szc/JNI$SDCardRecord");
	clazz.channel = env->GetFieldID(clazz.clazz, "channel", "I");
	clazz.recordType = env->GetFieldID(clazz.clazz, "recordType", "I");
	clazz.from = env->GetFieldID(clazz.clazz, "from", "Lspider/szc/JNI$DateTime;");
	clazz.timeLen = env->GetFieldID(clazz.clazz, "timeLen", "I");
	clazz.init = env->GetMethodID(clazz.clazz , "<init>", "()V");  
	return clazz;
}

struct SDCardRecordPlanClass{
	jclass clazz;
	jmethodID init;
	jfieldID channel, planAction, from, to, weekInfo;
};

struct SDCardRecordPlanClass getSDCardRecordPlanClass(JNIEnv *env)
{
	struct SDCardRecordPlanClass clazz;
	clazz.clazz = env->FindClass("spider/szc/JNI$SDCardRecordPlan");
	clazz.channel = env->GetFieldID(clazz.clazz, "channel", "I");
	clazz.planAction = env->GetFieldID(clazz.clazz, "planAction", "I");
	clazz.from = env->GetFieldID(clazz.clazz, "from", "Lspider/szc/JNI$DateTime;");
	clazz.to = env->GetFieldID(clazz.clazz, "to", "Lspider/szc/JNI$DateTime;");
	clazz.weekInfo = env->GetFieldID(clazz.clazz, "weekInfo", "I");
	clazz.init = env->GetMethodID(clazz.clazz , "<init>", "()V");  
	return clazz;
}


/*
 * Class:     spider_szc_JNI
 * Method:    SDReocrdGetReocrdList
 * Signature: (ILspider/szc/JNI/SearchRecordRequest;Lspider/szc/JNI/SearchRecordResponse;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_sdcardReocrdGetReocrdList(JNIEnv *env, jclass clazz, jint connect, jobject request, jobject response){
	P2pSession *pSess = (P2pSession*)connect;
	struct CmdSearchRecordReq req;
	struct CmdGetListSearchReocrdResp *resp;
	struct dcs_datetime from_datetime;
	struct dcs_datetime to_datetime;
	
	struct SearchRecordRequestClass searchRecordRequestClazz = getSearchRecordRequestClass(env, request);
	struct SearchRecordResponse searchRecordResponseClazz = getSearchRecordResponse(env, response);
	struct SDCardRecordClass sdcardRecordClazz = getSDCardRecordClass(env);
	struct DateTimeClass dateTimeClass = getDateTimeClass(env);

	jobject from = env->GetObjectField(request, searchRecordRequestClazz.from);
	from_datetime.year = env->GetIntField(from, dateTimeClass.year);
	from_datetime.mon = env->GetIntField(from, dateTimeClass.mon);
	from_datetime.mday = env->GetIntField(from, dateTimeClass.day);
	from_datetime.hour = env->GetIntField(from, dateTimeClass.hour);
	from_datetime.min = env->GetIntField(from, dateTimeClass.min);
	from_datetime.sec = env->GetIntField(from, dateTimeClass.sec);

	jobject to = env->GetObjectField(request, searchRecordRequestClazz.to);
	to_datetime.year = env->GetIntField(to, dateTimeClass.year);
	to_datetime.mon = env->GetIntField(to, dateTimeClass.mon);
	to_datetime.mday = env->GetIntField(to, dateTimeClass.day);
	to_datetime.hour = env->GetIntField(to, dateTimeClass.hour);
	to_datetime.min = env->GetIntField(to, dateTimeClass.min);
	to_datetime.sec = env->GetIntField(to, dateTimeClass.sec);

	req.nItemPerPage = env->GetIntField(request, searchRecordRequestClazz.pageNum);
	req.iPage = env->GetIntField(request, searchRecordRequestClazz.currentPage);
	req.flag = env->GetIntField(request, searchRecordRequestClazz.flag);
	req.chn = env->GetIntField(request, searchRecordRequestClazz.channel);
	req.eventType = (EnumRecordTYPE)(env->GetIntField(request, searchRecordRequestClazz.recordType));
	req.from = from_datetime;
	req.to = to_datetime;
	
	int ret = DAP2pGetRecordlListByTime(pSess->GetConnectionHandle(), &req, &resp);
	if (ret != 0){
		return ret;
	}

	jclass listClazz = env->FindClass("java/util/ArrayList");
	jmethodID listInit = env->GetMethodID(listClazz, "<init>", "()V");
	jmethodID listAdd = env->GetMethodID(listClazz, "add", "(Ljava/lang/Object;)Z");
	jobject list = env->NewObject(listClazz, listInit);

	env->SetIntField(response, searchRecordResponseClazz.totalNum, resp->nTotal);
	env->SetIntField(response, searchRecordResponseClazz.firstNum, resp->iFirstItem);
	env->SetObjectField(response, searchRecordResponseClazz.list, list);

	for (int i=0; i<resp->n_item && i<req.nItemPerPage; i++){
		jobject fromTime = env->NewObject(dateTimeClass.clazz, dateTimeClass.constructor);
		env->SetIntField(fromTime, dateTimeClass.year, resp->items[i].tRec.year);
		env->SetIntField(fromTime, dateTimeClass.mon, resp->items[i].tRec.mon);
		env->SetIntField(fromTime, dateTimeClass.day, resp->items[i].tRec.mday);
		env->SetIntField(fromTime, dateTimeClass.hour, resp->items[i].tRec.hour);
		env->SetIntField(fromTime, dateTimeClass.min, resp->items[i].tRec.min);
		env->SetIntField(fromTime, dateTimeClass.sec, resp->items[i].tRec.sec);
		
		jobject record = env->NewObject(sdcardRecordClazz.clazz, sdcardRecordClazz.init);
		env->SetIntField(record, sdcardRecordClazz.channel, resp->items[i].chn);
		env->SetIntField(record, sdcardRecordClazz.recordType, (int)(resp->items[i].eventType));
		env->SetIntField(record, sdcardRecordClazz.timeLen, resp->items[i].timelen);
		env->SetObjectField(record, sdcardRecordClazz.from, fromTime);

		env->CallBooleanMethod(list, listAdd, record);
	}
	if (resp != NULL){
		free(resp);
	}
	return 0;
}

/*
 * Class:     spider_szc_JNI
 * Method:    SDReocrdPlaybackReocrd
 * Signature: (ILspider/szc/JNI/SDCardRecord;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_sdcardReocrdPlaybackReocrd(JNIEnv *env, jclass clazz, jint connect, jobject record, jobject colorArray){
	P2pSession *pSess = (P2pSession*)connect;
	struct SDCardRecordClass sdcardRecordClazz = getSDCardRecordClass(env);
	struct DateTimeClass dateTimeClass = getDateTimeClass(env);

	struct CmdPlayBackReq req;
	req.chn = env->GetIntField(record, sdcardRecordClazz.channel);
	req.eventType = (EnumRecordTYPE)env->GetIntField(record, sdcardRecordClazz.recordType);
	req.timelen = env->GetIntField(record, sdcardRecordClazz.timeLen);

	jobject timeFrom = env->GetObjectField(record, sdcardRecordClazz.from);
	req.tRec.year = env->GetIntField(timeFrom, dateTimeClass.year);
	req.tRec.mon= env->GetIntField(timeFrom, dateTimeClass.mon);
	req.tRec.mday= env->GetIntField(timeFrom, dateTimeClass.day);
	req.tRec.hour= env->GetIntField(timeFrom, dateTimeClass.hour);
	req.tRec.min= env->GetIntField(timeFrom, dateTimeClass.min);
	req.tRec.sec= env->GetIntField(timeFrom, dateTimeClass.sec);

	return pSess->startSdcardRecordVideo(env, req, colorArray);
}

/*
 * Class:     spider_szc_JNI
 * Method:    SDRecordPlayBackSelectTime
 * Signature: (ILspider/szc/JNI/SDCardRecord;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_sdcardRecordPlayBackSelectTime(JNIEnv *env, jclass clazz, jint connect, jobject record){
	P2pSession *pSess = (P2pSession*)connect;
	struct SDCardRecordClass sdcardRecordClazz = getSDCardRecordClass(env);
	struct DateTimeClass dateTimeClass = getDateTimeClass(env);

	struct CmdPlayBackReq req;
	req.chn = env->GetIntField(record, sdcardRecordClazz.channel);
	req.eventType = (EnumRecordTYPE)env->GetIntField(record, sdcardRecordClazz.recordType);
	req.timelen = env->GetIntField(record, sdcardRecordClazz.timeLen);

	jobject timeFrom = env->GetObjectField(record, sdcardRecordClazz.from);
	req.tRec.year = env->GetIntField(timeFrom, dateTimeClass.year);
	req.tRec.mon= env->GetIntField(timeFrom, dateTimeClass.mon);
	req.tRec.mday= env->GetIntField(timeFrom, dateTimeClass.day);
	req.tRec.hour= env->GetIntField(timeFrom, dateTimeClass.hour);
	req.tRec.min= env->GetIntField(timeFrom, dateTimeClass.min);
	req.tRec.sec= env->GetIntField(timeFrom, dateTimeClass.sec);
	return DAP2pPlayBackSelectTime(pSess->GetConnectionHandle(), &req);
}

/*
 * Class:     spider_szc_JNI
 * Method:    SDRecordSetPlayRate
 * Signature: (II)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_sdcardRecordSetPlayRate(JNIEnv *env, jclass clazz, jint connect, jint rate){
	P2pSession *pSess = (P2pSession*)connect;
	EnumRateTimes req = (EnumRateTimes)rate;
	return DAP2pSetPlayRate(pSess->GetConnectionHandle(), &req);
}

/*
 * Class:     spider_szc_JNI
 * Method:    SDRecordPauseOrRePlayVideo
 * Signature: (II)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_sdcardRecordPauseOrRePlayVideo(JNIEnv *env, jclass clazz, jint connect, jint action){
	P2pSession *pSess = (P2pSession*)connect;
	EnumVideoType req = (EnumVideoType)action;
	return DAP2pPauseOrRePlayVideo(pSess->GetConnectionHandle(), &req);
}

/*
 * Class:     spider_szc_JNI
 * Method:    SDRecordTerminatePlayBack
 * Signature: (ILspider/szc/JNI/SDCardRecord;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_sdcardRecordTerminatePlayBack(JNIEnv *env, jclass clazz, jint connect, jobject record){
	P2pSession *pSess = (P2pSession*)connect;
	struct SDCardRecordClass sdcardRecordClazz = getSDCardRecordClass(env);
	struct DateTimeClass dateTimeClass = getDateTimeClass(env);

	struct CmdPlayBackReq req;
	req.chn = env->GetIntField(record, sdcardRecordClazz.channel);
	req.eventType = (EnumRecordTYPE)env->GetIntField(record, sdcardRecordClazz.recordType);
	req.timelen = env->GetIntField(record, sdcardRecordClazz.timeLen);

	jobject timeFrom = env->GetObjectField(record, sdcardRecordClazz.from);
	req.tRec.year = env->GetIntField(timeFrom, dateTimeClass.year);
	req.tRec.mon= env->GetIntField(timeFrom, dateTimeClass.mon);
	req.tRec.mday= env->GetIntField(timeFrom, dateTimeClass.day);
	req.tRec.hour= env->GetIntField(timeFrom, dateTimeClass.hour);
	req.tRec.min= env->GetIntField(timeFrom, dateTimeClass.min);
	req.tRec.sec= env->GetIntField(timeFrom, dateTimeClass.sec);

	return pSess->stopSdcardRecordVideo(req);
}

/*
 * Class:     spider_szc_JNI
 * Method:    SDRecordGetRecordPlan
 * Signature: (ILjava/util/List;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_sdcardRecordGetRecordPlan(JNIEnv *env, jclass clazz, jint connect, jint chn, jobject list){
	P2pSession *pSess = (P2pSession*)connect;
	struct CmdGetRecordPlanListResp *resp;
	struct SDCardRecordPlanClass sdcardRecordPlanClass = getSDCardRecordPlanClass(env);
	struct DateTimeClass dateTimeClass = getDateTimeClass(env);
	int ret = DAP2pGetRecordInf(pSess->GetConnectionHandle(), chn, &resp);
	if (ret != 0){
		return ret;
	}

	jclass listClazz = env->GetObjectClass(list);
	jmethodID listAdd = env->GetMethodID(listClazz, "add", "(Ljava/lang/Object;)Z");
	
	for (int i=0; i<resp->n_item; i++){
		jobject fromTime = env->NewObject(dateTimeClass.clazz, dateTimeClass.constructor);
		env->SetIntField(fromTime, dateTimeClass.hour, resp->items[i].from.hour);
		env->SetIntField(fromTime, dateTimeClass.min, resp->items[i].from.minute);
		env->SetIntField(fromTime, dateTimeClass.sec, resp->items[i].from.second);

		jobject toTime = env->NewObject(dateTimeClass.clazz, dateTimeClass.constructor);
		env->SetIntField(toTime, dateTimeClass.hour, resp->items[i].to.hour);
		env->SetIntField(toTime, dateTimeClass.min, resp->items[i].to.minute);
		env->SetIntField(toTime, dateTimeClass.sec, resp->items[i].to.second);

		jobject plan = env->NewObject(sdcardRecordPlanClass.clazz, sdcardRecordPlanClass.init);
		env->SetIntField(plan, sdcardRecordPlanClass.channel, resp->items[i].chn);
		env->SetIntField(plan, sdcardRecordPlanClass.planAction, (int)resp->items[i].doType);
		env->SetObjectField(plan, sdcardRecordPlanClass.from, fromTime);
		env->SetObjectField(plan, sdcardRecordPlanClass.to, toTime);

		int weekInfo = 0;
		for (int j=0; j<resp->items[i].n_item; j++){
			int week = (int)resp->items[i].items[j];
			weekInfo = (1<<week) | weekInfo;
		}
		env->SetIntField(plan, sdcardRecordPlanClass.weekInfo, weekInfo);
		
		env->CallBooleanMethod(list, listAdd, plan);
	}
	if (resp != NULL){
		free(resp);
	}
	return 0;
}

/*
 * Class:     spider_szc_JNI
 * Method:    SDRecordSetRecordPlan
 * Signature: (ILjava/util/List;)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_sdcardRecordSetRecordPlan(JNIEnv *env, jclass clazz, jint connect, jobject list){
	P2pSession *pSess = (P2pSession*)connect;
	struct SDCardRecordPlanClass sdcardRecordPlanClass = getSDCardRecordPlanClass(env);
	struct DateTimeClass dateTimeClass = getDateTimeClass(env);
	struct CmdSetRecordPlanListResp req;

	jclass listClazz = env->GetObjectClass(list);
	jmethodID listSize = env->GetMethodID(listClazz, "size", "()I");
	jmethodID listGet = env->GetMethodID(listClazz, "get", "(I)Ljava/lang/Object;");
	req.n_item  = env->CallIntMethod(list, listSize);

	for (int i=0; i<req.n_item; i++){
		jobject plan = env->CallObjectMethod(list, listGet, i);

		req.items[i].chn = env->GetIntField(plan, sdcardRecordPlanClass.channel);
		req.items[i].doType = (EnumPlanType)env->GetIntField(plan, sdcardRecordPlanClass.planAction);

		jobject fromTime = env->GetObjectField(plan, sdcardRecordPlanClass.from);
		req.items[i].from.hour = env->GetIntField(fromTime, dateTimeClass.hour);
		req.items[i].from.minute = env->GetIntField(fromTime, dateTimeClass.min);
		req.items[i].from.second = env->GetIntField(fromTime, dateTimeClass.sec);

		jobject toTime = env->GetObjectField(plan, sdcardRecordPlanClass.to);
		req.items[i].to.hour = env->GetIntField(toTime, dateTimeClass.hour);
		req.items[i].to.minute = env->GetIntField(toTime, dateTimeClass.min);
		req.items[i].to.second = env->GetIntField(toTime, dateTimeClass.sec);

		int weekInfo = env->GetIntField(plan, sdcardRecordPlanClass.weekInfo);
		req.items[i].n_item = 0;
		for (int j=0; j<7; j++){
			int week = (1<<j);
			if ((weekInfo & week) == week){
				req.items[i].items[req.items[i].n_item++] = (WeekInf)j;
			}
		}
	}
	
	return DAP2pSetRecordInf(pSess->GetConnectionHandle(), &req);
}

/*
 * Class:     spider_szc_JNI
 * Method:    SDRecordFormatSDCard
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_spider_szc_JNI_sdcardRecordFormatSDCard(JNIEnv *env, jclass clazz, jint connect){
	P2pSession *pSess = (P2pSession*)connect;
	return DAP2pSetSDKFormat(pSess->GetConnectionHandle());
}

/*
 * Class:     spider_szc_JNI
 * Method:    calcCRC16
 * Signature: ([B)[B
 */
JNIEXPORT jbyteArray JNICALL Java_spider_szc_JNI_calcCRC16(JNIEnv *env, jclass jclazz, jbyteArray data, jint len){
	unsigned char da;
	const unsigned short crc_ta[16] = { 			  /* CRC余式表 */	
		0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,   
		0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,   
	};
	unsigned char *ptr = (unsigned char*)env->GetByteArrayElements(data, 0);
//	unsigned int len = (unsigned int)env->GetArrayLength(data);
	unsigned short crc = 0;
	while(len--!=0) 
	{	
		da = crc >> 12; //((unsigned char)(crc/256))/16;	  /* 暂存CRC的高四位 */   
		crc <<= 4;					 /* CRC右移4位，相当于取CRC的低12位）*/   
		crc ^= crc_ta[da^(*ptr >> 4)];	   /* CRC的高4位和本字节的前半字节相加后查表计算CRC，然后加上上一次CRC的余数 */   
		da = crc >> 12; //((uchar)(crc/256))/16;	  /* 暂存CRC的高4位 */	 
		crc <<= 4;					 /* CRC右移4位， 相当于CRC的低12位） */   
		crc ^= crc_ta[da ^ (*ptr & 0x0f)];	 /* CRC的高4位和本字节的后半字节相加后查表计算CRC，然后再加上上一次CRC的余数 */   
		ptr++;	 
	}
	short crc_array[1] =  {crc};
	jbyteArray resultByteArray = env->NewByteArray(2);
	env->SetByteArrayRegion(resultByteArray, 0, 2, (jbyte*)crc_array);
	return resultByteArray; 
}

#endif

