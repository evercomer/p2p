#include <stdlib.h>
#include "YUV2RGB.h"

static int *colortab = NULL;
static int *u_b_tab;
static int *u_g_tab;
static int *v_g_tab;
static int *v_r_tab;

static unsigned int *rgb_2_pix = NULL;	//Used for (r,g,b) -> RGB565
static unsigned int *r_2_pix;
static unsigned int *g_2_pix;
static unsigned int *b_2_pix;

/*
 R= 1.0Y + 0 +1.402(V-128)  
 G= 1.0Y - 0.34413 (U-128)-0.71414(V-128)  
 B= 1.0Y + 1.772 (U-128)+0
*/

void CreateYUVTab();
void DeleteYUVTab();
void YUV420_to_RGB565(unsigned int *pdst, unsigned char *y, unsigned char *u, unsigned char *v, int width, int height, int src_ystride, int src_uvstride, int dst_ystride);
void UYVY2RGB24(const unsigned char *psrc, int srcPitch, int width, int height, unsigned char *pdst, int dstPitch);

//#define YUV420_RGB555
#define YUV420_RGB565


void CreateYUVTab()
{
	int i;
	int u, v;

	if(colortab) return;

	colortab = (int *)malloc(4*256*sizeof(int));
	u_b_tab = &colortab[0*256];
	u_g_tab = &colortab[1*256];
	v_g_tab = &colortab[2*256];
	v_r_tab = &colortab[3*256];

	for (i=0; i<256; i++)
	{
		u = v = (i-128);

		u_b_tab[i] = (int) ( 1.772 * u);
		u_g_tab[i] = (int) ( 0.34414 * u);
		v_g_tab[i] = (int) ( 0.71414 * v); 
		v_r_tab[i] = (int) ( 1.402 * v);
	}
	
#if defined(YUV420_RGB565) || defined(YUV420_RGB555)
	//Used for (r,g,b) -> RGB565

	rgb_2_pix = (unsigned int *)malloc(3*768*sizeof(unsigned int));

	r_2_pix = &rgb_2_pix[0*768];
	g_2_pix = &rgb_2_pix[1*768];
	b_2_pix = &rgb_2_pix[2*768];

	for(i=0; i<256; i++)
	{
		r_2_pix[i] = 0;
		g_2_pix[i] = 0;
		b_2_pix[i] = 0;
	}
#ifdef YUV420_RGB565
	for(i=0; i<256; i++)
	{
		r_2_pix[i+256] = (i & 0xF8) << 8;
		g_2_pix[i+256] = (i & 0xFC) << 3;
		b_2_pix[i+256] = (i ) >> 3;
	}

	for(i=0; i<256; i++)
	{
		r_2_pix[i+512] = 0xF8 << 8;
		g_2_pix[i+512] = 0xFC << 3;
		b_2_pix[i+512] = 0x1F;
	}
#else
	for(i=0; i<256; i++)
	{
		r_2_pix[i+256] = (i & 0xF8) << 7;
		g_2_pix[i+256] = (i & 0xF8) << 2;
		b_2_pix[i+256] = (i ) >> 3;
	}

	for(i=0; i<256; i++)
	{
		r_2_pix[i+512] = 0xF8 << 7;
		g_2_pix[i+512] = 0xF8 << 2;
		b_2_pix[i+512] = 0x1F;
	}
#endif
	r_2_pix += 256;
	g_2_pix += 256;
	b_2_pix += 256;
#endif
}

void DeleteYUVTab()
{
	if(colortab) { free(colortab); colortab = NULL; }
	if(rgb_2_pix) { free(rgb_2_pix); rgb_2_pix = NULL; }
}
#if defined(YUV420_RGB565) || defined(YUV420_RGB555)


