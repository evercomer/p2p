#include "rudp.h"
#include "p2pbase.h"
#include "p2pcore.h"
#include "p2psess_imp.h"
#include "p2psess.h"
#include "chnbuf.h"
#include "netbase.h"
#include "linux_list.h"

#define MAX_PAYLOAD_SIZE 1400

#ifndef min
#define min(x,y) ((x)<(y)?(x):(y))
#endif
#ifndef max
#define max(x,y) ((x)>(y)?(x):(y))
#endif

#ifndef offsetof
#define offsetof(s,m)   (size_t)&(((s *)0)->m)
#endif


static void _MediaDemultiplexer(HP2PSESS pP2pSess, BOOL bDcsPacket, unsigned char * pBuff,  unsigned int len);
static VERIFYAUTHSTRINGCB s_AuthCb;
static PA_MUTEX	s_PendingConnMutex;
static LIST_HEAD(s_PendingConnList);

static PA_EVENT s_AcceptSessEvent;
static LIST_HEAD(s_AcceptedSessList);
static PA_SPIN s_spinTid;

static BOOL _VerifyAuthString(const char *auth_str)
{
	if(s_AuthCb) return s_AuthCb(auth_str);
	return TRUE;
}

void init_p2pcmd_header(struct p2pcmd_header* p, int op, int cls, int status, int len, uint32_t trans_id)
{
	p->tag = P2PCMD_TAG;
	P2PCMD_SET_STATUS(p, status);
	P2PCMD_SET_OP(p, op);
	P2PCMD_SET_DATA_LEN(p, len);
	p->cls = cls;
	p->end = 1;
	P2PCMD_SET_TID(p, trans_id);
}

static HP2PSESS allocP2pSess()
{
	HP2PSESS psess = (HP2PSESS)calloc(sizeof(P2PSESS), 1);
	psess->tag = P2PSESS_TAG;
	INIT_LIST_HEAD(&psess->sess_list);
	INIT_LIST_HEAD(&psess->cb_list);
	psess->rBuff = (BYTE*)malloc(1500);

	int i;
	for(i=0; i<MAX_CMD_CHN; i++)
		PA_RWLockInit(psess->icch[i].rwlock);
	for(i=0; i<MAX_MEDIA_CHN; i++)
		PA_RWLockInit(psess->imch[i].rwlock);

	return psess;
}

static void _cleanAndFreeSess(HP2PSESS pP2pSess)
{
	int i;

	if(pP2pSess->rBuff) free(pP2pSess->rBuff);

	for(i=0; i<MAX_MEDIA_CHN; i++)
		if(pP2pSess->imch[i].pMediaChn)
			ChnBuffCancel(pP2pSess->imch[i].pMediaChn->pChnBuff);

	for(i=0; i<MAX_CMD_CHN; i++)
		if(pP2pSess->icch[i].pCmdChn)
			ChnBuffCancel(pP2pSess->icch[i].pCmdChn);

	for(i=0; i<MAX_MEDIA_CHN; i++)
	{
		if(pP2pSess->imch[i].pMediaChn)
		{
			MediaChnDestroy(pP2pSess->imch[i].pMediaChn);
			pP2pSess->imch[i].pMediaChn = NULL;
		}
	}

	for(i=0; i<MAX_CMD_CHN; i++)
	{
		if(pP2pSess->icch[i].pCmdChn)
		{
			ChnBuffDestroy(pP2pSess->icch[i].pCmdChn);
			pP2pSess->icch[i].pCmdChn = NULL;
		}
	}


	for(i=0; i<MAX_MEDIA_CHN; i++)
		PA_RWLockUninit(pP2pSess->imch[i].rwlock);
	for(i=0; i<MAX_CMD_CHN; i++)
		PA_RWLockUninit(pP2pSess->icch[i].rwlock);

	pP2pSess->tag = 0;
	free(pP2pSess);
}


static int _ConnCreated(HP2PCONN hconn)
{
	void *pUser;
	HP2PSESS psess;

	P2pConnGetUserData(hconn, &pUser);

	psess = allocP2pSess();
	psess->hconn = hconn;
	P2pConnSetUserData(hconn, psess);
	P2pConnSetUserBuffer(hconn, psess->rBuff, 1500);

	if(pUser) //outgoing connection established
	{
		PA_MutexLock(s_PendingConnMutex);
		struct list_head *p;
		list_for_each(p, &s_PendingConnList)
		{
			PENDINGCONN *item = list_entry(p, PENDINGCONN, list);
			if(item == pUser)
			{
				item->hsess = (HP2PSESS)psess;
				item->err = 0;
				if(item->bSync)
					PA_EventSet(item->sync.event);
				else 
				{
					if(item->async.cb)
						item->async.cb(item->hsess, 0, item->async.pUser);
					list_del(&item->list);
					free(item);
				}
				break;
			}
		}
		PA_MutexUnlock(s_PendingConnMutex);
	}
	else //incoming connection accepted
	{
		PA_MutexLock(s_PendingConnMutex);
		list_add_tail(&psess->sess_list, &s_AcceptedSessList);
		PA_MutexUnlock(s_PendingConnMutex);
		PA_EventSet(s_AcceptSessEvent);
	}

	return 0;
}

