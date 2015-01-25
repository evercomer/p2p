/* 2013-08-26 add hMutex to P2PCHNBUFF
 */

#include "chnbuf.h"
#include "p2psess.h"

#define LOCK PA_MutexLock(pCb->hMutex)
#define UNLOCK PA_MutexUnlock(pCb->hMutex)

P2PCHNBUFF *ChnBuffCreate(UINT size, UINT nSafeSpace)
{
	P2PCHNBUFF *pCb = (P2PCHNBUFF*)calloc(sizeof(P2PCHNBUFF), 1);
	pCb->nSafeSpace = nSafeSpace;
	pCb->nBufferSize = size;
	PA_EventInit(pCb->hEvtW);
	PA_EventInit(pCb->hEvtR);
	PA_MutexInit(pCb->hMutex);
	pCb->pBuffer = (BYTE*)malloc(size);
	
	pCb->pR = pCb->pW = pCb->pBuffer;
	*((UINT*)pCb->pR) = 0;
	return pCb;
}
void ChnBuffDestroy(P2PCHNBUFF *pCb)
{
	ChnBuffCancel(pCb);
	PA_EventUninit(pCb->hEvtW);
	PA_EventUninit(pCb->hEvtR);
	PA_MutexUninit(pCb->hMutex);
	free(pCb->pBuffer);
	free(pCb);
}

void ChnBuffCancel(P2PCHNBUFF *pCb)
{
	LOCK;
	pCb->bQuit = TRUE;
	UNLOCK;
	PA_EventSet(pCb->hEvtW);
	PA_EventSet(pCb->hEvtR);
}

BOOL ChnBuffIsReady(P2PCHNBUFF *pCb)
{
	return pCb->pR != pCb->pW && *((UINT*)pCb->pR) != 0;
}

BOOL ChnBuffGetFrame(P2PCHNBUFF *pCb, void **ppBuff, UINT *pLen, UINT timeout)
{
	BOOL bRlt = FALSE;
start:
	LOCK;
	if(pCb->bQuit) goto out;
	if(pCb->pR == pCb->pW)
	{
		UNLOCK;
		if(!PA_EventWaitTimed(pCb->hEvtR, timeout))
		       return FALSE;
		goto start;
	}
	if(pCb->bQuit) goto out;
	if(*((UINT*)pCb->pR) == 0)	
	{
		pCb->pR = pCb->pBuffer;
		if(pCb->pR == pCb->pW)
		{
			UNLOCK;
			if(!PA_EventWaitTimed(pCb->hEvtR, timeout))
			       return FALSE;
			goto start;
		}
	}

	if(pCb->bQuit) goto out;
	*ppBuff = (pCb->pR + 4);
	*pLen = *((UINT*)pCb->pR);
	if(*pLen == 0)
		LOG("GetFrame, length = 0\n");
	bRlt = TRUE;
out:
	UNLOCK;
	return bRlt;
}

BOOL ChnBuffReleaseFrame(P2PCHNBUFF *pCb, const void *pFrame)
{
	if(pFrame == pCb->pR + 4)
	{
		LOCK;
		int len = *((UINT*)pCb->pR);
		pCb->pR += (4 + len + 3) & 0xFFFFFFFC;
		//if(pCb->pR != pCb->pW && *((UINT*)pCb->pR) == 0)
		//	pCb->pR = pCb->pBuffer;
		UNLOCK;
		PA_EventSet(pCb->hEvtW);
		//dbg_msg("pR = %p\n", pCb->pR);
		return TRUE;
	}
	return FALSE;
}

//* Frame: Aligned at 4-bytes boundary, first 4 bytes is length, then is the data
//* A frame can be writen as multiple fragements. The last framement is marked as bEnd = TRUE,
//  then the next fragement will start a new frame
BOOL ChnBuffWriteFragment(P2PCHNBUFF *pCb, const void *pData, UINT len, BOOL bEnd)
{
	BOOL bRlt = FALSE;

	if(len > pCb->nSafeSpace) return FALSE;
start:
	LOCK;

	if(pCb->bQuit) goto out;

	if(pCb->pW >= pCb->pR)
	{
		if(*((UINT*)pCb->pW) == 0 && ((pCb->nBufferSize - (pCb->pW - pCb->pBuffer)) < pCb->nSafeSpace))
		{
			if(pCb->pR == pCb->pBuffer)
			{
				UNLOCK;
				PA_EventWait(pCb->hEvtW);
				goto start;
				if(pCb->bQuit) goto out;
			}
			pCb->pW = pCb->pBuffer;
			*((UINT*)pCb->pW) = 0;
		}
	}
	while( (pCb->pW < pCb->pR) && (pCb->pR - pCb->pW < pCb->nSafeSpace) )
	{
		UNLOCK;
		PA_EventWait(pCb->hEvtW);
		LOCK;
		if(pCb->bQuit)
			goto out;
	}
	
	memcpy(pCb->pW + pCb->uFrameLen + 4, pData, len);
	pCb->uFrameLen += len;

	if(bEnd)
	{
		*((UINT*)pCb->pW) = pCb->uFrameLen;
		pCb->pW += (pCb->uFrameLen + 4 + 3) & 0xFFFFFFFC;
		*((UINT*)pCb->pW) = 0;
		pCb->uFrameLen = 0;
		PA_EventSet(pCb->hEvtR);
	}
	
	bRlt = TRUE;
out:
	UNLOCK;
	return bRlt;
}

