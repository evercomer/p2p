#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ctype.h>

#include "ctp.h"
#include "rtsp_parse.h"
#include "rtspcmd.h"
#include "rtspsvc.h"
#include "sender.h"
#include "p2psess.h"

extern int g_RtpSock;

#if 0
#include "lutil.h"
char *ParasetBase64(int vstrm, char *buff)
{
	extern unsigned char *get_sprop_ps(EVSTREAMID vid);
	unsigned char *sps = get_sprop_ps(vstrm);
	char *s = buff;
	int first = 1;

	while(*sps)
	{
		if(first)
			first = 0;
		else
		{
			*s++ = ',';
		}
		s += lutil_b64_ntop(sps+1, *sps, s, 200);
		sps += *sps + 1;
	}
	*s = '\0';
	return buff;
}
#endif

void RtspSenderInit(P2pSession *pSess)
{
	if(!pSess->m_pRtspSender)
	{
		int st;
		RTSPSENDER *pSs = (RTSPSENDER*)malloc(sizeof(RTSPSENDER)*SENDERTYPE_MAX);
		for(st=0; st<SENDERTYPE_MAX; st++)
		{
			InitSender(&pSs[st], MEDIATYPE_VIDEO_H264, (SENDERTYPE)st, (unsigned int)pSess);
			pSs[st].sock = g_RtpSock;
		}
		pSess->m_pRtspSender = pSs;
	}
}

void RtspSenderDestroy(P2pSession *pSess)
{
	if(pSess->m_pRtspSender)
	{
		int st;
		RTSPSENDER *pSenders = (RTSPSENDER*)pSess->m_pRtspSender;
		for(st=0; st<SENDERTYPE_MAX; st++)
			UninitSender(&pSenders[st]);
		pSess->m_pRtspSender = NULL;
	}
}

static void VideoWaitIFrame(RTSPSENDER *pSender)
{
	SENDERTYPE sd;
	RTP_TARGETHOST_S* pTargetHost = NULL;
	struct list_head *pTargetHostPos;

	list_for_each(pTargetHostPos, &(pSender->target_list))
	{
		pTargetHost = list_entry(pTargetHostPos, RTP_TARGETHOST_S , target_list);
		if ( pTargetHost->hostState == TARGETSTATE_REQ_IFrame )
		{
			pTargetHost->hostState = TARGETSTATE_Sending;
		}
	}
}

void PackVideoAndSend(RTSPSENDER *pSender, const P2PFRAMEINFO *pFi)
{
	uint32 rpts = pFi->ts * 90;		//pts for rtsp
	extern void PackRTPFuAonRTSP(RTSPSENDER *pSender, unsigned int ts_inc, BOOL start, 
			BOOL end, unsigned char c, SLICE *pSlice);
	extern void PackRTPFuA(RTSPSENDER *pSender, int ts_inc, BOOL start, 
			BOOL end, unsigned char c, SLICE *pSlice);

	SLICE slice;
	char *ptr;
	int len;
	BYTE byt;

	ptr = (char*)pFi->pFrame + 4;
	byt = ptr[0];
	ptr++;
	len = pFi->len - 5;

	BOOL bStart = TRUE;
	slice.iov[1].iov_len = 0;
	do {
		int sl = len > 1400 ? 1400 : len;
		slice.iov[0].iov_base = ptr;
		slice.iov[0].iov_len = sl;

		switch(pSender->st)
		{
			case SENDERTYPE_RTP:
				PackRTPFuA(pSender, rpts, bStart, len == sl, byt, &slice);
				SenderSend(pSender);
				break;
			case SENDERTYPE_RTSP:
				PackRTPFuAonRTSP(pSender, rpts, bStart, len == sl, byt, &slice);
				SenderSend(pSender);
				break;
		}

		bStart = FALSE;
		len -= sl;
		ptr += sl;
	} while(len);
}
void RtspSenderSend(P2pSession *pSess, P2PFRAMEINFO *pFi)
{
	RTSPSENDER *pSenders = (RTSPSENDER*)pSess->m_pRtspSender;
	if(pSenders)
	{
		int st;

		for(st = 0; st < SENDERTYPE_MAX; st++)
		{
			RTSPSENDER *pSender = &pSenders[st];
			if(!list_empty(&pSender->target_list))
			{
				pthread_mutex_lock(&pSender->mutex);
				if(MEDIATYPE_IS_VIDEO(pFi->mt) && pFi->isKeyFrame) 
					VideoWaitIFrame(pSender);
				PackVideoAndSend(pSender, pFi);
				pthread_mutex_unlock(&pSender->mutex);
			}
		}
	}
}
//----------------------------------------------------------------------------