static void _ConnAbortionNotify(HP2PCONN hconn, int err)
{
	/* Clear the session not accepted */
	HP2PSESS psess;
	if(P2pConnGetUserData(hconn, (void**)&psess) == 0 && psess)
	{
		PA_MutexLock(s_PendingConnMutex);
		struct list_head *p;
		HP2PSESS pPendingSess = NULL;
		list_for_each(p, &s_AcceptedSessList)
		{
			HP2PSESS item = list_entry(p, P2PSESS, sess_list);
			if(item == psess)
			{
				list_del(&psess->sess_list);
				pPendingSess = psess;
				break;
			}
		}
		PA_MutexUnlock(s_PendingConnMutex);

		if(pPendingSess)
		{
			P2pConnCloseAsync(hconn);
			_cleanAndFreeSess(psess);
		}
	}
}

static void _ConnFailed(int err, void *pUser)
{
	PENDINGCONN *pend = (PENDINGCONN*)pUser;

	PA_MutexLock(s_PendingConnMutex);
	struct list_head *p;
	list_for_each(p, &s_PendingConnList)
	{
		PENDINGCONN *item = list_entry(p, PENDINGCONN, list);
		if(item == pend)
		{
			item->err = err;
			if(item->bSync) PA_EventSet(item->sync.event);
			else 
			{
				if(item->async.cb) item->async.cb(NULL, err, item->async.pUser);
				list_del(&item->list);
				free(item);
			}
			break;
		}
	}
	PA_MutexUnlock(s_PendingConnMutex);
}

static void _OnSendHb(HP2PCONN hconn)
{
	HP2PSESS psess;
	if(P2pConnGetUserData(hconn, (void**)&psess) != 0 || !psess) return;
    P2pSessSendRequest(psess, 0, OP_HEART_BEAT, NULL, 0);
}

static void _OnData(HP2PCONN hconn, BYTE *pData, int len)
{
	HP2PSESS psess;
	if(P2pConnGetUserData(hconn, (void**)&psess) != 0 || !psess) return;

	int nExistedData;
	P2PMEDIAHEADER *pMdHdr;
	struct p2pcmd_header *pDh;

	pMdHdr = (P2PMEDIAHEADER*)psess->rBuff;
	pDh = (struct p2pcmd_header*)pMdHdr;
	nExistedData = psess->nBytesInBuff + len;
	if( (nExistedData < min( sizeof(P2PMEDIAHEADER), sizeof(struct p2pcmd_header) )) ||
			(pMdHdr->syncByte1 == 0xAA && ( nExistedData < sizeof(P2PMEDIAHEADER) + ntohs(pMdHdr->len) )) ||
			(check_p2pcmd_header(pDh) && nExistedData < P2PCMD_PACKET_LEN(pDh)) )
	{
		psess->nBytesInBuff = nExistedData;
		P2pConnSetUserBufferOffset(hconn, nExistedData);
		return;
	}

	while(1)
	{
		UINT packLen;
		BOOL bDcsPacket = FALSE;
		if( pMdHdr->syncByte1 == 0xAA && pMdHdr->syncByte2 == 0x55 )
		{
			packLen = sizeof(P2PMEDIAHEADER) + ntohs(pMdHdr->len);
		}
		else if(check_p2pcmd_header(pDh))
		{
			bDcsPacket = TRUE;
			packLen = sizeof(struct p2pcmd_header) + ntohl(pDh->length);
		}
		else //处理发送端可能只发送了部分数据包的情况: Relocate to the header
		{
			unsigned char *p, *rear;
			//dbg_msg("Corrupt package, re-locate the header\n");
			p = (unsigned char*)pMdHdr;
			rear = p + nExistedData - 3;
			for(; p < rear; p++)
			{
				if( (*p == 0xAA && *(p+1) == 0x55) || check_p2pcmd_header((const struct p2pcmd_header*)p) ) 
					break;
			}

			nExistedData -= (p - (unsigned char*)pMdHdr);
			if(nExistedData <= 0) { nExistedData = 0; break; }
#ifdef __FORCE_ALIGNMENT__ 
        //为避免可能出现的字节外界对齐问题，复制数据到缓冲区起始
        memcpy(psess->rBuff, psess->rBuff+packLen, nExistedData);
#else
			pMdHdr = (P2PMEDIAHEADER*)p;
#endif
			pDh = (struct p2pcmd_header*)pMdHdr;

			goto check_partial_pack;
		}

		/* Valid packet */
		if(bDcsPacket && pDh->op == OP_HEART_BEAT)
			;//heart_beat_sent = 0;
		else
		{
			_MediaDemultiplexer(psess, bDcsPacket, (unsigned char*)pMdHdr, packLen);
		}

		nExistedData -= packLen;
#ifdef __FORCE_ALIGNMENT__ 
        //为避免可能出现的字节外界对齐问题，复制数据到缓冲区起始
        memcpy(psess->rBuff, psess->rBuff+packLen, nExistedData);
#else
		pMdHdr = (P2PMEDIAHEADER*)((char*)pMdHdr + packLen);
#endif
		pDh = (struct p2pcmd_header*)pMdHdr;


		//Partial packet
check_partial_pack:
		if( nExistedData < min( sizeof(P2PMEDIAHEADER), sizeof(struct p2pcmd_header) ) ||
				(pMdHdr->syncByte1 == 0xAA && (nExistedData < sizeof(P2PMEDIAHEADER) + ntohs(pMdHdr->len))) ||
				(check_p2pcmd_header(pDh) && ( nExistedData < sizeof(struct p2pcmd_header) + ntohl(pDh->length) )) )
		{
			memcpy(psess->rBuff, pMdHdr, nExistedData); //
			psess->nBytesInBuff = nExistedData;
			P2pConnSetUserBufferOffset(hconn, nExistedData);
			break;
		}
	}
}

