#ifndef __sender_h__
#define __sender_h__

#include <netinet/in.h>
#include <pthread.h>
#include "platform_adpt.h"
#include "linux_list.h"
#include "mediatyp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAKESSRC(chn,media) (0x43270000 | ((chn&0x0F) << 4) | (media&0x0F))

#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif

/*RTP Payload type define*/
typedef enum hiRTP_PT_E
{
    RTP_PT_ULAW             = 0,        /* mu-law */
    RTP_PT_GSM              = 3,        /* GSM */
    RTP_PT_G723             = 4,        /* G.723 */
    RTP_PT_ALAW             = 8,        /* a-law */
    RTP_PT_G722             = 9,        /* G.722 */
    RTP_PT_S16BE_STEREO     = 10,       /* linear 16, 44.1khz, 2 channel */
    RTP_PT_S16BE_MONO       = 11,       /* linear 16, 44.1khz, 1 channel */
    RTP_PT_MPEGAUDIO        = 14,       /* mpeg audio */
    RTP_PT_JPEG             = 26,       /* jpeg */
    RTP_PT_H261             = 31,       /* h.261 */
    RTP_PT_MPEGVIDEO        = 32,       /* mpeg video */
    RTP_PT_MPEG2TS          = 33,       /* mpeg2 TS stream */
    RTP_PT_H263             = 34,       /* old H263 encapsulation */
                            
    //RTP_PT_PRIVATE          = 96,       
    RTP_PT_H264             = 96,       /* hisilicon define as h.264 */
    RTP_PT_G726             = 97,       /* hisilicon define as G.726 */
    RTP_PT_ADPCM            = 98,       /* hisilicon define as ADPCM */

    RTP_PT_INVALID          = 127
} RTP_PT_E;

/* total 12Bytes */
typedef struct hiRTP_HDR_S
{
//BYTE_ORDER == LITTLE_ENDIAN)
    /* byte 0 */
    WORD cc      :4;   /* CSRC count */
    WORD x       :1;   /* header extension flag */
    WORD p       :1;   /* padding flag */
    WORD version :2;   /* protocol version */

    /* byte 1 */
    WORD pt      :7;   /* payload type */
    WORD marker  :1;   /* marker bit */
#if 0
//(BYTE_ORDER == BIG_ENDIAN)
    /* byte 0 */
    WORD version :2;   /* protocol version */
    WORD p       :1;   /* padding flag */
    WORD x       :1;   /* header extension flag */
    WORD cc      :4;   /* CSRC count */
    /*byte 1*/
    WORD marker  :1;   /* marker bit */
    WORD pt      :7;   /* payload type *//
#endif


    /* bytes 2, 3 */
    WORD seqno  :16;   /* sequence number */

    /* bytes 4-7 */
    DWORD ts;            /* timestamp in ms */
  
    /* bytes 8-11 */
    DWORD ssrc;          /* synchronization source */
} RTP_HDR_S;

#define RTP_VERSION	2

#define RTP_HDR_SET_VERSION(pHDR, val)  ((pHDR)->version = val)
#define RTP_HDR_SET_P(pHDR, val)        ((pHDR)->p       = val)
#define RTP_HDR_SET_X(pHDR, val)        ((pHDR)->x       = val) 
#define RTP_HDR_SET_CC(pHDR, val)       ((pHDR)->cc      = val)

#define RTP_HDR_SET_M(pHDR, val)        ((pHDR)->marker  = val)
#define RTP_HDR_SET_PT(pHDR, val)       ((pHDR)->pt      = val)

#define RTP_HDR_SET_SEQNO(pHDR, _sn)    ((pHDR)->seqno  = (_sn))
#define RTP_HDR_SET_TS(pHDR, _ts)       ((pHDR)->ts     = (_ts))

#define RTP_HDR_SET_SSRC(pHDR, _ssrc)    ((pHDR)->ssrc  = _ssrc)

#define RTP_HDR_LEN  sizeof(RTP_HDR_S)

#define RTP_DATA_MAX_LENGTH 2048

//------------------------------------------------------------
typedef struct __tagRH {
	uint8 rc:5;
	uint8 p:1;
	uint8 ver:2;	//2
	uint8 pt;	//SR=200 RR=201
	uint16 length;
	uint32 ssrc_sender;
} RTCP_REPORTHEADER;
typedef struct __tagRB {
	uint8 fraction_lost;
	uint8 n_lost[3];
	uint32 high_sn;
	uint32 i_jitter;
	uint32 last_sr;
	uint32 delay;
} RTCP_REPORTBLOCK;