int RTSP_Make_RespHdr(CLIENT *pclt, char *buf, int err)
{
	char *pTmp = buf;
	pTmp += sprintf(pTmp, "RTSP/1.0 %d %s\r\n", err, RTSP_Get_Status_Str(err));
	pTmp += sprintf(pTmp, "CSeq: %u\r\n", pclt->cseq);
	pTmp += sprintf(pTmp, "Server: p2p embbed server 0.1\r\n");
	return pTmp - buf;
}

int RTSP_Send_Reply(CLIENT *pclt, char *buf, int err, const char *addon , int simple)
{
	char *pTmp = buf;
	if (simple == 1)
	{
		pTmp += RTSP_Make_RespHdr(pclt, buf, err);
	}

	if ( addon )
	{
		pTmp += sprintf(pTmp, "%s", addon );
	}

	if (simple)
		strcat( buf, "\r\n");

	dbg_msg(">>>>>>>>>>>>\r\n%s", buf);
	//send(pclt->sock, buf, strlen(buf), 0);
	write(pclt->sock, buf, strlen(buf));
	return 0;
}


int RTSP_Handle_Options(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body)
{
    int i = RTSP_Make_RespHdr(pclt, body, 200);
    sprintf(body + i , "Public: DESCRIBE, SET_PARAMETER, SETUP, TEARDOWN, PLAY\r\n\r\n");
    RTSP_Send_Reply(pclt, body, 200, NULL, 0);
    return 0;
}

//txt like: sn/video or sn/av or sn/audio
static BOOL ParseUri(const char *txt, char *sn, int *medias)
{
	const char *slash;
	P2pSession *pSess;
	if( txt && txt[0] && (slash = strchr(txt, '/')) ) {
		strncpy(sn, txt, slash - txt);
		sn[slash - txt] = '\0';

		txt = slash + 1;
		if(strncmp(txt, "av", 2) == 0)
		{
			*medias = (1<<MEDIAINDEX_VIDEO)|(1<<MEDIAINDEX_AUDIO);
		}
		else if(strcmp(txt, "audio") == 0)
			*medias = (1<<MEDIAINDEX_AUDIO);
		else
			*medias = (1<<MEDIAINDEX_VIDEO);
		return TRUE;
	}
	return FALSE;
}

MEDIATYPE GetAudioType()
{
	return MEDIATYPE_AUDIO_G711A;
}