static struct P2pCoreCbFuncs _p2pcore_cb  = {
#ifdef __LINUX__
	.VerifyAuthString = _VerifyAuthString,
	.ConnCreated = _ConnCreated,
	.ConnAbortionNotify = _ConnAbortionNotify,
	.ConnFailed = _ConnFailed,
	.OnData = _OnData,
	.OnSendHb = _OnSendHb
#else
	_VerifyAuthString,
	_ConnCreated,
	_ConnAbortionNotify,
	_ConnFailed,
	_OnData,
	_OnSendHb
#endif
};


int P2pSessGlobalInitialize(const char *server1, const char *server2, const char *sn, VERIFYAUTHSTRINGCB cb)
{
	const char *svrs[] = { server1, server2 };
	s_AuthCb = cb;
	PA_SpinInit(s_spinTid);
	PA_MutexInit(s_PendingConnMutex);
	PA_EventInit(s_AcceptSessEvent);
	return P2pCoreInitialize(svrs, 2, sn, &_p2pcore_cb);
}

void P2pSessGlobalUninitialize()
{
	P2pCoreTerminate();
	P2pCoreCleanup();

	struct list_head *p, *q;
	HP2PSESS psess;
	list_for_each_safe(q, p, &s_AcceptedSessList)
	{
		psess = list_entry(p, P2PSESS, sess_list);
		list_del(&psess->sess_list);
		//P2pConnClose(psess->hconn); //hconn is invalid after P2pCoreCleanup()
		free(psess);
	}
	list_for_each_safe(p, q, &s_PendingConnList)
	{
		PENDINGCONN *item = list_entry(p, PENDINGCONN, list);
		item->err = P2PE_TIMEOUTED;
		if(item->bSync) PA_EventSet(item->sync.event);
		else 
		{
			if(item->async.cb) item->async.cb(NULL/*failed*/, item->err, item->async.pUser);
			list_del(&item->list);
			free(item);
		}
	}

	PA_MutexUninit(s_PendingConnMutex);
	PA_EventUninit(s_AcceptSessEvent);
	PA_SpinUninit(s_spinTid);
}

