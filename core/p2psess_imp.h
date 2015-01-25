#ifndef __p2psess_imp_h__
#define __p2psess_imp_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "rudp.h"
#include "chnbuf.h"
#include "p2pcore.h"
#include "p2psess.h"
#include "linux_list.h"

#define P2PSESS_TAG	0x57503250	//'P2PS'

typedef	struct _tagOUTMEDIACHN {
	uint16_t 	seqno;
#define OMCHN_STATE_WAIT_KEYFRAME	0
#define OMCHN_STATE_SENDING		1
	uint16_t  state;
} OUTMEDIACHN;

typedef struct _tagMediaChnEx {
	MEDIACHNBUFF *pMediaChn;
	PA_RWLOCK    rwlock;
} INMEDIACHN;
typedef struct _tagCmdChnEx {
	P2PCHNBUFF	*pCmdChn;
	PA_RWLOCK	rwlock;
} INCMDCHN;
#define MAX_MEDIA_CHN		8
#define MAX_CMD_CHN			8

typedef struct _cbItem {
	struct list_head cb_list;
	uint32_t	tid;
	int		op;
	P2PCMD_RESP_CB cb;
	void *pUserData;
} P2PCBITEM;

typedef struct P2PSESS {
	DWORD	tag;

	struct list_head sess_list;
	HP2PCONN	hconn;
	BYTE	*rBuff; //buffer for dispatch
	int	nBytesInBuff;
	
	
	//----------------------------------------
	OUTMEDIACHN		omch[MAX_MEDIA_CHN];
	INMEDIACHN	    imch[MAX_MEDIA_CHN];
	INCMDCHN	    icch[MAX_CMD_CHN];

	UINT	uTransId;
		
	//----------------------------
	PA_MUTEX	mutx_cbq;
	struct list_head cb_list;
	//----------------------------
	int timeout_val;	//timeout value(in milliseconds), < 25000
	//----------------------------

	//----------------------------
	ONEVENT_CB		OnEventCB;
	void	*pUserData;
} P2PSESS;

typedef struct PendingConn {
	struct list_head list;

	BOOL     bSync;
	union {
		//bSync == TRUE
		struct {
			PA_EVENT event;  //bSync == TRUE
		} sync;

		//bSync == FALSE
		struct {
			SESSCREATECB cb;
			void     *pUser;
		} async;
	};

	int err;
	HP2PSESS hsess; //session handle when err == 0
} PENDINGCONN;

#ifdef __cplusplus
}
#endif

#endif