int buildSdp(char *buff, const char *szserver, int port, const char *sn, MEDIATYPE vType, MEDIATYPE aType)
{
	/* SDP body */
	char *pTemp2 = buff;
	pTemp2 += sprintf(pTemp2,"v=0\r\n");
	pTemp2 += sprintf(pTemp2,"o=StreamingServer 3331435948 1116907222000 IN IP4 %s\r\n", szserver);
	pTemp2 += sprintf(pTemp2,"s=h264.mp4\r\n");
	pTemp2 += sprintf(pTemp2, "i=p2p client embbed server\r\n");
	pTemp2 += sprintf(pTemp2, "c=IN IP4 %s\r\n", szserver);
	pTemp2 += sprintf(pTemp2, "t=0 0\r\n");
	pTemp2 += sprintf(pTemp2, "a=recvonly\r\n");

	//media=video
	if(vType == MEDIATYPE_VIDEO_H264)
	{
		pTemp2 += sprintf(pTemp2,"m=video 0 RTP/AVP 96\r\n");
		pTemp2 += sprintf(pTemp2,"a=control:rtsp://%s:%d/%s/video\r\n", szserver, port, sn);
		pTemp2 += sprintf(pTemp2,"a=rtpmap:96 H264/90000\r\n");

		pTemp2 += sprintf(pTemp2, "a=fmtp:96 packetization-mode=1\r\n");
		//pTemp2 += sprintf(pTemp2,"a=fmtp:96 packetization-mode=1; sprop-parameter-sets=%s\r\n", 
		//		ParasetBase64((medias&0xFFFF)-1, sdpMsg+3800));
		//pTemp2 += sprintf(pTemp2, "a=framesize:96 1280-720\r\n");
	}

	//media=audio
	if(aType != MEDIATYPE_INVALID)
	{
		int pt;
		switch(aType)
		{
		case MEDIATYPE_AUDIO_G711A: pt = RTP_PT_ALAW; break;
		case MEDIATYPE_AUDIO_ADPCM_DVI4: pt = RTP_PT_ADPCM; break;
		case MEDIATYPE_AUDIO_G726_16: 
		default:
			pt = RTP_PT_G726; 
			break;
		}
		pTemp2 += sprintf(pTemp2, "m=audio 0 RTP/AVP %d\r\n", pt);
		pTemp2 += sprintf(pTemp2, "a=control:rtsp://%s:%d/%s/audio\r\n", szserver, port, sn);
		switch(GetAudioType())
		{
		case MEDIATYPE_AUDIO_G711A: 
			pTemp2 += sprintf(pTemp2, "a=rtpmap:%d pcma/8000\r\n", pt);
			break;

		case MEDIATYPE_AUDIO_ADPCM_DVI4: 
			pTemp2 += sprintf(pTemp2, "a=rtpmap:%d dvi4/8000\r\n", pt);
			break;
		case MEDIATYPE_AUDIO_G726_40: 
			pTemp2 += sprintf(pTemp2, "a=rtpmap:%d g726-40/8000\r\n", pt);
			break;
		case MEDIATYPE_AUDIO_G726_32: 
			pTemp2 += sprintf(pTemp2, "a=rtpmap:%d g726-32/8000\r\n", pt);
			break;
		case MEDIATYPE_AUDIO_G726_24: 
			pTemp2 += sprintf(pTemp2, "a=rtpmap:%d g726-24/8000\r\n", pt);
			break;
		case MEDIATYPE_AUDIO_G726_16: 
		default:
			pTemp2 += sprintf(pTemp2, "a=rtpmap:%d g726-16/8000\r\n", pt);
			break;
		}

		pTemp2 += sprintf(pTemp2, "a=ptime:40\r\n");
	}

	strcpy(pTemp2, "\r\n");
	pTemp2 += 2;
	return pTemp2 - buff;
}

int RTSP_Handle_Describe(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body)
{
	char szserver[64], object[128], sn[32];
	int  port;
	char*   pTemp;
	char *sdpMsg;
	int medias = 0;

	if (!RTSP_Parse_Url(preq->uri, szserver, &port, object, NULL, NULL)) {
		RTSP_Send_Reply(pclt, body, 400, NULL, 1);
		return 0;
	}
	if(! ParseUri(object, sn, &medias) || !P2pSession::FindP2pSession(sn) ) medias = -1;

	if(medias < 0)
	{
		RTSP_Send_Reply(pclt, body, 400, NULL, 1);
		return 0;
	}

	/*Accept: application/sdp*/
	if(!popt->accept || !strstr(popt->accept, "application/sdp"))
	{
		RTSP_Send_Reply(pclt, body, 551, NULL, 1);	/* Option not supported */
		return 0;
	}


	pTemp = body + RTSP_Make_RespHdr(pclt, body, 200);
	pTemp += sprintf(pTemp, "Content-Type: application/sdp\r\n");
//	pTemp += sprintf(pTemp,"Content-Base: rtsp://%s/%s/\r\n", szserver, sn);

	/* SDP body */
	sdpMsg = (char*)malloc(4000);
	int sdp_len = buildSdp(sdpMsg, szserver, port, sn, MEDIATYPE_VIDEO_H264, MEDIATYPE_INVALID);

	pTemp += sprintf(pTemp, "Content-length: %d\r\n\r\n", sdp_len);     
	strcat(pTemp, sdpMsg);
	RTSP_Send_Reply(pclt, body, 200, NULL, 0);
	free(sdpMsg);
	return 0;
}

