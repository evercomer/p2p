#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
//#include <error.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/ioctl.h>

#include "sender.h"
#include "mediatyp.h"

#define H264_STARTCODE_LEN      4 /* 00 00 00 01 */
//#define dbg_msg printf

void InitSender(RTSPSENDER *pSndr, MEDIATYPE fmt, SENDERTYPE st, unsigned int ssrc)
{
	memset(pSndr, 0, sizeof(RTSPSENDER));

	INIT_LIST_HEAD(&pSndr->target_list);
	pthread_mutex_init(&pSndr->mutex, NULL);

	pSndr->ssrc = ssrc;

	switch(fmt)
	{
	case MEDIATYPE_AUDIO_G711A:
		pSndr->pt = RTP_PT_ALAW;
		pSndr->mt = MEDIATYPE_AUDIO_G711A;
		break;
	case MEDIATYPE_AUDIO_ADPCM_DVI4:
	case MEDIATYPE_AUDIO_ADPCM_IMA:
		pSndr->pt = RTP_PT_ADPCM;
		pSndr->mt = fmt;
		break;
	case MEDIATYPE_AUDIO_G726_40:
	case MEDIATYPE_AUDIO_G726_32:
	case MEDIATYPE_AUDIO_G726_24:
	case MEDIATYPE_AUDIO_G726_16:
		pSndr->pt = RTP_PT_G726;
		pSndr->mt = fmt;
		break;
	case MEDIATYPE_VIDEO_H264:
		pSndr->pt = RTP_PT_H264;
		pSndr->mt = MEDIATYPE_VIDEO_H264;
		break;
	default:
		pSndr->mt = MEDIATYPE_INVALID;
		fprintf(stderr, "*** InitSender: Unknown media format %d\n", fmt);
		//exit(-1);
	}

	pSndr->st = st;
	switch(st)
	{
	case SENDERTYPE_RTSP:
		pSndr->hdr_len = (pSndr->pt == RTP_PT_H264) ? RTSP_RTPHDR_FUA_LEN : RTSP_RTPHDR_LEN;
		break;
	case SENDERTYPE_RTP:
		pSndr->hdr_len = (pSndr->pt == RTP_PT_H264) ? RTP_HDR_FUA_LEN : RTP_HDR_LEN;
		break;
	}
}

void UninitSender(RTSPSENDER *pSender)
{
	pthread_mutex_destroy(&pSender->mutex);
	if(pSender->buffer) free(pSender->buffer);
	pSender->buffer = NULL;
}

//
int SenderSend(IN RTSPSENDER * pSender)
{
	RTP_TARGETHOST_S* pTgt;
	struct list_head *pos, *q;
	int rlt;
	struct iovec v[3];
#ifdef __SIMULATE__
const char cprog[] = "-\\|/";
static int ci = 0;
#endif

	v[0].iov_base = &pSender->rtpHdr;
	memcpy(&v[1], pSender->iov, sizeof(pSender->iov));
	list_for_each_safe(pos, q, &(pSender->target_list) )
	{
		pTgt= list_entry(pos, TARGETHOST, target_list);
		if(pTgt->hostState == TARGETSTATE_Paused) continue;
		if (pTgt->hostState == TARGETSTATE_Sending)
		{
			switch( pTgt->trans_type )
			{
				case TRANSPORT_RTSP:
					if(pTgt->sock > 0)
					{
						if(pSender->buffer)
							rlt = send(pTgt->sock, pSender->buffer, pSender->data_size, MSG_NOSIGNAL);
						else
						{
							v[0].iov_len = pSender->hdr_len;
							pSender->rtspHdr.interleaved_id = pTgt->interleaved_id;

							struct msghdr h;

							h.msg_control = NULL;
							h.msg_controllen = 0;
							h.msg_iov = v;
							h.msg_iovlen = 3;
							h.msg_name = &pTgt->remote_addr;
							h.msg_namelen = sizeof(struct sockaddr);
							rlt = sendmsg(pTgt->sock, &h, MSG_NOSIGNAL);
						}
						if(rlt < 0)
						{
							int err = errno;
							if(errno == EAGAIN || err == EWOULDBLOCK)
							{
								if(MEDIATYPE_IS_VIDEO(pSender->mt))
								{
									dbg_msg("busy, wait for i-frame...\n");
									pTgt->hostState = TARGETSTATE_REQ_IFrame;
								}
							}
							else
								perror("send media");
						}
					}
					break;

				case TRANSPORT_UDP:
					{
						struct msghdr h;

						v[0].iov_len = pSender->hdr_len;
						h.msg_control = NULL;
						h.msg_controllen = 0;
						h.msg_iov = v;
						h.msg_iovlen = 3;
						h.msg_namelen = sizeof(struct sockaddr);
						h.msg_name = &pTgt->remote_addr;
						rlt = sendmsg(pSender->sock, &h, 0);
						if(rlt < 0) perror("sendmsg(udp)");
						else if(rlt == 0) fprintf(stderr, "\nudp buffer problem....\n");
#ifdef __SIMULATE__
						static struct sockaddr_in sa_tgt;
						static int s = 0;
						if(s == 0) {
							s = pSender->sock;
							memcpy(&sa_tgt, &pTgt->remote_addr, sizeof(sa_tgt));
						}
						if(s != pSender->sock || memcmp(&sa_tgt, &pTgt->remote_addr, sizeof(sa_tgt)))
							fprintf(stderr, "\ndata corrupted.\n");
						printf("\r%d %c", v[0].iov_len+v[1].iov_len+v[1].iov_len, cprog[ci]); fflush(stdout); ci = (ci+1)%4;
#endif
					}
					break;
			}
		}
	}
	
	return 0;
}