void ChnBuffDiscardCurrentFrame(P2PCHNBUFF *pCb)
{
	LOCK;
	*((UINT*)pCb->pW) = 0;
	UNLOCK;
}

//------------------------------------------------
typedef struct _tagFRAMEHEADER {
	UINT mt;	//media type
	UINT ts;	//time stamp
	UINT isKeyFrame;
} FRAMEHEADER;

MEDIACHNBUFF *MediaChnCreate(UINT size, UINT nSafeSpace)
{
	MEDIACHNBUFF *pMdCh = (MEDIACHNBUFF*)calloc(sizeof(MEDIACHNBUFF), 1);
	pMdCh->pChnBuff = ChnBuffCreate(size, nSafeSpace);
	return pMdCh;
}
void MediaChnDestroy(MEDIACHNBUFF *pMdCh)
{
	ChnBuffDestroy(pMdCh->pChnBuff);
	free(pMdCh);
}

BOOL MediaChnWriteHeader(MEDIACHNBUFF *pMdCh, UINT mt, UINT ts, UINT isKeyFrame)
{
	FRAMEHEADER mhdr;

	mhdr.isKeyFrame = isKeyFrame;
	mhdr.ts = ts;
	mhdr.mt = mt;
	return ChnBuffWriteFragment(pMdCh->pChnBuff, (const BYTE*)&mhdr, sizeof(mhdr), FALSE);
	//pMdChn->pFrameHeaderWritten = TRUE;
}

//return: length of frame data
BOOL MediaChnGetFrame(MEDIACHNBUFF *pMdCh, /*OUT*/FRAMEINFO *pFrmInfo, UINT timeout)
{
	BYTE *pData;
	UINT len;
	if(!ChnBuffGetFrame(pMdCh->pChnBuff, (void**)&pData, &len, timeout))
		return FALSE;
	memcpy(pFrmInfo, pData, sizeof(FRAMEHEADER));
	pFrmInfo->pFrame = pData + sizeof(FRAMEHEADER);
	pFrmInfo->len = len - sizeof(FRAMEHEADER);
	return TRUE;
}

BOOL MediaChnReleaseFrame(MEDIACHNBUFF *pMdCh, const FRAMEINFO *pFrmInfo)
{
	return ChnBuffReleaseFrame(pMdCh->pChnBuff, pFrmInfo->pFrame - sizeof(FRAMEHEADER));
}

//---------------------------------------------------------
int CmdChnWrite(P2PCHNBUFF *pCb, int status, UINT transId, int op, int cpf_flags, const BYTE *pBody, UINT len)
{
	CMDPKTHEADER hdr;
	hdr.trans_id = transId;
	hdr.cmd = op;
	hdr.status = status;
	hdr.flags = cpf_flags;
	if(ChnBuffWriteFragment(pCb, &hdr, sizeof(hdr), len?FALSE:TRUE))
		if(len) ChnBuffWriteFragment(pCb, pBody, len, TRUE);

	return 0;
}

int CmdChnWrite2(P2PCHNBUFF *pCb, const struct p2pcmd_header *pHdr)
{
	CMDPKTHEADER hdr;
	hdr.trans_id = P2PCMD_TID(pHdr);
	hdr.cmd = P2PCMD_OP(pHdr);
	hdr.status = P2PCMD_STATUS(pHdr);
	hdr.flags = 0;
	if(pHdr->cls) hdr.flags |= CPF_IS_RESP;
	if(pHdr->end) hdr.flags |= CPF_IS_LAST;
	if(ChnBuffWriteFragment(pCb, &hdr, sizeof(hdr), pHdr->length?FALSE:TRUE))
		if(pHdr->length) ChnBuffWriteFragment(pCb, pHdr+1, P2PCMD_DATA_LEN(pHdr), TRUE);

	return 0;
}

BOOL CmdChnGetPacket(P2PCHNBUFF *pCb, CMDPKTINFO *pCpi, UINT timeout)
{
	BYTE *pData;
	UINT len;

	if(!ChnBuffGetFrame(pCb, (void**)&pData, &len, timeout))
		return FALSE;

	memcpy(&pCpi->hdr, pData, sizeof(CMDPKTHEADER));
	pCpi->pData = pData + sizeof(CMDPKTHEADER);
	pCpi->len = len - sizeof(CMDPKTHEADER);
	return TRUE;
}

BOOL CmdChnReleasePacket(P2PCHNBUFF *pCb, const CMDPKTINFO *pResp)
{
	return ChnBuffReleaseFrame(pCb, (char*)(pResp->pData) - sizeof(CMDPKTHEADER));
}