/*
in 1.mp4 Video is Track2
C --> S
    SETUP rtsp://10.71.147.222/1.mp4/trackID=2 RTSP/1.0
    CSeq: 2
    Transport: RTP/AVP;unicast;client_port=6970-6971
    x-retransmit: our-retransmit
    x-dynamic-rate: 1
    x-transport-options: late-tolerance=0.400000
    User-Agent: QuickTime/7.0.2 (qtver=7.0.2;os=Windows NT 5.1Service Pack 1)
    Accept-Language: hr-HR

S --> C
    RTSP/1.0 200 OK
    Server: DSS/5.0.1.1 (Build/464.1.1; Platform/Win32; Release/5; )
    Cseq: 2
    Last-Modified: Tue, 24 May 2005 04:00:22 GMT
    Cache-Control: must-revalidate
    Session: 117381456211096
    Date: Mon, 22 May 2006 08:41:38 GMT
    Expires: Mon, 22 May 2006 08:41:38 GMT
    Transport: RTP/AVP;unicast;client_port=6970-6971;source=10.71.147.222;server_port=6970-6971;ssrc=00002861
    x-Transport-Options: late-tolerance=0.400000
    x-Retransmit: our-retransmit
    x-Dynamic-Rate: 1;rtt=15

=========================================
in 1.mp4  Audio is Track6
C --> S
    SETUP rtsp://10.71.147.222/1.mp4/trackID=6 RTSP/1.0
    CSeq: 3
    Transport: RTP/AVP;unicast;client_port=6972-6973   (KEY) 获取rtp接收端口 or RTP/AVP/TCP;interleaved=0-1
    x-retransmit: our-retransmit
    x-dynamic-rate: 1
    x-transport-options: late-tolerance=0.400000
    Session: 117381456211096
    User-Agent: QuickTime/7.0.2 (qtver=7.0.2;os=Windows NT 5.1Service Pack 1)
    Accept-Language: hr-HR

S --> C
    RTSP/1.0 200 OK
    Server: DSS/5.0.1.1 (Build/464.1.1; Platform/Win32; Release/5; )
    Cseq: 3
    Session: 117381456211096
    Last-Modified: Tue, 24 May 2005 04:00:22 GMT
    Cache-Control: must-revalidate
    Date: Mon, 22 May 2006 08:41:38 GMT
    Expires: Mon, 22 May 2006 08:41:38 GMT
    Transport: RTP/AVP;unicast;client_port=6972-6973;source=10.71.147.222;server_port=6970-6971;ssrc=00003654		or	RTP/AVP/TCP;interleaved=0-1
    x-Transport-Options: late-tolerance=0.400000
    x-Retransmit: our-retransmit
    x-Dynamic-Rate: 1

1) 服务器根据Track信息， 获取对应的rtp请求传输端口
2) 创建rtp发送实例
3) 发送回复信息
*/
int RTSP_Handle_Setup(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body)
{
	char* pTemp;
	char object[255], server[64], real_path[200], sn[32];
	int svrport;
	int medias, mediaIndex;
	P2pSession *pP2pSess;


	if (!RTSP_Parse_Url(preq->uri, server, &svrport, object, NULL, NULL)) {   
		dbg_msg("SETUP: Invalid URL\n");
		RTSP_Send_Reply(pclt, body, 400, 0, 1);	/* bad request */
		return 0;
	}

	if( !ParseUri(object, sn, &medias) || !(pP2pSess = P2pSession::FindP2pSession(sn)) ) {
		dbg_msg("SETUP: Invalid Session\n");
		RTSP_Send_Reply(pclt, body, 400, NULL, 1);	/* Not Acceptable */
		return 0;
	}
	mediaIndex = (medias&(1<<MEDIAINDEX_VIDEO))?MEDIAINDEX_VIDEO:MEDIAINDEX_AUDIO;

	int transport = -1;
	int client_port = 0;
	int chnid = 0;
	if(popt->transport) {
		char *psave1;
		pTemp = strtok_r(popt->transport, ",", &psave1);
		while(pTemp) {
			if(strncmp(popt->transport, "RTP/AVP", 7) == 0) {
				pTemp = strtok(pTemp, ";");
				if(strcmp(pTemp, "RTP/AVP/TCP") == 0) {
					transport = TRANSPORT_RTSP;
					while((pTemp = strtok(NULL, ";")) != NULL) {
						if(strncmp(pTemp, "interleaved=", 12) == 0)
							chnid = atoi(pTemp + 12);
					}
					break;
				}
				else if(strcmp(pTemp, "RTP/AVP") == 0 || strcmp(pTemp, "RTP/AVP/UDP") == 0) {
					while((pTemp = strtok(NULL, ";")) != NULL) {
						if(strcmp(pTemp, "unicast") == 0)
						{
							transport = TRANSPORT_UDP;
						}
						else if(strcmp(pTemp, "multicast") == 0)
						{
							//transport = TRANSPORT_MULTICAST;
						}
						else if(strncmp(pTemp, "client_port=", 12) == 0)
						{
							client_port = atoi(pTemp + 12);
						}
					} 
				}
				break;
			}
			pTemp = strtok_r(NULL, ",", &psave1);
		}
	}
	if(transport < 0 || transport != TRANSPORT_RTSP && client_port <= 0)
	{
		RTSP_Send_Reply(pclt, body, 406, "Require: Transport settings of rtp", 1);
		return 0;
	}


	SESSION *pSess;
	if(popt->session) 
	{
		pSess = FindClientSession(pclt, popt->session);
		if(!pSess)
		{
			RTSP_Send_Reply(pclt, body, 454, NULL, 1);
			return 0;
		}
	}
	else
		pSess = AddSession(pclt, 0, (TRANSPORTTYPE)transport);

	pSess->client_port[mediaIndex] = client_port;
	pSess->chnid[mediaIndex] = chnid;
	pSess->requested_medias |= medias;
	pSess->pP2pSess = pP2pSess;


	pTemp = body;
	pTemp += RTSP_Make_RespHdr(pclt, body, 200);
	pTemp += sprintf(pTemp,"Session: %s; timeout=60\r\n", pSess->sessId);

	switch(transport)
	{
	case TRANSPORT_RTSP:
		pTemp += sprintf(pTemp, "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d;mode=play\r\n\r\n", chnid, chnid+1);
		break;
	case TRANSPORT_UDP:
		pTemp += sprintf(pTemp, "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n\r\n", 
				client_port, client_port+1, SERVER_RTP_PORT, SERVER_RTP_PORT+1);//, 0x4e87);//RTP_DEFAULT_SSRC);
		break;
	}

	//pclt->sess_state = RTSP_STATE_READY;

	RTSP_Send_Reply(pclt, body, 200, NULL, 0);

	return 0;
}
/*
C --> S
    PLAY rtsp://10.71.147.222/1.mp4 RTSP/1.0
    CSeq: 4
    Range: npt=0.000000-415.063333 (KEY)
    x-prebuffer: maxtime=2.000000
    x-transport-options: late-tolerance=10
    Session: 117381456211096
    User-Agent: QuickTime/7.0.2 (qtver=7.0.2;os=Windows NT 5.1Service Pack 1)

S --> C
    RTSP/1.0 200 OK
    Server: DSS/5.0.1.1 (Build/464.1.1; Platform/Win32; Release/5; )
    Cseq: 4
    Session: 117381456211096
    Range: npt=0.00000-415.06332 (KEY)
    RTP-Info: url=trackID=2;seq=32136;rtptime=2180,url=trackID=6;seq=6075;rtptime=26770
*/

