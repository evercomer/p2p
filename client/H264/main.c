/********************************************************************
	created:	2008/06/23	
	filename: 	main.c
	author:		xcl
	
	purpose:	testbed for h264 decoder
*********************************************************************/


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "dsputil.h"
#include "h264.h"

#define PI 3.14159265358979323846


#include "avcodec.h"
#include "define.h"

#define INBUF_SIZE 4096


void pgm_save(unsigned char *buf,int wrap, int xsize,int ysize,char *filename, char *recfilename)
{
    FILE *f;
    int i,j;

	int ci=0;

	static int framenum =0;


	if(framenum == 0)
	{
		
		f=fopen(filename,"r+");
		fprintf(f,"P5\n%d %d\n%d\n",xsize,ysize,255);
		//  for(i=0;i<ysize;i++)
		//      fwrite(buf + i * wrap,1,xsize,f);
		for(i=0;i<ysize;i++)
		{
			for (j=0; j < xsize; j++)
			{				
				fprintf(f, "%3d(%3d,%3d)",*(buf + i * wrap+j),i,j);
				if(ci++%5==0)
					fprintf(f, "\n");
			}
			fprintf(f, "\n");
		}
		fclose(f);

		f=fopen(recfilename, "wb");
		fwrite(&wrap, sizeof(int), 1, f);
		fwrite(&xsize, sizeof(int), 1, f);
		fwrite(&ysize, sizeof(int), 1, f);
		fclose(f);
	}

	f=fopen(recfilename,"ab+");   
	
	for(i=0;i<ysize;i++)
	{
		fwrite(buf + i * wrap, 1, xsize, f );
//		for (j=0; j < xsize; j++)
//		{	   
//			fprintf(f, "%d(%d,%d)",*(buf + i * wrap+j),i,j);	 
//		}	  
	}
    fclose(f);
	framenum++;
}

int main(int argc, char **argv)
{
	const char *outfilename = "outrec.txt";
	const char *outrecfilename = "outrec.yuv";
	const char *filename = "test.264";
	extern AVCodec h264_decoder;
	AVCodec *codec = &h264_decoder;
	AVCodecContext *c= NULL;
	int frame, size, got_picture, len;
	FILE *fin, *fout;
	AVFrame *picture;
	uint8_t inbuf[INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE], *inbuf_ptr;
	char buf[1024]; 
	DSPContext dsp;
	unsigned int tbegin;



	/* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
	memset(inbuf + INBUF_SIZE, 0, FF_INPUT_BUFFER_PADDING_SIZE);


	/* find the mpeg1 video decoder */
	avcodec_init();
	c= avcodec_alloc_context();
	picture= avcodec_alloc_frame();
	//	 dsputil_init(&dsp, c);

	if(codec->capabilities&CODEC_CAP_TRUNCATED)
		c->flags|= CODEC_FLAG_TRUNCATED; /* we do not send complete frames */

	/* For some codecs, such as msmpeg4 and mpeg4, width and height
	   MUST be initialized there because this information is not
	   available in the bitstream. */

	/* open it */


	if (avcodec_open(c, codec) < 0) {
		fprintf(stderr, "could not open codec\n");
		exit(1);
	}
	{

		H264Context *h = c->priv_data;
		MpegEncContext *s = &h->s;
		s->dsp.idct_permutation_type =1;
		dsputil_init(&s->dsp, c);
	}
	/* the codec gives us the frame size, in samples */

	if(argc == 2) filename = argv[1];
	fin = fopen(filename, "rb");
	if (!fin) {
		fprintf(stderr, "could not open %s\n", filename);
		exit(1);
	}
	fout = fopen(outfilename, "wb");
	if (!fin) {
		fprintf(stderr, "could not open %s\n", outfilename);
		exit(1);
	}
	fclose(fout);

	fout = fopen(outrecfilename, "wb");
	if (!fin) {
		fprintf(stderr, "could not open %s\n", outrecfilename);
		exit(1);
	}
	fclose(fout);

	printf("Video decoding...\n");

	frame = 0;
	for(;;) {
		size = fread(inbuf, 1, INBUF_SIZE, fin);
		if (size == 0)
			break;

		/* NOTE1: some codecs are stream based (mpegvideo, mpegaudio)
		   and this is the only method to use them because you cannot
		   know the compressed data size before analysing it.

		   BUT some other codecs (msmpeg4, mpeg4) are inherently frame
		   based, so you must call them with all the data for one
		   frame exactly. You must also initialize 'width' and
		   'height' before initializing them. */

		/* NOTE2: some codecs allow the raw parameters (frame size,
		   sample rate) to be changed at any frame. We handle this, so
		   you should also take care of it */

		/* here, we use a stream based decoder (mpeg1video), so we
		   feed decoder and see if it could decode a frame */
		inbuf_ptr = inbuf;
		while (size > 0) {
			len = avcodec_decode_video(c, picture, &got_picture,
					inbuf_ptr, size);
			if (len < 0) {
				fprintf(stderr, "Error while decoding frame %d\n", frame);
				//exit(1);
			}
			if (got_picture) {
				//printf("saving frame %3d\n", frame);
				fflush(stdout);

				/* the picture is allocated by the decoder. no need to
				   free it */
				//  snprintf(buf, sizeof(buf), outfilename, frame);
#if 0	//save file
				pgm_save(picture->data[0], picture->linesize[0],
						c->width, c->height, outfilename, outrecfilename);
				pgm_save(picture->data[1], picture->linesize[1],
						c->width/2, c->height/2, outfilename, outrecfilename);
				pgm_save(picture->data[2], picture->linesize[2],
						c->width/2, c->height/2, outfilename, outrecfilename);
#endif
				frame++;
			}
			size -= len;
			inbuf_ptr += len;
		}
	}

	printf("%0.3f elapsed\n", (double)clock()/1000);
	/* some codecs, such as MPEG, transmit the I and P frame with a
	   latency of one frame. You must do the following to have a
	   chance to get the last frame of the video */
#define NOTFOR264
#ifdef NOTFOR264

	//    len = avcodec_decode_video(c, picture, &got_picture,
	//                               NULL, 0);
	len = avcodec_decode_video(c, picture, &got_picture,
			inbuf_ptr, 0);
	if (got_picture) {
		printf("saving last frame %3d\n", frame);
		fflush(stdout);

		/* the picture is allocated by the decoder. no need to
		   free it */
		//    snprintf(buf, sizeof(buf), outfilename, frame);
		pgm_save(picture->data[0], picture->linesize[0],
				c->width, c->height, outfilename, outrecfilename);
		pgm_save(picture->data[1], picture->linesize[1],
				c->width/2, c->height/2, outfilename, outrecfilename);
		pgm_save(picture->data[2], picture->linesize[2],
				c->width/2, c->height/2, outfilename, outrecfilename);
		frame++;
	}
#endif

	fclose(fin);
	//	 fclose(fout);

	avcodec_close(c);
	av_free(c);
	av_free(picture);
	printf("\n");
}
