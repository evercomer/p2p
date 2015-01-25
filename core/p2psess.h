#ifndef __p2psess_h__
#define __p2psess_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "platform_adpt.h"
#include "p2pconst.h"
#include "chnbuf.h"
#include "p2pcore.h"

#ifdef WIN32
#ifdef P2PCLT_EXPORTS
#define P2PSESSAPI_DECL __declspec(dllexport)
#else
#define P2PSESSAPI_DECL __declspec(dllimport)
#endif
#else
#define P2PSESSAPI_DECL
#endif

#ifndef __BIG_ENDIAN__
#define P2PCMD_TAG		0xD1EAC10D
#else
#define P2PCMD_TAG		0x0DC1EAD1
#endif


/** p2p command&response header. 
 * Intentionally 16 bytes length for 64-bits system */
struct p2pcmd_header 
{
	uint32_t tag;	//同步标志
	uint8_t  chno;
	uint8_t  status;
#ifndef __BIG_ENDIAN__
	uint16_t op:14;
	uint16_t end:1;
	uint16_t cls:1;
#else
	uint16_t cls:1;
	uint16_t end:1;
	uint16_t op:14;
#endif
	/** A way to match the request and response.
	 *  1. asynchronous requests&responses can be matched by trans_id.
	 *  2. A request/response larger than maxium packet size(1400B?) can be 
	 *     sent in mulitple packets with each packet having the same trans_id
	 *     and the last packet set its "end" flag. These data are reassembled
	 *     in receiver's queue
	 */
	uint32_t trans_id; //
	uint32_t length;
} __PACKET__;
#define P2PCMD_STATUS(ph) ((ph)->status)
#define P2PCMD_SET_STATUS(ph, status) (ph)->status = (status)
#define P2PCMD_OP(ph) ntohs((ph)->op)
#define P2PCMD_SET_OP(ph, op) (ph)->op = htons(op)
#define P2PCMD_TID(ph) ntohl((ph)->trans_id)
#define P2PCMD_SET_TID(ph, trans_id) ((ph)->trans_id = htonl(trans_id))
#define P2PCMD_DATA_LEN(ph) ntohl((ph)->length)
#define P2PCMD_SET_DATA_LEN(ph, len) (ph)->length = htonl(len)
#define P2PCMD_PACKET_LEN(ph) (sizeof(struct p2pcore_header) + ntohl((ph)->length))

void init_p2pcmd_header(struct p2pcmd_header* p, int op, int cls, int status, int len, uint32_t trans_id);
static INLINE BOOL check_p2pcmd_header(const struct p2pcmd_header *pch)
{
	return (pch->tag == P2PCMD_TAG);
    //return pch->tag1 == 0xA5 && pch->tag2 == 0xA5;
}


/* Errors for p2p session */
#define P2PSE_INVALID_SESSION_OBJECT    -30001
#define P2PSE_INVALID_P2PCHNO           -30002 
#define P2PSE_P2PCHN_NOT_OPEN           -30003

struct P2PSESS;
typedef struct P2PSESS *HP2PSESS;

typedef BOOL (*VERIFYAUTHSTRINGCB)(const char *auth_str);
typedef int (*SESSCREATEDCB)(HP2PSESS hsess);
typedef void (*SESSABORTEDCB)(HP2PSESS hsess);
typedef void (*CONNFAILEDCB)(int err, void *pUser);

typedef
enum {
	PSCB_ID_VERIFY_AUTH_STRING = 1,
	PSCB_ID_SESS_CREATED,
	PSCB_ID_SESS_ABORTED,
	PSCB_ID_CONN_FAILED
} EP2PSESSCBID;

void P2pSessSetAuthCallback(int id, VERIFYAUTHSTRINGCB cb);


//same as FRAMEINFO in chnbuf.h
struct _tagP2PFRAMEINFO {
	UINT mt;	//media type MEDIATYPE_XXXX
	UINT ts;	//time stamp
	UINT isKeyFrame;