void _MediaDemultiplexer(HP2PSESS pP2pSess, BOOL bDcsPacket, unsigned char * pBuff,  unsigned int len)
{ 
	//if(check_p2pcmd_header((const struct p2pcmd_header*)pBuff))
	if(bDcsPacket)
	{ 
		struct p2pcmd_header *pdh = (struct p2pcmd_header*)pBuff;
		switch(P2PCMD_OP(pdh))
		{
			case OP_HEART_BEAT:
				if(pdh->cls == CLS_REQUEST)
                        P2pSessSendResponse(pP2pSess, 0, OP_HEART_BEAT, NULL, 0, P2PCMD_TID(pdh));
				break;

			case OP_EVENT:
				if(pP2pSess->OnEventCB)
				{ 
					int *p = (int*)(pdh+1);
					pP2pSess->OnEventCB(*p, (BYTE*)(p+1), len - sizeof(struct p2pcmd_header) - 4, pP2pSess->pUserData);
				}
				break;

			default:
				if(pdh->cls == CLS_RESPONSE)
				{
					/*search in callback list for response*/
					struct list_head *p;
					PA_MutexLock(pP2pSess->mutx_cbq);
					list_for_each(p, &pP2pSess->cb_list)
					{
						P2PCBITEM *item = list_entry(p, P2PCBITEM, cb_list);
						if(item->tid == P2PCMD_TID(pdh) && item->op == P2PCMD_OP(pdh))
						{
							list_del(&item->cb_list);
							PA_MutexUnlock(pP2pSess->mutx_cbq);
							item->cb(P2PCMD_STATUS(pdh), pdh->end, pdh + sizeof(struct p2pcmd_header), P2PCMD_DATA_LEN(pdh), item->pUserData);
							if(pdh->end) free(item);
							else {
								PA_MutexLock(pP2pSess->mutx_cbq);
								list_add_tail(&item->cb_list, &pP2pSess->cb_list);
								PA_MutexLock(pP2pSess->mutx_cbq);
							}
							return;
						}
					}
					PA_MutexUnlock(pP2pSess->mutx_cbq);
				}

				/* put data(request|response|event?|...) in buffer */
				if(pdh->chno < MAX_CMD_CHN)
				{ 
					PA_RWLockLockR(pP2pSess->icch[pdh->chno].rwlock);
					if(pP2pSess->icch[pdh->chno].pCmdChn)
					{ 
						//CmdChnWrite(pP2pSess->icch[pdh->chno].pCmdChn, pdh->status, P2PCMD_TID(pdh), pdh->end, 
						//		pBuff + sizeof(struct p2pcmd_header),
						//		len - sizeof(struct p2pcmd_header));
						CmdChnWrite2(pP2pSess->icch[pdh->chno].pCmdChn, pdh);
					}
					PA_RWLockUnlock(pP2pSess->icch[pdh->chno].rwlock);
				}
		}
	}
	else
	{ 
		BYTE *pData;
		UINT dataLen, ts, pt;
		P2PMEDIAHEADER *pHdr;
		unsigned short	nSQ;
		MEDIACHNBUFF	*pMdChn;
		
		pHdr = (P2PMEDIAHEADER*)pBuff;
		if(pHdr->chno >= MAX_MEDIA_CHN) return;

		PA_RWLockLockR(pP2pSess->imch[pHdr->chno].rwlock);
		if(!(pMdChn = pP2pSess->imch[pHdr->chno].pMediaChn))
			goto out;
 
		pData = pBuff + sizeof(P2PMEDIAHEADER);
		dataLen = len - sizeof(P2PMEDIAHEADER);
		ts = ntohl(pHdr->ts);
		pt = pHdr->pt;		
		nSQ = ntohs(pHdr->seqno);
	
		/* 1. 检查是否掉包  */
		if ((pMdChn->u16SN + 1 != nSQ) && (pMdChn->u16SN != 0) && (0 != nSQ))	//掉包
		{ 
			pMdChn->bWaitIDR = TRUE;
			ChnBuffDiscardCurrentFrame(pMdChn->pChnBuff);
			pMdChn->pFrameHeaderWritten = FALSE;
		}
		pMdChn->u16SN = nSQ;

		if(pHdr->keyframe){ 
			pMdChn->bWaitIDR = FALSE;
		}
		if(pMdChn->bWaitIDR) goto out;
	
	
		/* 2. 缓冲数据，第一包则初始帧头 */
		if(pMdChn->pFrameHeaderWritten == 0) 
		{ 
			MediaChnWriteHeader(pMdChn, pt, ts, pHdr->keyframe);
			pMdChn->pFrameHeaderWritten = TRUE;
		}
	
//		LOG("fragment: len = %d, end = %d\n", dataLen, pHdr->end);
		ChnBuffWriteFragment(pMdChn->pChnBuff, pData, dataLen, pHdr->end);
		pMdChn->pFrameHeaderWritten = pHdr->end ? FALSE : TRUE;
	
/*	
		if(pP2pSess->tsPrevSec == 0) pP2pSess->tsPrevSec = ts;
		else if(pP2pSess->bPlayback && (ts - pP2pSess->tsPrevSec > 1000))
		{
			pP2pSess->tsPrevSec += 1000;
			if(pP2pSess->PbClock.dt.sec < 59)
				pP2pSess->PbClock.dt.sec ++;
			else
				pP2pSess->PbClock.dt.sec = 0;
			if(pP2pSess->OnEventCB)
				pP2pSess->OnEventCB(P2PET_PB_CLOCK, &pP2pSess->PbClock, sizeof(pP2pSess->PbClock), pP2pSess->pUserData);
		}
*/ 
out:
		PA_RWLockUnlock(pP2pSess->imch[pHdr->chno].rwlock);
	}
}
//------------------------------------------------------------------------------
/*
HP2PSESS P2pSessFromConn(HP2PCONN hconn)
{
	HP2PSESS pP2pSess;

	pP2pSess = (P2PSESS*)calloc(sizeof(P2PSESS), 1);
	pP2pSess->tag = P2PSESS_TAG;

	int i;
	for(i=0; i<MAX_CMD_CHN; i++)
		PA_RWLockInit(pP2pSess->icch[i].rwlock);
	for(i=0; i<MAX_MEDIA_CHN; i++)
		PA_RWLockInit(pP2pSess->imch[i].rwlock);
	pP2pSess->icch[0].pCmdChn = ChnBuffCreate(2*1500, 1500);

	return (HP2PSESS)pP2pSess;
}
*/
int P2pSessCreate(const char *p2psvr, const char *id, const uint8_t *sident, const char *auth_str, HP2PSESS *hsess)
{
	int err;
	PENDINGCONN pend;

	INIT_LIST_HEAD(&pend.list);
	PA_EventInit(pend.sync.event);
	pend.bSync = TRUE;
	pend.err = 0;

	if( (err = P2pConnInit(p2psvr, id, sident, auth_str, auth_str?strlen(auth_str):0, &pend)) == 0)
	{
		PA_MutexLock(s_PendingConnMutex);
		list_add_tail(&pend.list, &s_PendingConnList);
		PA_MutexUnlock(s_PendingConnMutex);

		PA_EventWait(pend.sync.event);

		PA_MutexLock(s_PendingConnMutex);
		list_del(&pend.list);
		PA_MutexUnlock(s_PendingConnMutex);

		if(pend.err == 0)
		{
			*hsess = pend.hsess;
			err = 0;
		}
		else
			err = pend.err;
	}
	PA_EventUninit(pend.sync.event);
	return err;
}