static void PackRTPHdr(RTP_HDR_S *pRtpHdr, RTSPSENDER *pSender, DWORD ts_inc, DWORD marker)
{
	RTP_HDR_SET_VERSION(pRtpHdr, RTP_VERSION);
	RTP_HDR_SET_P(pRtpHdr, 0);
	RTP_HDR_SET_X(pRtpHdr, 0);
	RTP_HDR_SET_CC(pRtpHdr, 0);

	RTP_HDR_SET_M(pRtpHdr, marker);
	RTP_HDR_SET_PT(pRtpHdr, pSender->pt);

	RTP_HDR_SET_SEQNO(pRtpHdr, htons(pSender->last_sn));
	RTP_HDR_SET_TS(pRtpHdr, htonl(ts_inc));
	RTP_HDR_SET_SSRC(pRtpHdr, htonl(pSender->ssrc));
	pRtpHdr->pt = pSender->pt;
}

/* 调用者分片 */
void PackRTPFuA(IN RTSPSENDER *pSender, int ts_inc, BOOL start, BOOL end, unsigned char c, SLICE *pSlice)
{
	memcpy(pSender->iov, pSlice->iov, sizeof(pSender->iov));
	PackRTPHdr(&pSender->rtpFua.rtpHdr, pSender, ts_inc, end);
	pSender->rtpFua.indicator = (c & 0x60) | 28;
	pSender->rtpFua.fu_hdr = (start?0x80:0) | (end?0x40:0) | (c&0x1f);

	pSender->last_sn ++;
}

//
//输入不含同步字节
void PackRTPFuAonRTSP(RTSPSENDER *pSender, unsigned int ts_inc, BOOL start, BOOL end, unsigned char c, SLICE *pSlice)
{
	pSender->rtspFua.dollar = '$';
	pSender->rtspFua.interleaved_id = 0;	//Filled when send
	pSender->rtspFua.len = htons(pSlice->iov[0].iov_len + pSlice->iov[1].iov_len + sizeof(RTP_HDR_FUA));

	memcpy(pSender->iov, pSlice->iov, sizeof(pSender->iov));

	PackRTPHdr(&pSender->rtspFua.rtpFua.rtpHdr, pSender, ts_inc, end);
	pSender->rtspFua.rtpFua.indicator = (c & 0xE0) | 28;
	pSender->rtspFua.rtpFua.fu_hdr = (start?0x80:0) | (end?0x40:0) | (c&0x1f);

	pSender->last_sn++;
}


/*输入带有同步头*/
void PackRTP(IN RTSPSENDER * pSender, sint32 ts_inc, DWORD marker, SLICE* pSlice)
{
	if ( pSender->pt == RTP_PT_H264 )
	{
		pSender->iov[0].iov_base = pSlice->iov[0].iov_base + H264_STARTCODE_LEN;
		pSender->iov[0].iov_len = pSlice->iov[0].iov_len - H264_STARTCODE_LEN;
		pSender->iov[1].iov_base = pSlice->iov[1].iov_base;
		pSender->iov[1].iov_len = pSlice->iov[1].iov_len;
	}
	else
	{
		memcpy(pSender->iov, pSlice->iov, sizeof(pSender->iov));
		if(pSender->st == SENDERTYPE_RTP)
		{
			ts_inc *= 8;
		}
	}

	PackRTPHdr(&pSender->rtpHdr, pSender, ts_inc, marker);

	pSender->last_sn++;
}

//输入含同步串
static void PackRTPonRTSP(RTSPSENDER *pSender, DWORD ts_inc, DWORD marker, SLICE* pSlice)
{
	pSender->rtspHdr.dollar = '$';
	pSender->rtspHdr.interleaved_id = 0;	//Filled when send
	pSender->rtspHdr.len = htons(pSlice->iov[0].iov_len + pSlice->iov[1].iov_len + sizeof(RTP_HDR_S) - 4);

	if ( pSender->pt == RTP_PT_H264 )
	{
		pSender->iov[0].iov_base = pSlice->iov[0].iov_base + H264_STARTCODE_LEN;
		pSender->iov[0].iov_len = pSlice->iov[0].iov_len - H264_STARTCODE_LEN;
		pSender->iov[1].iov_base = pSlice->iov[1].iov_base;
		pSender->iov[1].iov_len = pSlice->iov[1].iov_len;
	}
	else
	{
		memcpy(pSender->iov, pSlice->iov, sizeof(pSender->iov));
		ts_inc *= 8;
	}

	PackRTPHdr(&pSender->rtspHdr.rtpHdr, pSender, ts_inc, marker);

	pSender->last_sn++;
}

/*输入pPayload是带有同步头的*/
void SenderPackMedia(IN RTSPSENDER * pSender, sint32 pts, DWORD marker, SLICE* pSlice)
{
	switch( pSender->st )
	{
	case SENDERTYPE_RTP:
		PackRTP(pSender, pts, marker, pSlice);
		break;
	case SENDERTYPE_RTSP:
		PackRTPonRTSP(pSender, pts, marker, pSlice);
		break;
	default:
		break;
	}
}