	BYTE *pFrame;
	UINT len;
} __PACKED__;
typedef struct _tagP2PFRAMEINFO P2PFRAMEINFO;

/*
//same as RESPINFO in rcvrchn.h
typedef struct __tagP2PRESPINFO {
	UINT transId;
	int status;
	int isLast;	//is the last packet

	BYTE *pData;
	UINT len;
} P2PRESPINFO;
*/

typedef enum { SCT_CMD, SCT_MEDIA_SND, SCT_MEDIA_RCV } SESSCHNTYPE;

#define P2PEVENT_ALERT_MOTION   1
#define P2PEVENT_ALERT_IO       2
#define P2PEVENT_ALERT_SOUND    3
#define P2PEVENT_SDCARD_FAILURE 4
typedef void (*ONEVENT_CB)(int event, BYTE *pData, int len, void *pUserData);

//! \brief callback for P2pSessCreateAync
/** If connection failed, hsess is NULL, err is the code for error; otherwise, hsess is a valid handle, err is 0 */
typedef void (*SESSCREATECB)(HP2PSESS hsess, int err, void *pUser);


P2PSESSAPI_DECL int P2pSessGlobalInitialize(const char *server1, const char *server2, const char *sn, VERIFYAUTHSTRINGCB cb);
P2PSESSAPI_DECL void P2pSessGlobalUninitialize();

P2PSESSAPI_DECL int P2pSessCreate(const char *p2psvr, const char *id, const uint8_t *sident, const char *auth_str, HP2PSESS *hsess);
P2PSESSAPI_DECL int P2pSessCreateAsync(const char *p2psvr, const char *id, const uint8_t *sident, 
		const char *auth_str, SESSCREATECB cb, void *pUser);
P2PSESSAPI_DECL int P2pSessAccept(HP2PSESS *hsess, UINT wait_ms);
P2PSESSAPI_DECL int P2pSessDestroy(HP2PSESS hP2pSess);
P2PSESSAPI_DECL HP2PCONN P2pSessGetConn(HP2PSESS hP2pSess);

/** \brief Get session state
 *  \return
 * 	\retval  <0  -- error
 *	\retval  >=0 -- P2PCONNSTATE
 */
P2PSESSAPI_DECL int P2pSessGetState(HP2PSESS hP2pSess);

P2PSESSAPI_DECL void *P2pSessGetUserData(HP2PSESS hP2pSess);
int P2pSessSetUserData(HP2PSESS hP2pSess, void *pUser);
P2PSESSAPI_DECL void P2pSessSetEventCallback(HP2PSESS hP2pSess, ONEVENT_CB cb, void *pData);


P2PSESSAPI_DECL BOOL P2pSessOpenChannel(HP2PSESS hP2pSess, SESSCHNTYPE type, int chno, UINT size, UINT nSafeSpace);
P2PSESSAPI_DECL BOOL P2pSessCloseChannel(HP2PSESS hP2pSess, SESSCHNTYPE type, int chno);

///* \brief Send command request
//   Length per sending should be less than 1350 bytes.
P2PSESSAPI_DECL int P2pSessSendRequest(HP2PSESS hsess, int chno, int cmd, const void *pData, UINT nDataLen);

/** \brief Send Response */
P2PSESSAPI_DECL int P2pSessSendResponse(HP2PSESS hsess, int chno, int cmd, const void *pData, UINT nDataLen, uint32_t trans_id);

///* \brief Send event notification to peer
P2PSESSAPI_DECL int P2pSessSendEvent(HP2PSESS hsess, const void *pData, UINT nDataLen);

///* \brief Send event notification to all peers
P2PSESSAPI_DECL int P2pSessSendEventToAll(const void *pData, UINT nDataLen);

///* \brief Send response with error indication, no extra data
//   \param status only DCSS_xxx defined in p2pconst.h are allowed
P2PSESSAPI_DECL int P2pSessSendResponseError(HP2PSESS hsess, int chno, int cmd, uint8_t status, uint32_t trans_id);