int P2pSessCreateAsync(const char *p2psvr, const char *id, const uint8_t *sident, 
		const char *auth_str, SESSCREATECB cb, void *pUser)
{
	PENDINGCONN *pPendConn = (PENDINGCONN*)calloc(sizeof(PENDINGCONN), 1);
	pPendConn->bSync = FALSE;
	pPendConn->async.cb = cb;
	pPendConn->async.pUser = pUser;

	PA_MutexLock(s_PendingConnMutex);
	list_add_tail(&pPendConn->list, &s_PendingConnList);
	PA_MutexUnlock(s_PendingConnMutex);

	return P2pConnInit(p2psvr, id, sident, auth_str, auth_str?strlen(auth_str):0, pPendConn);
}

int P2pSessAccept(HP2PSESS *hsess, UINT wait_ms)
{
	if(list_empty(&s_AcceptedSessList))
		if(!PA_EventWaitTimed(s_AcceptSessEvent, wait_ms))
			return P2PE_TIMEOUTED;

	HP2PSESS psess;
	PA_MutexLock(s_PendingConnMutex);
	psess = list_entry(s_AcceptedSessList.next, P2PSESS, sess_list);
	list_del(s_AcceptedSessList.next);
	PA_MutexUnlock(s_PendingConnMutex);
	INIT_LIST_HEAD(&psess->sess_list);
	*hsess = psess;
	return 0;
}

HP2PCONN P2pSessGetConn(HP2PSESS hsess)
{
	if(hsess->tag != P2PSESS_TAG) return NULL;
	return hsess->hconn;
}

int P2pSessDestroy(HP2PSESS hsess)
{
	if(hsess->tag != P2PSESS_TAG)
		return P2PSE_INVALID_SESSION_OBJECT;

	if(hsess->hconn)
		P2pConnClose(hsess->hconn);

	_cleanAndFreeSess(hsess);
	
	return 0;
}


BOOL P2pSessOpenChannel(HP2PSESS hsess, SESSCHNTYPE type, int chno, UINT size, UINT nSafeSpace)
{
	if(hsess->tag != P2PSESS_TAG) return FALSE;

	//if(hsess->state != 0) return FALSE;
	if(type == SCT_CMD)
	{
		if(chno >= MAX_CMD_CHN) return FALSE;
		PA_RWLockLockW(hsess->icch[chno].rwlock);
		if(!hsess->icch[chno].pCmdChn)
			hsess->icch[chno].pCmdChn = ChnBuffCreate(4*1500, 1500);
		PA_RWLockUnlock(hsess->icch[chno].rwlock);
		return TRUE;
	}
	else if(type == SCT_MEDIA_RCV)
	{
		if(chno >= MAX_MEDIA_CHN) return FALSE;
		PA_RWLockLockW(hsess->imch[chno].rwlock);
		if(!hsess->imch[chno].pMediaChn)
			hsess->imch[chno].pMediaChn = MediaChnCreate(size, nSafeSpace);
		PA_RWLockUnlock(hsess->imch[chno].rwlock);
		return TRUE;
	}
	else if(type == SCT_MEDIA_SND)
	{
		if(chno >= MAX_MEDIA_CHN) return FALSE;
		hsess->omch[chno].seqno = 0;
		hsess->omch[chno].state = 0;
	}

	return FALSE;
}
BOOL P2pSessCloseChannel(HP2PSESS hsess, SESSCHNTYPE type, int chno)
{
	if(hsess->tag != P2PSESS_TAG) return FALSE;

	if(type == SCT_CMD)
	{
		if(chno >= MAX_CMD_CHN) return FALSE;
		if(hsess->icch[chno].pCmdChn)
		{
			ChnBuffCancel(hsess->icch[chno].pCmdChn);

			PA_RWLockLockW(hsess->icch[chno].rwlock);
			ChnBuffDestroy(hsess->icch[chno].pCmdChn);
			hsess->icch[chno].pCmdChn = NULL;
			PA_RWLockUnlock(hsess->icch[chno].rwlock);
		}
		return TRUE;
	}
	else if(type == SCT_MEDIA_RCV)
	{
		if(chno >= MAX_MEDIA_CHN) return FALSE;
		if(hsess->imch[chno].pMediaChn)
		{
			ChnBuffCancel(hsess->imch[chno].pMediaChn->pChnBuff);

			PA_RWLockLockW(hsess->imch[chno].rwlock);
			MediaChnDestroy(hsess->imch[chno].pMediaChn);
			hsess->imch[chno].pMediaChn = NULL;
			PA_RWLockUnlock(hsess->imch[chno].rwlock);
		}
		return TRUE;
	}
	return FALSE;
}


#if 0
int P2pSessGetInfo(HP2PSESS hsess, P2PSESSINFO *pInfo)
{
	if(hsess->tag != P2PSESS_TAG) return P2PSE_INVALID_SESSION_OBJECT;

	if( (pInfo->state = (P2PSESSSTATE)hsess->state) == TPCS_RUNNING )
	{
		pInfo->ct = hsess->connType;
		memcpy(&pInfo->peer_addr, &hsess->peer_addr, sizeof(struct sockaddr));
	}
	return 0;
}
int P2pSessGetState(HP2PSESS hsess)
{
	if(hsess->tag != P2PSESS_TAG) return P2PSE_INVALID_SESSION_OBJECT;
	return (P2PSESSSTATE)hsess->state;
}
#endif