int RTSP_Handle_Play(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body)
{
	char *semicolon;
	SESSION *pSess;

	char szserver[64], object[128], sn[32];
	int  port;
	int medias = 0;

	/* get SESSION object, which is create at SETUP */
       	semicolon = strchr(popt->session, ';');
	if(semicolon) *semicolon = '\0';
	dbg_msg("popt->session = %d\n", popt->session);
       	pSess = FindClientSession(pclt, popt->session);

	if(!pSess)
	{
		RTSP_Send_Reply(pclt, body, 454, 0, 1);
		return 0;
	}

	/* get P2pSession */
	if (!RTSP_Parse_Url(preq->uri, szserver, &port, object, NULL, NULL)) {
		RTSP_Send_Reply(pclt, body, 400, NULL, 1);
		return 0;
	}
	if(! ParseUri(object, sn, &medias) )
	{
		RTSP_Send_Reply(pclt, body, 400, NULL, 1);
		return 0;
	}
	RtspSenderInit(pSess->pP2pSess);


	int media;
	for(media=0; media < MEDIAINDEX_MAX; media++)
	{
		if(!(pSess->requested_medias & (1<<media)))
			continue;

		RTP_TARGETHOST_S *pTgt = NULL;
		struct sockaddr_in addr;
		int addr_len;
		SENDERTYPE st;

		dbg_msg("add media %d\n", media);
		switch(pSess->trans_type)
		{
			case TRANSPORT_RTSP:
				pTgt = AddTarget(popt->session, (MEDIAINDEX)media, pclt->sock);
				pTgt->interleaved_id = pSess->chnid[media];
				st = SENDERTYPE_RTSP;
				break;
			case TRANSPORT_UDP:
				addr_len = sizeof(addr);
				PA_GetPeerName(pclt->sock, (struct sockaddr*)&addr, &addr_len);
				addr.sin_port = htons(pSess->client_port[media]);
				pTgt = AddTarget(popt->session, (MEDIAINDEX)media, (unsigned long)&addr);
				st = SENDERTYPE_RTP;
				break;
		}

		if(!pTgt) {
			RTSP_Send_Reply(pclt, body, 454, NULL, 1);
			return 0;
		}
	}


	if(pSess->trans_type == TRANSPORT_RTSP)
		RTSP_Send_Reply(pclt, body, 200, NULL, 1);
	else
	{
		char *pTemp = body;
		pTemp += RTSP_Make_RespHdr(pclt, body, 200);
		//pTemp += sprintf(pTemp, "RTP-Info: url=%s;seq=%d;rtptime=%d\r\n", preq->uri, pSender->last_sn, pSender->last_ts);
		pTemp += sprintf(pTemp, "Range: npt=now-\r\n\r\n");
		//pclt->sess_state = RTSP_STATE_PLAY;
		RTSP_Send_Reply(pclt, body, 200, NULL, 0);
	}
	return 0;
}