//! \brief Receive media frame
P2PSESSAPI_DECL int P2pSessGetFrame(HP2PSESS hP2pSess, int chn, /*OUT*/P2PFRAMEINFO *pFrmHdr, UINT timeout);
P2PSESSAPI_DECL int P2pSessReleaseFrame(HP2PSESS hP2pSess, int chn, const P2PFRAMEINFO *pFrmHdr);

//! \brief Receive command packet
/// \param [out] pCpi
int P2pSessRecvPacket(HP2PSESS hP2pSess, int chn, CMDPKTINFO *pCpi, DWORD timeout);

//! \brief Release command packet
P2PSESSAPI_DECL int P2pSessReleasePacket(HP2PSESS hP2pSess, int chn, CMDPKTINFO *pPkt);

//! \breif Receive&&release command packet and return status only
P2PSESSAPI_DECL int P2pSessRecvStatus(HP2PSESS hP2pSess, int chn, DWORD timeout);

typedef int (*P2PCMD_RESP_CB)(int status, int end, void *pData, UINT nSize, void *pUserData);
//! \brief Asynchronous command handler
/// Send command. When response comes, call a callback function
P2PSESSAPI_DECL int P2pSessCommandWithCB(HP2PSESS hP2pSess, int chn, int cmd, const void *pDataIn, UINT nDataInLen, 
									P2PCMD_RESP_CB cb, void *pUserData, UINT timeout);

//! \brief Synchronous command handler
/// Send command and wait for response, omit the call to P2pSessReleasePacket()
/// \return status or error code
P2PSESSAPI_DECL int P2pSessQuery(HP2PSESS hP2pSess, int chn, int cmd, const void *pDataIn, UINT nDataInLen, 
									/*OUT*/void *pDataOut, /*INOUT*/UINT *nDataOutLen, UINT timeout);

//! \brief Synchronous command handler
/// Like P2pSessQuery(), but has no data out
/// \return status or error code
P2PSESSAPI_DECL int P2pSessExec(HP2PSESS hP2pSess, int chn, int cmd, const void *pDataIn,	UINT nDataInLen, UINT timeout);

//--------------------------------------------------------------------
//--------------------------------------------------------------------
#define OP_EVENT           100 //D->C only
struct p2pcmd_event_header {
	struct p2pcmd_header dh;
	int	event;
	//uint8_t data[0];
};

/*
#define OP_CD_COMMAND		110
//C(lient) --> D(evice)
//command request header
struct p2pcmd_command_header {
	struct p2pcmd_header dh;
	int cmd;
};
*/
//-----------------------------------------
// length = 12
typedef struct _tagP2PMEDIAHEADER
{                                                                                            
	uint8_t syncByte1, syncByte2;
	uint8_t  pt;
#ifndef __BIG_ENDIAN__
	uint8_t  chno:6;
	uint8_t  end:1;
	uint8_t  keyframe:1;
#else
	uint8_t  keyframe:1;
	uint8_t  end:1;
	uint8_t  chno:6;
#endif

	uint16_t seqno;
	uint16_t len;             

	uint32_t ts;             
} P2PMEDIAHEADER; 


/** \brief Send media frame
  for h264, frame is not nalu splitted, and start with 4 bytes sync bytes: 00 00 00 01 */
P2PSESSAPI_DECL int P2pSessSendMediaFrame(HP2PSESS hSess, int chno, uint8_t mt/*media type*/, DWORD timestamp, 
		BOOL isKeyFrame, BYTE *pFrame, int size, UINT maxWaitMs);
P2PSESSAPI_DECL int P2pSessSendMediaFrameV(HP2PSESS hsess, int chno, uint8_t mt/*media type*/, DWORD timestamp, 
		BOOL isKeyFrame, PA_IOVEC *vec, UINT size, UINT maxWaitMs);


#ifdef __cplusplus
}
#endif

#endif