int P2pSessSetUserData(HP2PSESS hsess, void *pUser)
{
	if(hsess->tag != P2PSESS_TAG) return P2PSE_INVALID_SESSION_OBJECT;
	hsess->pUserData = pUser;
	return 0;
}
void *P2pSessGetUserData(HP2PSESS hsess)
{
	if(hsess->tag != P2PSESS_TAG) return NULL;
	return hsess->pUserData;
}

void P2pSessSetEventCallback(HP2PSESS hsess, ONEVENT_CB cb, void *pData)
{
	//if(hsess->state != 0) return;
	if(hsess->tag == P2PSESS_TAG)
	{
		hsess->OnEventCB = cb;
		hsess->pUserData = pData;
	}
}

// send a packet. nDataLen is less than 1448 ?
static int _sendPacket(HP2PSESS psess, const struct p2pcmd_header *pdh, const void *pData, int nDataLen)
{
	PA_IOVEC v[2];

	PA_IoVecSetPtr(&v[0], pdh);
	PA_IoVecSetLen(&v[0], sizeof(struct p2pcmd_header));
	PA_IoVecSetPtr(&v[1], pData);
	PA_IoVecSetLen(&v[1], nDataLen);
	
	return P2pConnSendV(psess->hconn, 0, v, 2, 0);
}
static int _sendResp(HP2PSESS psess, int chno, int cmd, const void *pData, UINT nDataLen, uint8_t status, uint32_t trans_id, int isLast)
{
	struct p2pcmd_header dh;

	init_p2pcmd_header(&dh, cmd, CLS_RESPONSE, status, nDataLen, trans_id);
	dh.chno = chno;
	dh.end = isLast?1:0;
	return _sendPacket(psess, &dh, pData, nDataLen);
}

int P2pSessSendEvent(HP2PSESS hsess, const void *pData, UINT nDataLen)
{
	struct p2pcmd_header dh;

	HP2PSESS psess = (P2PSESS*)hsess;
	if(psess->tag != P2PSESS_TAG) return P2PSE_INVALID_SESSION_OBJECT;

	init_p2pcmd_header(&dh, OP_EVENT, CLS_REQUEST, 0, nDataLen, 0);
	dh.chno = 0;
	dh.end = 1;//isLast?1:0;
	return _sendPacket(psess, &dh, pData, nDataLen);
}

int P2pSessSendResponseError(HP2PSESS hsess, int chno, int cmd, uint8_t status, uint32_t trans_id)
{
	return _sendResp((P2PSESS*)hsess, chno, cmd, NULL, 0, status, trans_id, 1);
}

int P2pSessSendResponse(HP2PSESS hsess, int chno, int cmd, const void *pData, UINT nDataLen, uint32_t trans_id)
{
	HP2PSESS psess = (P2PSESS*)hsess;
	if(psess->tag != P2PSESS_TAG) return P2PSE_INVALID_SESSION_OBJECT;

	char *ptr = (char*)pData;
	int rlt = 0;
	do {
		int n = nDataLen > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : nDataLen;
		nDataLen -= n;
		if( (rlt = _sendResp(psess, chno, cmd, ptr, n, 0, trans_id, nDataLen?0:1)) )
			break;
		ptr += n;
	} while(nDataLen);
	return rlt;
}


int P2pSessSendMediaFrameV(HP2PSESS hsess, int chno, uint8_t mt, DWORD timestamp, BOOL isKeyFrame, 
		PA_IOVEC *vec, UINT size, UINT maxWaitMs)
{
	HP2PSESS psess = (P2PSESS*)hsess;
	if(psess->tag != P2PSESS_TAG) return P2PSE_INVALID_SESSION_OBJECT;

	P2PMEDIAHEADER mhdr;
	UINT i, total;
	PA_IOVEC v[2];

	if(isKeyFrame) psess->omch[chno].state = OMCHN_STATE_SENDING;
	else if(psess->omch[chno].state != OMCHN_STATE_SENDING)
		return 0;

	mhdr.syncByte1 = 0xAA;
	mhdr.syncByte2 = 0x55;
	mhdr.keyframe = isKeyFrame?1:0;
	mhdr.chno = chno;	//set when sending
	mhdr.ts = htonl(timestamp);
	mhdr.pt = mt;

	PA_IoVecSetPtr(&v[0], &mhdr);
	PA_IoVecSetLen(&v[0], sizeof(mhdr));

	for(i=total=0; i<size; i++) total += PA_IoVecGetLen(&vec[i]);
	for(i=0; i<size; i++)
	{
		int len = PA_IoVecGetLen(&vec[i]);
		const BYTE *pData = (const BYTE*)PA_IoVecGetPtr(&vec[i]);
		while(len) 
		{
			int err;
			int sl = len > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : len;

			mhdr.end = total == sl;
			mhdr.seqno = htons(psess->omch[chno].seqno);
			mhdr.len = htons(sl);

			psess->omch[chno].seqno++;

			PA_IoVecSetPtr(&v[1], pData);
			PA_IoVecSetLen(&v[1], sl);

			if( (err = P2pConnSendV(psess->hconn, 1, v, 2, maxWaitMs)) < 0 )
			{
				psess->omch[chno].state = OMCHN_STATE_WAIT_KEYFRAME;
				psess->omch[chno].seqno++; //Make seqno of next packet incontinous to indicate that some data is dropped.
				return err;
			}

			total -= sl;
			len -= sl;
			pData += sl;
		} 
	}

	return 0;
}

