#ifndef __YUV2RGB_H__
#define __YUV2RGB_H__

#include "platform_adpt.h"

#ifdef __cplusplus
extern "C" {
#endif

void CreateYUVTab();
void DeleteYUVTab();
void YUV420_to_RGB565(const unsigned char *y, const unsigned char *u, const unsigned char *v, 
		int width, int height, int src_ystride, int src_uvstride, 
		unsigned int *pdst, int dst_ystride);
void UYVY2RGB24(const unsigned char *psrc, int srcPitch, int width, int height, unsigned char *pdst, int dstPitch);
void YUV420_to_RGB32(const unsigned char *y, const unsigned char *u, const unsigned char *v, 
			int width, int height, int src_ystride, int src_uvstride, 
			int *pdst, int dst_stride);

void YUV420_to_RGB565_V2(int width, int height, const unsigned char *src, unsigned short *dst);
	


#ifdef __cplusplus
}
#endif

#endif