void YUV420_to_RGB565_V2(int width, int height, const unsigned char *src, unsigned short *dst)
{
	int line, col, linewidth;
	int y, u, v, yy, vr, ug, vg, ub;
	int r, g, b;
	const unsigned char *py, *pu, *pv;

	linewidth = width >> 1;
	py = src;
	pu = py + (width * height);
	pv = pu + (width * height) / 4;

	y = *py++;
	yy = y << 8;
	u = *pu - 128;
	ug = 88 * u;
	ub = 454 * u;
	v = *pv - 128;
	vg = 183 * v;
	vr = 359 * v;

	for (line = 0; line < height; line++) {
		for (col = 0; col < width; col++) {
			r = (yy + vr) >> 8;
			g = (yy - ug - vg) >> 8;
			b = (yy + ub ) >> 8;

			if (r < 0) r = 0;
			if (r > 255) r = 255;
			if (g < 0) g = 0;
			if (g > 255) g = 255;
			if (b < 0) b = 0;
			if (b > 255) b = 255;
			*dst++ = (((unsigned short)r>>3)<<11) | (((unsigned short)g>>2)<<5) | (((unsigned short)b>>3)<<0); 

			y = *py++;
			yy = y << 8;
			if (col & 1) {
				pu++;
				pv++;

				u = *pu - 128;
				ug = 88 * u;
				ub = 454 * u;
				v = *pv - 128;
				vg = 183 * v;
				vr = 359 * v;
			}
		} 
		if ((line & 1) == 0) { 
			pu -= linewidth;
			pv -= linewidth;
		}
	} 
}



//YUV -> RGB565
void YUV420_to_RGB565(const unsigned char *y, const unsigned char *u, const unsigned char *v, 
		int width, int height, int src_ystride, int src_uvstride, 
		unsigned int *pdst, int dst_ystride)
{
	int i, j;
	int r, g, b, rgb;

	int yy, ub, ug, vg, vr;

	const int width2 = width/2;
	const int height2 = height/2;

	const unsigned char* yoff;
	const unsigned char* uoff;
	const unsigned char* voff;
	unsigned int *pRGB1, *pRGB2;
	int src_ystride2=2*src_ystride;
	int src_y=0;
	int src_uv=0;
	int dst_y=0;
	int halfdst_y=dst_ystride/2;
	for(j=0; j<height2; j++) 
	{   
		yoff=y+src_y;//yoff = y + j * src_ystride2;
		src_y= src_y+src_ystride2;
	
		uoff = u + src_uv;
		voff = v + src_uv;
		src_uv=src_uv+src_uvstride;
		
		pRGB1 = pdst + dst_y;
		pRGB2 = pdst + dst_y + halfdst_y;	
		dst_y=dst_y+dst_ystride;
		

		if (pRGB1 <=0 || pRGB2 <=0)
			return;

		for(i=0; i<width2; i++)
		{

			const unsigned char *tempp;
			tempp=yoff+(i<<1);
			
			yy  = *(tempp);
			
			int tempuo=*(uoff+i);
			int tempvo=*(voff+i);

			ub = u_b_tab[tempuo];
			ug = u_g_tab[tempuo];
			vg = v_g_tab[tempvo];
			vr = v_r_tab[tempvo];
			
			int tempugvg=ug+vg;
			
			b = yy + ub;
			g = yy - tempugvg;
			r = yy + vr;

			if(b > 255) b = 255;
			if(g < 0) g = 0;
			if(r > 255) r = 255;

			rgb = r_2_pix[r] + g_2_pix[g] + b_2_pix[b];

			yy = *(tempp+1);
			b = yy + ub;
			g = yy - tempugvg;
			r = yy + vr;
			if(b > 255) b = 255;
			if(g < 0) g = 0;
			if(r > 255) r = 255;

			*pRGB1++ = (rgb)+((r_2_pix[r] + g_2_pix[g] + b_2_pix[b])<<16);

			const unsigned char *tempp2=tempp+src_ystride;
			yy = *(tempp2);
			b = yy + ub;
			g = yy - tempugvg;
			r = yy + vr;
			if(b > 255) b = 255;
			if(g < 0) g = 0;
			if(r > 255) r = 255;

			rgb = r_2_pix[r] + g_2_pix[g] + b_2_pix[b];

			yy = *(tempp2+1);
			b = yy + ub;
			g = yy - tempugvg;
			r = yy + vr;
			if(b > 255) b = 255;
			if(g < 0) g = 0;
			if(r > 255) r = 255;

			*pRGB2++ = (rgb)+((r_2_pix[r] + g_2_pix[g] + b_2_pix[b])<<16);
		}
	}
}
#endif

