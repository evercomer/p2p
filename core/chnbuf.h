#ifndef __chnbuf_h__
#define __chnbuf_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "platform_adpt.h"

//RUDP with chn support: 
//	system -> rudp receiver buffer -> frames buffer -> user
//RUDP without chn support: 
//	system -> rudp receiver buffer -> sdk's receiver buffer -> frame buffer -> user
//TCP: 
//	system -> sdk's receiver buffer -> frames buffer -> user

typedef 
struct _tagP2pChnBuffer {
	BYTE        *pBuffer;
	int         nBufferSize;
	UINT        nSafeSpace;	//Safe free space
	
	BYTE        *pR, *pW;
	UINT        uFrameLen;  //length of frame in writing
	
	PA_EVENT    hEvtW;      //There's enough space for writing
	PA_EVENT    hEvtR;      //There's enough space for reading

	PA_MUTEX    hMutex;     //2013-08-26
	//
	BOOL        bQuit;
} P2PCHNBUFF;

P2PCHNBUFF *ChnBuffCreate(UINT size, UINT nSafeSpace);
void ChnBuffDestroy(P2PCHNBUFF *pCb);
void ChnBuffCancel(P2PCHNBUFF *pCb);
BOOL ChnBuffIsReady(P2PCHNBUFF *pCb);
BOOL ChnBuffGetFrame(P2PCHNBUFF *pCb, void **ppBuff, UINT *pLen, UINT timeout);
BOOL ChnBuffReleaseFrame(P2PCHNBUFF *pCb, const void *pFrame);
BOOL ChnBuffWriteFragment(P2PCHNBUFF *pCb, const void *pData, UINT len, BOOL bEnd);
void ChnBuffDiscardCurrentFrame(P2PCHNBUFF *pCb);

typedef struct _tagFRAMEINFO {
	UINT        mt; //media type
	UINT        ts; //time stamp
	UINT        isKeyFrame;

	BYTE        *pFrame;
	UINT        len;
} FRAMEINFO;

typedef struct _tagMediaChn {
	P2PCHNBUFF	*pChnBuff;
	//Variables for receiving frame
	USHORT      u16SN;
	BOOL        bWaitIDR;
	BOOL        pFrameHeaderWritten;
} MEDIACHNBUFF;

MEDIACHNBUFF *MediaChnCreate(UINT size, UINT nSafeSpace);
void MediaChnDestroy(MEDIACHNBUFF *pMdCh);
BOOL MediaChnWriteHeader(MEDIACHNBUFF *pMdCh, UINT mt, UINT ts, UINT isKeyFrame);
BOOL MediaChnGetFrame(MEDIACHNBUFF *pMdCh, /*OUT*/FRAMEINFO *pFrmInfo, UINT timeout);
BOOL MediaChnReleaseFrame(MEDIACHNBUFF *pMdCh, const FRAMEINFO *pFrmInfo);

//command packet flags
#define CPF_IS_LAST      0x0001
#define CPF_IS_RESP      0x0002
typedef struct __tagCMDPKTHEADER {
	UINT        trans_id;

	/** \ref struct p2pcmd_header */
	uint16_t      cmd;    //p2pcmd_header::op
	uint8_t       status;
	uint8_t       flags;  //CPF_xxx
} CMDPKTHEADER;

typedef struct __tagCMDPKTINFO {
	CMDPKTHEADER hdr;

	BYTE         *pData;
	UINT         len;
} CMDPKTINFO;

int CmdChnWrite(P2PCHNBUFF *pCb, int status, UINT transId, int op, int cpf_flags, const BYTE *pBody, UINT len);
struct p2pcmd_header;
int CmdChnWrite2(P2PCHNBUFF *pCb, const struct p2pcmd_header *pHdr);
int CmdChnGetPacket(P2PCHNBUFF *pCb, CMDPKTINFO *pCpi, UINT timeout);
int CmdChnReleasePacket(P2PCHNBUFF *pCb, const CMDPKTINFO *pResp);
	
#ifdef __cplusplus
}
#endif

#endif
