#include "avcodec.h"
#include "dsputil.h"
#include "h264.h"
#include "h264dec.h"

void InitDecoder()
{
	avcodec_init();
}

struct decoder_handle {
	unsigned int tag;	//'HDEC'
	AVFrame *picture;
	AVCodecContext *c;
};
extern AVCodec h264_decoder;

void *CreateDecoder()
{
	struct decoder_handle *hdec;

	InitDecoder();

	hdec = malloc(sizeof(struct decoder_handle));
	if(!hdec) return NULL;
	hdec->tag = 'HDEC';

	hdec->c = avcodec_alloc_context();
	if(!hdec->c) goto c2;

	//if(h264_decoder.capabilities & CODEC_CAP_TRUNCATED)
	//	hdec->c->flags |= CODEC_FLAG_TRUNCATED;
		
	if(avcodec_open(hdec->c, &h264_decoder) < 0)
	{
		av_free(hdec->c);
		return NULL;
	}
	
	{
		H264Context *h = hdec->c->priv_data;
		MpegEncContext *s = &h->s;
		s->dsp.idct_permutation_type = 1;
		dsputil_init(&s->dsp, hdec->c);
	}
	
	hdec->picture = avcodec_alloc_frame();
	if(!hdec->picture) goto c1;

	return hdec;
	
	//av_free(picture);
c1:
	av_free(hdec->c);
c2:
	free(hdec);
	return NULL;
}
int DestroyDecoder(HDEC hDec)
{
	struct decoder_handle *pd;

	if(hDec == NULL) return 0;
	pd = (struct decoder_handle*)hDec;
	if(pd->tag != 'HDEC') return -1;

	av_free(pd->c);
	av_free(pd->picture);
	free(pd);
	return 0;
}

int Decode(HDEC hDec, const void *inbuf, unsigned int size, VDECOUTPUT *pVFrame)
{
	AVFrame *picture;
	AVCodecContext *c;
	struct decoder_handle *pd;
	int len;
	
	pd = (struct decoder_handle*)hDec;
	if(pd->tag != 'HDEC') return -1;
	
	len = avcodec_decode_video(pd->c, pd->picture, &pVFrame->bDisplay, inbuf, size);
	if(len < 0) return -1;

	picture = pd->picture;
	c = pd->c;
	pVFrame->pY = picture->data[0];
	pVFrame->pU = picture->data[1];
	pVFrame->pV = picture->data[2];

	pVFrame->uYStride = picture->linesize[0];
	pVFrame->uUVStride = picture->linesize[1];

	pVFrame->width = c->width;
	pVFrame->height = c->height;

	pVFrame->interlaced_frame = picture->interlaced_frame;

	return len;
}
