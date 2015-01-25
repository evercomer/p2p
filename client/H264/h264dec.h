#ifndef __h264dec_h__
#define _h264dec_h__

#ifdef __cplusplus
extern "C" {
#endif

typedef void* HDEC;

typedef struct tagH264DecFrame
{
  unsigned char   *pY;
  unsigned char   *pU;
  unsigned char   *pV;
  
  int             width;
  int             height;
                  
  int             uYStride;
  int             uUVStride;  
                    
  int             bDisplay;
  int			interlaced_frame;
} VDECOUTPUT;

void *CreateDecoder();
void InitDecoder();
int DestroyDecoder(HDEC hdec);
int Decode(HDEC hDec, const void *inbuf, unsigned int size, VDECOUTPUT *pVFrame);

#ifdef __cplusplus
}
#endif

#endif