int P2pSessSendMediaFrame(HP2PSESS hsess, int chno, uint8_t mt, DWORD timestamp, BOOL isKeyFrame, BYTE *pFrame, int len, UINT maxWaitMs)
{
	PA_IOVEC v;
	PA_IoVecSetPtr(&v, pFrame);
	PA_IoVecSetLen(&v, len);
	return P2pSessSendMediaFrameV(hsess, chno, mt, timestamp, isKeyFrame, &v, 1, maxWaitMs);
}


int P2pSessGetFrame(HP2PSESS hsess, int chn, /*OUT*/P2PFRAMEINFO *pFrmHdr, UINT timeout)
{
	if(hsess->tag != P2PSESS_TAG)
		return P2PSE_INVALID_SESSION_OBJECT;
	if(chn > MAX_MEDIA_CHN)
		return P2PSE_INVALID_P2PCHNO;

	int rlt;
	PA_RWLockLockR(hsess->imch[chn].rwlock);
	if(!ChnBuffIsReady(hsess->imch[chn].pMediaChn->pChnBuff))
	{
		P2PCONNINFO info;
		rlt = P2pConnGetInfo(hsess->hconn, &info);
	}
	if(rlt == 0)
	{
		if(hsess->imch[chn].pMediaChn)
			rlt = MediaChnGetFrame(hsess->imch[chn].pMediaChn, (FRAMEINFO*)pFrmHdr, timeout);
		else
			rlt = P2PSE_P2PCHN_NOT_OPEN;
	}
	PA_RWLockUnlock(hsess->imch[chn].rwlock);
	return rlt;
}

int P2pSessReleaseFrame(HP2PSESS hsess, int chn, const P2PFRAMEINFO *pFrame)
{
	if(hsess->tag != P2PSESS_TAG)
		return P2PSE_INVALID_SESSION_OBJECT;

	if(chn > MAX_MEDIA_CHN)
		return P2PSE_INVALID_P2PCHNO;
		
	int rlt;
	PA_RWLockLockR(hsess->imch[chn].rwlock);
	if(hsess->imch[chn].pMediaChn)
		rlt = MediaChnReleaseFrame(hsess->imch[chn].pMediaChn, (const FRAMEINFO*)pFrame);
	else
		rlt = P2PSE_P2PCHN_NOT_OPEN;
	PA_RWLockUnlock(hsess->imch[chn].rwlock);
	return rlt;
}

static uint32_t _getTid(HP2PSESS psess)
{
	uint32_t tid;
	PA_SpinLock(s_spinTid);
	tid = psess->uTransId++;
	PA_SpinUnlock(s_spinTid);
	return tid;
}

static int _P2pSessSendRequest(HP2PSESS pP2pSess, int chn, int cmd, const void *pData, UINT nDataLen, uint32_t tid)
{
	struct p2pcmd_header dh;

	init_p2pcmd_header(&dh, cmd, CLS_REQUEST, 0, nDataLen, tid);
	dh.chno = chn;
	dh.end = 1;//isLast?1:0;
	return _sendPacket(pP2pSess, &dh, pData, nDataLen);
}

int P2pSessSendRequest(HP2PSESS hsess, int chn, int cmd, const void *pData, UINT nDataLen)
{
	uint32_t tid;

	if(hsess->tag != P2PSESS_TAG) return P2PSE_INVALID_SESSION_OBJECT;
	//if(hsess->state != 0) return P2PE_SESS_ABORTED;
	if(chn > MAX_CMD_CHN) return P2PSE_INVALID_P2PCHNO;
	if(!hsess->icch[chn].pCmdChn) return P2PSE_P2PCHN_NOT_OPEN;

	tid = _getTid(hsess);

	return _P2pSessSendRequest(hsess, chn, cmd, pData, nDataLen, tid);
}


int P2pSessRecvPacket(HP2PSESS hsess, int chn, CMDPKTINFO *pCpi, DWORD timeout)
{
	if(hsess->tag != P2PSESS_TAG)
		return P2PSE_INVALID_SESSION_OBJECT;

	//if(hsess->state != 0) return P2PE_SESS_ABORTED;

	if(chn > MAX_CMD_CHN)
		return P2PSE_INVALID_P2PCHNO;
	if(!hsess->icch[chn].pCmdChn)
		return P2PSE_P2PCHN_NOT_OPEN;

	if(!ChnBuffIsReady(hsess->icch[chn].pCmdChn))
	{
		int err;
		P2PCONNINFO info;
		if( (err = P2pConnGetInfo(hsess->hconn, &info)) )
			return err;
	}

	if( !CmdChnGetPacket(hsess->icch[chn].pCmdChn, pCpi, timeout) )
		return P2PE_TIMEOUTED;

	if(pCpi->hdr.status)
	{
		CmdChnReleasePacket(hsess->icch[chn].pCmdChn, pCpi);
		pCpi->len = 0;
	}
	return pCpi->hdr.status;
}

