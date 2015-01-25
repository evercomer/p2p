#ifndef __pktextrct_h__
#define __pktextrct_h__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PKTEXTRACTOR {
	unsigned char *pBuff;
	int n_byts;

	int min_pkt_size, max_pkt_size;
	void *pUser;

	int (*is_valid_pkt)(unsigned char *pData);
	int (*fetch_pkt_len)(unsigned char *pPkt);
	void (*proc_pkt)(unsigned char *pPkt, int pktLen, void *pUser);
} PKTEXTRACTOR;

void PktExtractorInit(PKTEXTRACTOR *pPe, 
		int min_pkt_size, int max_pkt_size,
		int (*is_valid_pkt)(unsigned char *), 
		int (*fetch_pkt_len)(unsigned char *), 
		void (*proc_pkt)(unsigned char *, int, void *), 
		void *pUser);

void PktExtractorDeinit(PKTEXTRACTOR *pPe);

void PktExtractorProc(PKTEXTRACTOR *pCache, unsigned char *pData, int len);

#ifdef __cplusplus
}
#endif

#endif

