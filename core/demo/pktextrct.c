#include "pktextrct.h"
#include <stdlib.h>
#include <string.h>

void PktExtractorInit(PKTEXTRACTOR *pPe, 
		int min_pkt_size, int max_pkt_size,
		int (*is_valid_pkt)(unsigned char *), 
		int (*fetch_pkt_len)(unsigned char *), 
		void (*proc_pkt)(unsigned char *, int, void *),
		void *pUser)
{
	pPe->pBuff = NULL;
	pPe->n_byts = 0;
	pPe->min_pkt_size = min_pkt_size;
	pPe->max_pkt_size = max_pkt_size;
	pPe->is_valid_pkt = is_valid_pkt;
	pPe->fetch_pkt_len = fetch_pkt_len;
	pPe->proc_pkt = proc_pkt;
	pPe->pUser = pUser;
}
void PktExtractorDeinit(PKTEXTRACTOR *pPe)
{
	if(pPe->pBuff) free(pPe->pBuff);
	pPe->pBuff = NULL;
	pPe->n_byts = 0;
}

static int procPkts(PKTEXTRACTOR *pCache, unsigned char **ppData, int *pLen)
{
	unsigned char *pData = *ppData;
	int len_pkt, rlen = *pLen;

	while(rlen >= pCache->min_pkt_size)
	{
		if(!pCache->is_valid_pkt(pData))
		{
			do {
				pData ++;
				rlen --;
		
				if(pCache->is_valid_pkt(pData))
					break;
			} while(rlen >= pCache->min_pkt_size);
			if(rlen < pCache->min_pkt_size) break;
		}

		len_pkt = pCache->fetch_pkt_len(pData);
		if(len_pkt <= rlen)
		{
			pCache->proc_pkt(pData, len_pkt, pCache->pUser);
			pData += len_pkt;
			rlen -= len_pkt;
		}
		else
			break;
	}
	*ppData = pData;
	*pLen = rlen;
	return rlen;
}

void PktExtractorProc(PKTEXTRACTOR *pCache, unsigned char *pData, int len)
{
	if(!pCache->n_byts)
	{
		if(procPkts(pCache, &pData, &len) > 0)
		{
			if(!pCache->pBuff)
				pCache->pBuff = (unsigned char*)malloc(2*pCache->max_pkt_size + pCache->min_pkt_size);

			memcpy(pCache->pBuff, pData, len);
			pCache->n_byts = len;
		}
	}
	else
	{
		unsigned char *t;

		memcpy(pCache->pBuff + pCache->n_byts, pData, len);
		pCache->n_byts += len;
		t = pCache->pBuff;
		procPkts(pCache, &t, &pCache->n_byts);
		memcpy(pCache->pBuff, t, pCache->n_byts);
	}
}