int P2pSessReleasePacket(HP2PSESS hsess, int chn, CMDPKTINFO *pPkt)
{
	if(!pPkt) return 0;
	if(hsess->tag != P2PSESS_TAG)
		return P2PSE_INVALID_SESSION_OBJECT;

	if(chn > MAX_CMD_CHN)
		return P2PSE_INVALID_P2PCHNO;
	if(!hsess->icch[chn].pCmdChn)
		return P2PSE_P2PCHN_NOT_OPEN;

	return CmdChnReleasePacket(hsess->icch[chn].pCmdChn, pPkt);
}

int P2pSessRecvStatus(HP2PSESS hsess, int chn, DWORD timeout)
{
	CMDPKTINFO pkt;
	int status = P2pSessRecvPacket(hsess, chn, &pkt, timeout);
	if(status == 0)
	{
#ifndef offsetof
#define offsetof(s,m)   (size_t)&(((s *)0)->m)
#endif
		CmdChnReleasePacket(((P2PSESS*)hsess)->icch[chn].pCmdChn, &pkt);
	}
	return status;
}

int P2pSessCommandWithCB(HP2PSESS hsess, int chn, int cmd, const void *pDataIn, UINT nDataInLen, 
						  P2PCMD_RESP_CB cb, void *pUserData, UINT timeout)
{
	uint32_t trans_id;
	P2PCBITEM *cbItem;
	int err;

	if(hsess->tag != P2PSESS_TAG) return P2PSE_INVALID_SESSION_OBJECT;
	//if(hsess->state != 0) return P2PE_SESS_ABORTED;
	if(chn > MAX_CMD_CHN) return P2PSE_INVALID_P2PCHNO;
	if(!hsess->icch[chn].pCmdChn) return P2PSE_P2PCHN_NOT_OPEN;

	trans_id = _getTid(hsess);

	cbItem = (P2PCBITEM*)malloc(sizeof(P2PCBITEM));
	INIT_LIST_HEAD(&cbItem->cb_list);
	cbItem->op = cmd;
	cbItem->cb = cb;
	cbItem->tid = trans_id;
	cbItem->pUserData = pUserData;
	//PA_EventInit(cbItem->evt);
	
	PA_MutexLock(hsess->mutx_cbq);
	list_add_tail(&cbItem->cb_list, &hsess->cb_list);
	PA_MutexUnlock(hsess->mutx_cbq);

	err = _P2pSessSendRequest(hsess, chn, cmd, pDataIn, nDataInLen, trans_id);
	if(err) 
	{
		PA_MutexLock(hsess->mutx_cbq);
		list_del(&cbItem->cb_list);
		PA_MutexUnlock(hsess->mutx_cbq);
		free(cbItem);
		return err;
	}
	return 0;
}

//------------------------------------------------------------------------------
struct _MemDataCbParam {
	int status;
	BYTE *pBuff;
	UINT nBuffSize, nBytesInBuff;
};
static int _memoryDataCB(int status, int end, void *pData, UINT nSize, void *pUser)
{
	struct _MemDataCbParam *param = (struct _MemDataCbParam*)pUser;

	param->status = status;
	if(param->pBuff)
	{
		int nBytesToCopy = param->nBuffSize - param->nBytesInBuff;	//free space
		if(nBytesToCopy >= nSize) nBytesToCopy = nSize;
		if(nBytesToCopy)
		{
			memcpy(param->pBuff + param->nBytesInBuff, pData, nBytesToCopy);
			param->nBytesInBuff += nBytesToCopy;
		}
	}
	return 0;
}

int P2pSessQuery(HP2PSESS hsess, int chn, int cmd, const void *pDataIn, UINT nDataInLen, 
					 /*OUT*/void *pDataOut, /*INOUT*/UINT *nDataOutLen, UINT timeout)
{
	struct _MemDataCbParam param;
	int rlt;
	
	param.pBuff = (BYTE*)pDataOut;
	param.nBuffSize = *nDataOutLen;
	param.nBytesInBuff = 0;

	rlt = P2pSessCommandWithCB(hsess, chn, cmd, pDataIn, nDataInLen, _memoryDataCB, &param, timeout);
	if(rlt) return rlt;
	*nDataOutLen = param.nBytesInBuff;
	return 0;
}
//-----------------------------------------------------------------------------------------------
int P2pSessExec(HP2PSESS hsess, int chn, int cmd, const void *pDataIn, UINT nDataInLen, UINT timeout)
{
	int err;
	struct _MemDataCbParam param;
	memset(&param, 0, sizeof(param));
	err = P2pSessCommandWithCB(hsess, chn, cmd, pDataIn, nDataInLen, NULL, NULL, timeout);
	if(err) return err;
	return param.status;
}