void UYVY2RGB24(const unsigned char *psrc, int srcPitch, int width, int height, unsigned char *pdst, int dstPitch)
{
	int i, j;
	int y1, y2, ub, ug, vg, vr;
	const unsigned char *psrcl;
	unsigned char* pdstl;
	int width2;
	
	if(srcPitch < width) return;
		
	width2 = width/2;
	
	for(i=0; i<height; i++)
	{
		pdstl = pdst; psrcl = psrc;
		
		for(j=0; j<width2; j++)
		{
			y1 = psrcl[(j<<2)+1];
			y2 = psrcl[(j<<2)+3];
			ub = u_b_tab[*(psrcl + (j<<2) + 0)];
			ug = u_g_tab[*(psrcl + (j<<2) + 0)];
			vg = v_g_tab[*(psrcl + (j<<2) + 2)];
			vr = v_r_tab[*(psrcl + (j<<2) + 2)];

			int tmp = y1 + vr;		//r1
			*pdstl = tmp > 255 ? 255 : tmp;
			tmp = y1 - ug - vg;	//g1
			pdstl[1] = tmp < 0 ? 0 : tmp;
			tmp = y1 + ub;		//b1
			pdstl[2] = tmp>255 ? 255 : tmp;
			
			tmp = y2 + vr;		//r2
			pdstl[3] = tmp > 255 ? 255 : tmp;
			tmp = y2 - ug - vg;	//g2
			pdstl[4] = tmp < 0 ? 0 : tmp;
			tmp = y2 + ub;		//b2
			pdstl[5] = tmp > 255 ? 255 : tmp;
			

			pdstl += 6;
		}
		psrc += (srcPitch<<1);
		pdst += (dstPitch<<1) + dstPitch;
	}	
}

inline int mkrgb(int r, int g, int b)
{
	int val;
	if(b > 255) val = 0x00FF0000;
	else if(b > 0) val = b << 16;
	if(g > 255) val |= 0xFF00;
	else if(g > 0) val |= (g << 8);
	if(r > 255) val |= 0xFF;
	else if(r > 0) val |= r;
	return val;
}
void YUV420_to_RGB32(const unsigned char *y, const unsigned char *u, const unsigned char *v, 
			int width, int height, int src_ystride, int src_uvstride, 
			int *pdst, int dst_stride)
{
	int i, j;
	int yy, ub, ug, vg, vr;

	int width2 = width/2;
	int height2 = height/2;

	const unsigned char* yoff = y;
	const unsigned char* uoff = u;
	const unsigned char* voff = v;
	int *pRGB1 = pdst, *pRGB2;

	for(j=0; j<height2; j++) 
	{
		pRGB2 = pRGB1 + dst_stride;

		for(i=0; i<width2; i++)
		{
			ub = u_b_tab[*(uoff+i)];
			ug = u_g_tab[*(uoff+i)];
			vg = v_g_tab[*(voff+i)];
			vr = v_r_tab[*(voff+i)];

			yy  = *(yoff+(i<<1));
			*pRGB1++ = mkrgb(yy + vr, yy - ug - vg, yy + ub);

			yy = *(yoff+(i<<1)+1);
			*pRGB1++ = mkrgb(yy + vr, yy - ug - vg, yy + ub);
			
			yy = *(yoff+(i<<1)+src_ystride);
			*pRGB2++ = mkrgb(yy + vr, yy - ug - vg, yy + ub);

			yy = *(yoff+(i<<1)+src_ystride+1);
			*pRGB2++ = mkrgb(yy + vr, yy - ug - vg, yy + ub);
		}

		pRGB1 += dst_stride;
		yoff += 2*src_ystride;
		uoff += src_uvstride;
		voff += src_uvstride;
	}
}