/*
C --> S
    TEARDOWN rtsp://10.71.147.222/1.mp4 RTSP/1.0
    CSeq: 13
    Session: 117381456211096
    User-Agent: QuickTime/7.0.2 (qtver=7.0.2;os=Windows NT 5.1Service Pack 1)

S --> C
    RTSP/1.0 200 OK
    Server: DSS/5.0.1.1 (Build/464.1.1; Platform/Win32; Release/5; )
    Cseq: 13
    Session: 117381456211096
    Connection: Close
*/
int RTSP_Handle_Teardown(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body)
{
	SESSION *pSess = FindSession(popt->session);
	if(pSess) {
		RTSP_Send_Reply(pclt, body, 200, NULL, 1);
		RemoveSession(pSess);
	}
	else
		RTSP_Send_Reply(pclt, body, 454, NULL, 1);

	return 0;
}


int RTSP_Handle_Pause(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body)
{
	//RTSP_Send_Reply(pclt, body, 405, "Allow: OPTIONS, DESCRIBE, SETUP, TEARDOWN\r\n", 1);
	RTSP_Send_Reply(pclt, body, 200, NULL, 1);
	return 0;
}

int RTSP_Handle_SetParameter(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body)
{
	RTSP_Send_Reply(pclt, body, 200, NULL, 1);
	return 0;
}

int HandleRTSPCommand(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body)
{
	if( pclt->cseq + 1 != popt->cseq)
		;//return 0;
	pclt->cseq = popt->cseq;

	if(strcmp(preq->method, "OPTIONS") == 0)
		return RTSP_Handle_Options(pclt, preq, popt, body);

	if(strcmp(preq->method, "DESCRIBE") == 0)
		return RTSP_Handle_Describe(pclt, preq, popt, body);
	if(strcmp(preq->method, "SETUP") == 0)
		return RTSP_Handle_Setup(pclt, preq, popt, body);
	if(strcmp(preq->method, "PLAY") == 0)
		return RTSP_Handle_Play(pclt, preq, popt, body);
	if(strcmp(preq->method, "TEARDOWN") == 0)
		return RTSP_Handle_Teardown(pclt, preq, popt, body);
	if(strcmp(preq->method, "PAUSE") == 0)
		return RTSP_Handle_Pause(pclt, preq, popt, body);
	if(strcmp(preq->method, "SET_PARAMETER") == 0)
		return RTSP_Handle_SetParameter(pclt, preq, popt, body);
                            
	RTSP_Send_Reply(pclt, body, 405, "Allow: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n", 1);
 	return 0;
}