typedef struct __tagRR {
	RTCP_REPORTHEADER rh;
	RTCP_REPORTBLOCK rb[1];
} RTCP_RR;

//==========================================================================

typedef enum __tagTARGETHOST_STATE 
{
    TARGETSTATE_INIT       = 0, /*初始化状态*/
    TARGETSTATE_REQ_IFrame = 1, /*请求IFrame*/
    TARGETSTATE_Sending    = 2, /*正在发送*/   
    TARGETSTATE_Paused	    = 3,
    TARGETSTATE_BUTT
} TARGETSTATE;

/* Underlying transportion type */
typedef enum { 
	TRANSPORT_UDP, 
	TRANSPORT_RTSP,
} TRANSPORTTYPE;


typedef struct _tagTarget 
{
	struct list_head target_list;
	
	TARGETSTATE	hostState;

	TRANSPORTTYPE	trans_type;
	struct sockaddr_in	remote_addr;	//udp
	int	sock; //tcp

	int	interleaved_id;		//rtsp interleaved channel id
} RTP_TARGETHOST_S;
#define TARGETHOST	RTP_TARGETHOST_S

//--------------------------------------------------------------------------------
extern int streamIndex(const char *media);

typedef struct _rtpFUAHdr {
	RTP_HDR_S	rtpHdr;
	BYTE		indicator;
	BYTE		fu_hdr;
} __attribute__((packed)) RTP_HDR_FUA;
#define RTP_HDR_FUA_LEN		sizeof(RTP_HDR_FUA)

#define RTSP_RTPHDR_LEN	(sizeof(RTP_HDR_S) + 4)
typedef struct _tagRtspHdr {
	char	dollar;	//'$'
	unsigned char interleaved_id;
	unsigned short len;
	RTP_HDR_S	rtpHdr;
} RTSP_RTPHDR;

#define RTSP_RTPHDR_FUA_LEN	(sizeof(RTP_HDR_S) + 6)
typedef struct _tagRtspHdr_FUA {
	char dollar;
	unsigned char	interleaved_id;
	unsigned short 	len;
	RTP_HDR_FUA	rtpFua;
} RTSP_RTPHDR_FUA;

/* Type of packet header */
typedef enum { 
	SENDERTYPE_RTSP, //interleaved rtsp
	SENDERTYPE_RTP,
	SENDERTYPE_MAX
} SENDERTYPE;

typedef struct _tagSender
{
	SENDERTYPE	st;

	struct 		list_head target_list;	//
	pthread_mutex_t	mutex;

	MEDIATYPE	mt;	/* payload type, for p2p */
	RTP_PT_E	pt;           /*payload type, fill into the rtp header*/
	int		ssrc;
	WORD		last_sn;      /*last recv sn*/

	int	sock;	//sock for rtp sending

	union {
		RTP_HDR_S	rtpHdr;
		RTP_HDR_FUA	rtpFua;

		RTSP_RTPHDR	rtspHdr;
		RTSP_RTPHDR_FUA	rtspFua;
	};
	int		hdr_len;
	struct iovec iov[2];

	unsigned char	*buffer;	//将多个包合并到一起发送
	unsigned int	data_size, buff_size;
} RTSPSENDER;

typedef struct _tagSlice {
	struct iovec iov[2];
} SLICE;

void PackRTP(IN RTSPSENDER * pRtpStream, int ts_inc, DWORD marker, SLICE* pSlice);
void PackRTPFuAonRTSP(RTSPSENDER *pSender, unsigned int ts_inc, BOOL start, BOOL end, unsigned char c, SLICE *pSlice);
void PackRTPFuA(IN RTSPSENDER *pSender, int ts_inc, BOOL start, BOOL end, unsigned char c, SLICE *pSlice);

void InitSender(RTSPSENDER *pSndr, MEDIATYPE fmt, SENDERTYPE st, unsigned int ssrc);
void UninitSender(RTSPSENDER *pSender);
void SenderPackMedia(IN RTSPSENDER * pSender, sint32 pts, DWORD marker, SLICE* pSlice);
int SenderSend(IN RTSPSENDER * pRtpStream);

#define SERVER_RTP_PORT	20000

#ifdef __cplusplus
}
#endif

#endif
