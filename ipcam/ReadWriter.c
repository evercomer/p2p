#ifdef WIN32
#include <io.h>
#endif
#include "errdefs.h"
#include "ReadWriter.h"
#include "filestruct.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define sizeofmember(s, m) sizeof(((s*)0)->m)
#ifndef min
#define min(x, y) ((x)>(y)?(y):(x))
#endif

#ifndef SAFE_FREE
#define SAFE_FREE(x) if(x) { free(x); x=NULL; }
#endif

#ifdef __LINUX__
#include <sys/stat.h>
int filelength(int fd)
{
	struct stat _stat;
	if(fstat(fd, &_stat) == 0)
		return _stat.st_size;
	return -1;
}
#endif
///////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
	UINT	bufferSize;
	UINT	dataSize;
	BYTE	*pBuf;
} MEMSTREAM;

void InitMemStream(MEMSTREAM *pMS, UINT size)
{
	pMS->bufferSize = size;
	pMS->dataSize = 0;
	pMS->pBuf = (BYTE*)malloc(size);
}
void UninitMemStream(MEMSTREAM *pMS)
{
	if(pMS->pBuf) 
	{
		free(pMS->pBuf);
		pMS->pBuf = NULL;
	}
}
void MemStream_Write(MEMSTREAM *pMS, void *p, UINT len)
{
	if(pMS->dataSize + len > pMS->bufferSize)
	{
		pMS->pBuf = (BYTE*)realloc(pMS->pBuf, pMS->bufferSize + 2000);
		pMS->bufferSize += 2000;
	}
	memcpy(pMS->pBuf + pMS->dataSize, p, len);
	pMS->dataSize += len;
}
///////////////////////////////////////////////////////////////////////////////////////////////
//OurWriter
typedef struct _OWIndex {
	DWORD timeStamp;
	DWORD offset;
} KEYFRAME_INDEX;

void WriteKeyFrameIndex(MEMSTREAM *pMS, DWORD ts, DWORD off)
{
	KEYFRAME_INDEX kfi;
	kfi.timeStamp = ts;
	kfi.offset = off;
	MemStream_Write(pMS, &kfi, sizeof(kfi));
}

typedef struct {
	DWORD flag;		// = 1
	DWORD extraOffset;	//索引,TAG内容的偏移, 8字节边界对齐
} EXTRADATA;

typedef struct OWData {
	FILE *fp;
	//CString sFileName;
	BOOL	bWriting;
	BOOL	bFirstWrite;
	DWORD	dwTs0, dwLastTs;
	DWORD	streamTypes;

	MEMSTREAM kfidxStream;
	MEMSTREAM tagStream;

	VIDEOFILEHEADER	vfhdr;
} OURWRITERDATA;


static BOOL OurWriter_BeginWriting(const char *sFileName, DWORD streamTypes, void **ppData)
{
	OURWRITERDATA *pData = (OURWRITERDATA*)calloc(sizeof(OURWRITERDATA), 1);
	if( (pData->fp = fopen(sFileName, "wb")) )
	{
		VIDEOFILEHEADER *pHdr = &pData->vfhdr;

		pHdr->dwTag = '462v';
		pHdr->dwSize = sizeof(VIDEOFILEHEADER);
		pHdr->dwVer = V264FILE_VERSION;
		pHdr->dwStreams = streamTypes;

		pHdr->tmStart = time(NULL);

		pHdr->nSamplesPerSec = 8000;
		pHdr->nBitsPerSample = 16;
		pHdr->nChannels = 1;

		pHdr->dwExtra = 64;		//------ !!!

		pData->streamTypes = streamTypes;
		pData->dwTs0 = 0;
		pData->bFirstWrite = TRUE;
		pData->bWriting = TRUE;

		fwrite(pHdr, 1, sizeof(VIDEOFILEHEADER), pData->fp);	//先写入头，如果录像非正常中止，其它信息可以通过扫描文件重建
		fseek(pData->fp, pHdr->dwExtra, SEEK_CUR);

		InitMemStream(&pData->kfidxStream, 4000);
		InitMemStream(&pData->tagStream, 2000);

		*ppData = pData;

		return TRUE;
	}

	free(pData);
	return FALSE;
}

static BOOL OurWriter_Write(DWORD streamType, DWORD timeStamp, BYTE *buf, DWORD len, DWORD flag, void *data)
{
	//TRACE("st:%d, ts:%u\n", streamType, timeStamp);
	OURWRITERDATA *pWrtd = (OURWRITERDATA*)data;
	if(pWrtd && pWrtd->fp && (streamType & pWrtd->streamTypes))
	{
		if(pWrtd->bFirstWrite)
		{
			pWrtd->dwTs0 = timeStamp;
			//pWrtd->vfhdr.tmStart = time(NULL);
			pWrtd->bFirstWrite = FALSE;
		}

		timeStamp -= pWrtd->dwTs0;
		if(timeStamp > 0xFFFF0000) //前包后至。时标溢出前后的值与dwTs0相减其差是正常的
			return FALSE;

		DWORD offset = ftell(pWrtd->fp);
		pWrtd->dwLastTs = timeStamp;
		fwrite(&streamType, 4, 1, pWrtd->fp);
		len += 8;	//包含时标和标志
		fwrite(&len, sizeof(DWORD), 1, pWrtd->fp);
		fwrite(&timeStamp, sizeof(DWORD), 1, pWrtd->fp);
		fwrite(&flag, sizeof(DWORD), 1, pWrtd->fp);
		len -= 8;
		fwrite(buf, 1, len, pWrtd->fp);

		if( streamType == RECORD_STREAM_VIDEO && (flag & STREAMFLAG_KEYFRAME) )
		{
			WriteKeyFrameIndex(&pWrtd->kfidxStream, timeStamp, offset);
		}

		return TRUE;
	}
	return FALSE;
}
static BOOL OurWriter_WriteTag(const char *TagName, const void *Tag, UINT len, void *data)
{
	OURWRITERDATA *pWrtd = (OURWRITERDATA*)data;
	if(!pWrtd || !pWrtd->fp) return FALSE;

	if(TagName == TAG_DEVICENAME) strncpy(pWrtd->vfhdr.cDeviceName, (char*)Tag, sizeof(pWrtd->vfhdr.cDeviceName));
	else if(TagName == TAG_HOST)		strncpy(pWrtd->vfhdr.cHost, (char*)Tag, sizeof(pWrtd->vfhdr.cHost)); 
	else if(TagName == TAG_CHANNELNAME)	strncpy(pWrtd->vfhdr.cChannelName, (char*)Tag, sizeof(pWrtd->vfhdr.cChannelName));
	else if(TagName == TAG_CHANNELID)	pWrtd->vfhdr.dwChannel = (int)Tag;
	else if(TagName == TAG_STARTTIME)	pWrtd->vfhdr.tmStart = (DWORD)Tag;
	else if(TagName == TAG_ENDTIME)		pWrtd->vfhdr.tmEnd = (DWORD)Tag;
	else if(TagName == TAG_DEVICETYPEID)	pWrtd->vfhdr.dwDevTypeId = (DWORD)Tag;
	else {
		MemStream_Write(&pWrtd->tagStream, (void*)TagName, strlen(TagName) + 1);
		MemStream_Write(&pWrtd->tagStream, &len, sizeof(UINT));
		MemStream_Write(&pWrtd->tagStream, (void*)Tag, len);
	}
	return TRUE;
}
static void OurWriter_EndWriting(void *pData)
{
	if(pData)
	{
		OURWRITERDATA *pWrtd = (OURWRITERDATA*)pData;
		FILE *fp = pWrtd->fp;
		pWrtd->fp = NULL;
		pWrtd->bWriting = FALSE;
		if(fp) 
		{
			EXTRADATA extra;
			DWORD dw;

			extra.extraOffset = ftell(fp);
			extra.flag = 1;

			dw = RECORD_STREAM_INDICES;
			fwrite(&dw, sizeof(dw), 1, fp);
			fwrite(&pWrtd->kfidxStream.dataSize, sizeof(DWORD), 1, fp);
			fwrite(pWrtd->kfidxStream.pBuf, 1, pWrtd->kfidxStream.dataSize, fp);

			dw = RECORD_STREAM_TAGS;
			fwrite(&dw, sizeof(dw), 1, fp);
			fwrite(&pWrtd->tagStream.dataSize, sizeof(DWORD), 1, fp);
			fwrite(pWrtd->tagStream.pBuf, 1, pWrtd->tagStream.dataSize, fp);

			//pWrtd->vfhdr.tmEnd = time(NULL);
			pWrtd->vfhdr.tmEnd = pWrtd->vfhdr.tmStart + pWrtd->dwLastTs/1000;
			rewind(fp);
			fwrite(&pWrtd->vfhdr, pWrtd->vfhdr.dwSize, 1, fp);
			fwrite(&extra, sizeof(extra), 1, fp);

			fclose(fp);
		}
		UninitMemStream(&pWrtd->kfidxStream);
		UninitMemStream(&pWrtd->tagStream);

		free(pData);
	}
}

///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
//OurReader
static DWORD OurReader_Probe(const char *sFileName, FILEINFO *pFi);
static DWORD OurReader_BeginReading(const char *sFileName, void **ppData);
static DWORD OurReader_Read(DWORD *streamType, DWORD *timeStamp, BYTE *buf, DWORD *len, DWORD *flag, void *data);
static DWORD OurReader_ReadTag(const char *TagName, void *Tag, UINT *len, void *data);
static BOOL OurReader_Seek(DWORD timeStamp, void *data);
static void OurReader_EndReading(void *pData);

#pragma pack(push, 1)
typedef struct _FrameHeader {
	DWORD timeStamp;
	BYTE  streamType;
	union {
		DWORD flag;
		DWORD ALen;
	};
	DWORD VLen;
} FRAMEHEADER_v102;
#pragma pack(pop)

typedef struct  {
	DWORD  streamType;
	DWORD len;
	DWORD timeStamp;
	DWORD flag;
} FRAMEHEADER_v103;

typedef struct _ORData {
	FILE	*fp;			//
	DWORD	dwFileLength;	//文件长度(字节)
	DWORD	dwDuration;		//以时间记的文件长度, 单位: ms
	EXTRADATA	*pExtra;

	DWORD	dwDataOffset;	//第一包的文件偏移
	DWORD	dwTs0;			//第一包的时标

	UINT	nKfi;
	KEYFRAME_INDEX *pKfi;

	BYTE	*pTagBuf;
	UINT	dwTagBufferSize;
	//DWORD	dwPos, dwTs;	//当前位置

	union {
		FRAMEHEADER_v102 frmhdr102;		//for look ahead
		FRAMEHEADER_v103 frmhdr103;
		BYTE			 clkahd[16];
	};

	VIDEOFILEHEADER vfhdr;
} OURREADERDATA;

static DWORD OurReader_Probe(const char *sFileName, FILEINFO *pFi)
{
	VIDEOFILEHEADER vfhdr;
	FILE *fp;
	DWORD rlt = E_OK;
	
	if(pFi) memset(pFi, 0, sizeof(FILEINFO));

	fp = fopen(sFileName, "rb");
	if(!fp) return E_CANNOTOPENFILE;
	fread(&vfhdr, 1, sizeof(vfhdr), fp);
	if(vfhdr.dwTag != '462v') rlt = E_BADFMT;
	else
	if(vfhdr.dwSize != sizeof(VIDEOFILEHEADER) || vfhdr.dwVer > V264FILE_VERSION 
									|| vfhdr.dwVer < 0x0102) rlt = E_BADVER;
	if(pFi)
	{
		pFi->dwFileLength = filelength(fileno(fp));
		pFi->dwDuration = vfhdr.tmEnd?(vfhdr.tmEnd - vfhdr.tmStart)*1000:0;

		pFi->nChannels = vfhdr.nChannels;
		pFi->nSamplesPerSec = vfhdr.nSamplesPerSec;
		pFi->nBitsPerSample = vfhdr.nBitsPerSample;

		strncpy(pFi->cHost, vfhdr.cHost, sizeof(pFi->cHost));
		strncpy(pFi->cDeviceName, vfhdr.cDeviceName, sizeof(pFi->cDeviceName));
		pFi->dwChannel = vfhdr.dwChannel;
		strncpy(pFi->cChannelName, vfhdr.cChannelName, sizeof(pFi->cChannelName));
		pFi->tmStart = vfhdr.tmStart;
		pFi->tmEnd = vfhdr.tmEnd;
		pFi->dwDevTypeId = vfhdr.dwDevTypeId;
	}

	fclose(fp);
	return rlt;
}
static DWORD OurReader_BeginReading(const char *sFileName, void **ppData)
{
	FILE *fp;
	VIDEOFILEHEADER vfhdr;

	fp = fopen(sFileName, "rb");
	if(!fp) return E_CANNOTOPENFILE;

	fread(&vfhdr, 1, sizeof(vfhdr), fp);
	if(vfhdr.dwTag != '462v') 
	{ 
		fclose(fp); 
		return E_BADFMT; 
	}
	if( (vfhdr.dwSize != sizeof(VIDEOFILEHEADER)) || (vfhdr.dwVer > V264FILE_VERSION) || (vfhdr.dwVer < 0x0102) )
	{
		fclose(fp);
		return E_BADVER;
	}

	OURREADERDATA *pRdd;

	pRdd = (OURREADERDATA*)calloc(sizeof(OURREADERDATA), 1);

	pRdd->dwDuration = (vfhdr.tmEnd - vfhdr.tmStart)*1000;
	if(!pRdd->dwDuration) pRdd->dwDuration = 1000;

	memcpy(&pRdd->vfhdr, &vfhdr, sizeof(vfhdr));

	if(pRdd->vfhdr.dwVer == 0x0103) vfhdr.dwExtra = 64;
	pRdd->pExtra = (EXTRADATA*)malloc(vfhdr.dwExtra);
	fread(pRdd->pExtra, 1, vfhdr.dwExtra, fp);

	pRdd->dwDataOffset = ftell(fp);

	pRdd->dwFileLength = filelength(fileno(fp));

	if(pRdd->vfhdr.dwVer >= 0x0103)
	{
		DWORD st, len,i;
		fseek(fp, pRdd->pExtra->extraOffset, 0);
		for(i=0; i<2; i++)
		{
			if(fread(&st, sizeof(DWORD), 1, fp) != 1) break;
			if(fread(&len, sizeof(UINT), 1, fp) != 1) break;
			if(st == RECORD_STREAM_INDICES)
			{
				pRdd->pKfi = (KEYFRAME_INDEX*)malloc(len);
				if(fread(pRdd->pKfi, 1, len, fp) != len)
				{
					SAFE_FREE(pRdd->pKfi);
					break;
				}
				pRdd->nKfi = len / sizeof(KEYFRAME_INDEX);
			}
			else if(st == RECORD_STREAM_TAGS)
			{
				pRdd->pTagBuf = (BYTE*)malloc(len);
				fread(pRdd->pTagBuf, 1, len, fp);
				pRdd->dwTagBufferSize = len;
			}
		}
	}

	fseek(fp, pRdd->dwDataOffset, 0);

	pRdd->fp = fp;
	*ppData = pRdd;

	return E_OK;
}

static DWORD OurReader_GetFileInfo(FILEINFO *pFi, void *data)
{
	OURREADERDATA *pRdd = (OURREADERDATA*)data;

	pFi->dwFileLength = pRdd->dwFileLength;
	pFi->dwDuration = pRdd->dwDuration;
	pFi->nChannels = pRdd->vfhdr.nChannels;
	pFi->nSamplesPerSec = pRdd->vfhdr.nSamplesPerSec;
	pFi->nBitsPerSample = pRdd->vfhdr.nBitsPerSample;
	pFi->dwDevTypeId = pRdd->vfhdr.dwDevTypeId;

	strncpy(pFi->cHost, pRdd->vfhdr.cHost, sizeof(pFi->cHost));
	strncpy(pFi->cDeviceName, pRdd->vfhdr.cDeviceName, sizeof(pFi->cDeviceName));
	pFi->dwChannel = pRdd->vfhdr.dwChannel;
	strncpy(pFi->cChannelName, pRdd->vfhdr.cChannelName, sizeof(pFi->cChannelName));
	pFi->tmStart = pRdd->vfhdr.tmStart;
	pFi->tmEnd = pRdd->vfhdr.tmEnd;
	pFi->dwDevTypeId = pRdd->vfhdr.dwDevTypeId;

	return E_OK;
}
static DWORD OurReader_LookAhead(DWORD *streamType, DWORD *timeStamp, DWORD *flag, DWORD *size, void *data)
{
	OURREADERDATA *pRdd = (OURREADERDATA*)data;
	if(pRdd->vfhdr.dwVer == 0x102)
	{
		if(pRdd->frmhdr102.streamType == 0)	//No Cach
		{
			if(fread(&pRdd->frmhdr102, sizeof(FRAMEHEADER_v102), 1, pRdd->fp) != 1) return E_EOF;
		}

		*streamType = pRdd->frmhdr102.streamType;
		*timeStamp = pRdd->frmhdr102.timeStamp;
		if(*streamType == RECORD_STREAM_VIDEO)
		{
			*flag = pRdd->frmhdr102.flag;
			*size = pRdd->frmhdr102.VLen;
		}
		else
		{
			*size = pRdd->frmhdr102.ALen;
			*flag = 0;
		}
	}
	else if(pRdd->vfhdr.dwVer >= 0x103)
	{
		if(pRdd->pExtra->extraOffset && ftell(pRdd->fp) >= pRdd->pExtra->extraOffset) 
			return E_EOF;
		if(pRdd->frmhdr103.streamType == 0)
		{
			if(fread(&pRdd->frmhdr103, sizeof(FRAMEHEADER_v103), 1, pRdd->fp) != 1) 
				return E_EOF;
		}
		if(pRdd->frmhdr103.streamType > 100) return E_EOF;

		*streamType = pRdd->frmhdr103.streamType;
		*timeStamp = pRdd->frmhdr103.timeStamp;
		*size = pRdd->frmhdr103.len - 8;
		*flag = pRdd->frmhdr103.flag;
	}

	return E_OK;
}
static DWORD OurReader_Read(DWORD *streamType, DWORD *timeStamp, BYTE *buf, /*INOUT*/DWORD *size, DWORD *flag, void *data)
{
	DWORD len, rlt;
	OURREADERDATA *pRdd = (OURREADERDATA*)data;

	if(pRdd->vfhdr.dwVer == 0x0102)		//对旧文件保持兼容 0x0102
	{
		if(pRdd->frmhdr102.streamType == 0)
		{
			rlt = OurReader_LookAhead(streamType, timeStamp, flag, &len, data);
			if(rlt != E_OK) return rlt;
		}
		else
		{
			*streamType = pRdd->frmhdr102.streamType;
			*timeStamp = pRdd->frmhdr102.timeStamp;
			if(*streamType == RECORD_STREAM_VIDEO)
			{
				*flag = pRdd->frmhdr102.flag;
				len = pRdd->frmhdr102.VLen;
			}
			else
			{
				*flag = 0;
				len = pRdd->frmhdr102.ALen;
			}
		}
		if(*size < len) { *size = len;	return E_BUFFERTOOSMALL; }

		if(*streamType == RECORD_STREAM_VIDEO)
		{
			if(fread(buf, 1, len, pRdd->fp) != len) return E_EOF;
		}
		else if(*streamType == RECORD_STREAM_AUDIO)
		{
			memcpy(buf, &pRdd->frmhdr102.VLen, 4);
			if(fread(buf+4, 1, len-4, pRdd->fp) != (len-4)) return E_EOF;
		}
		else
			fseek(pRdd->fp, pRdd->frmhdr102.ALen - 4, SEEK_CUR);

		pRdd->frmhdr102.streamType = 0;
	}
	else if(pRdd->vfhdr.dwVer >= 0x0103)	//新的文件版本 >= 0x0103
	{
		if(pRdd->pExtra->extraOffset && ftell(pRdd->fp) >= pRdd->pExtra->extraOffset)
			return E_EOF;
		if(pRdd->frmhdr103.streamType == 0)
		{
			rlt = OurReader_LookAhead(streamType, timeStamp, flag, &len, data);
			if(rlt != 0) return rlt;
		}
		else
		{
			len = pRdd->frmhdr103.len - 8;
			*streamType = pRdd->frmhdr103.streamType;
			*timeStamp = pRdd->frmhdr103.timeStamp;
			*flag = pRdd->frmhdr103.flag;
		}
		if(*size < len)	{ *size = len; return E_BUFFERTOOSMALL;	}

		if(*streamType == RECORD_STREAM_VIDEO || *streamType == RECORD_STREAM_AUDIO)
		{
			if(fread(buf, 1, len, pRdd->fp) != len) return E_EOF;
		}
		else
			//fseek(pRdd->fp, len, SEEK_CUR);
			return E_EOF;
		pRdd->frmhdr103.streamType = 0;		//Cached Header 数据无效
	}

	*size = len;
	return E_OK;
}
#if 0
int strnlen(const char *s, int size)
{
	int i;
	for(i=0; i<size; i++) if(!*s) break;
	return i;
}
#endif

static DWORD OurReader_ReadTag(const char *TagName, void *Tag, /*INOUT*/UINT *len, void *data)
{
	int size, off;
	char *pTag;
	OURREADERDATA *pRdd = (OURREADERDATA*)data;

	if(TagName == TAG_DEVICENAME)
	{
		pTag = pRdd->vfhdr.cDeviceName;
		size = strnlen(pRdd->vfhdr.cDeviceName, sizeof(pRdd->vfhdr.cDeviceName));
	}
	else if(TagName == TAG_HOST)
	{
		pTag = pRdd->vfhdr.cHost;
		size = strnlen(pRdd->vfhdr.cDeviceName, sizeof(pRdd->vfhdr.cHost));
	}
	else if(TagName ==  TAG_CHANNELNAME)
	{
		pTag = pRdd->vfhdr.cChannelName;
		size = strnlen(pRdd->vfhdr.cDeviceName, sizeof(pRdd->vfhdr.cChannelName));
	}
	else if(TagName ==  TAG_CHANNELID || TagName == TAG_STARTTIME || TagName == TAG_ENDTIME || TagName == TAG_DEVICETYPEID)
	{
		if(!Tag) { *len = 4; return E_OK; }
		if(*len < 4) { *len = 4; return E_BUFFERTOOSMALL; }
		if(TagName == TAG_CHANNELID) memcpy(Tag, &pRdd->vfhdr.dwChannel, 4);
		else if(TagName == TAG_STARTTIME) memcpy(Tag, &pRdd->vfhdr.tmStart, 4);
		else if(TagName == TAG_ENDTIME)	memcpy(Tag, &pRdd->vfhdr.tmEnd, 4);
		else if(TagName == TAG_DEVICETYPEID) memcpy(Tag, &pRdd->vfhdr.dwDevTypeId, 4);

		return E_OK;
	}
	else
	{
		pTag = NULL;
		for(off = 0; off < pRdd->dwTagBufferSize; )
		{
			char *s = (char*)pRdd->pTagBuf + off;
			off += strlen(s) + 1;
			memcpy(&size, pRdd->pTagBuf + off, sizeof(UINT));
			if(strcmp(s, TagName) == 0)
			{
				pTag = (char*)pRdd->pTagBuf + off + sizeof(UINT);
				break;
			}
			off += sizeof(UINT) + size;
		}
		if(!pTag) return E_TAGNOTEXISTED;
	}

	if(*len < size) return E_BUFFERTOOSMALL;
	*len = size + 1;
	if(Tag)
	{
		memcpy(Tag, pTag, size);
	}

	return E_OK;
}

static DWORD OurReader_SeekKeyFrame(DWORD timeStamp, void *data)
{
	OURREADERDATA *pRdd = (OURREADERDATA*)data;

	memset(pRdd->clkahd, 0, 16);	//LookAhead() 缓冲无效

	if(pRdd->nKfi == 0 || pRdd->vfhdr.dwVer == 0x0102)	//没有索引
	{
		fseek(pRdd->fp, pRdd->dwDataOffset, 0);
		return 0;
	}
	else 
	{
		KEYFRAME_INDEX *pKfi = pRdd->pKfi;
		int low, hi, mid;
		low = mid = 0; hi = pRdd->nKfi - 1;
		while(low < hi)
		{
			 mid = (low + hi)/2;
			if(pKfi[mid].timeStamp > timeStamp) hi = mid - 1;
			else if(pKfi[mid].timeStamp < timeStamp) low = mid + 1;
			else break;
		}
		if(mid < 0) mid = 0;
		hi = pRdd->nKfi - 1;
		if(pKfi[mid].timeStamp < timeStamp) 
			for( ; mid < hi && pKfi[mid].timeStamp < timeStamp; mid++);
		for(; mid > 0 && pKfi[mid].timeStamp > timeStamp; mid--);

		fseek(pRdd->fp, pKfi[mid].offset, 0);
		return pKfi[mid].timeStamp;
	}
}
static void OurReader_EndReading(void *pData)
{
	if(pData)
	{
		OURREADERDATA *pRdd = (OURREADERDATA*)pData;
		fclose(pRdd->fp);
		free(pRdd->pExtra);
		free(pRdd->pKfi);
		free(pRdd->pTagBuf);
		free(pData);
	}
}

//=======================================================================================
///////////////////////////////////////////////////////////////////////////////////////
DWORD DefaultRepair(const char *fn)
{
	OURREADERDATA *pRdd;

	DWORD rlt;
	if( (rlt = OurReader_BeginReading(fn, (void**)&pRdd)) ) return rlt;

	if(pRdd->pKfi) //不需要修复
	{ 
		OurReader_EndReading(pRdd); 
		return 0; 
	}

	DWORD st, ts, len, flag, offset;
	MEMSTREAM kfidxStrm;
	BYTE *buf;
	
	InitMemStream(&kfidxStrm, 4*1024);
	buf = (BYTE*)malloc(64 << 10);

	offset = ftell(pRdd->fp);
	len = 64 << 10;
	while( (rlt = OurReader_Read(&st, &ts, buf, &len, &flag, pRdd)) == 0)
	{
		if( st == RECORD_STREAM_VIDEO && (flag & STREAMFLAG_KEYFRAME) )
		{
			WriteKeyFrameIndex(&kfidxStrm, ts, offset);
		}

		offset = ftell(pRdd->fp);
		len = 64 << 10;
	}
	free(buf);
	OurReader_EndReading(pRdd);
	if(rlt != E_EOF) return rlt;

	VIDEOFILEHEADER vfhdr;
	FILE *fp;
	EXTRADATA extra;
	
	fp = fopen(fn, "r+b");
	fread(&vfhdr, sizeof(vfhdr), 1, fp);
	vfhdr.tmEnd = vfhdr.tmStart + ts / 1000;
	extra.flag = 1;
	extra.extraOffset = filelength(fileno(fp));

	fseek(fp, 0, SEEK_SET);
	fwrite(&vfhdr, sizeof(vfhdr), 1, fp);
	fwrite(&extra, sizeof(extra), 1, fp);
	fflush(fp);

	fseek(fp, 0, SEEK_END);
	flag = RECORD_STREAM_INDICES;
	fwrite(&flag, sizeof(DWORD), 1, fp);
	fwrite(&kfidxStrm.dataSize, sizeof(DWORD), 1, fp);
	fwrite(kfidxStrm.pBuf, 1, kfidxStrm.dataSize, fp);
	UninitMemStream(&kfidxStrm);
	fclose(fp);

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
static WRITER OurWriter = {
	OurWriter_BeginWriting,
	OurWriter_Write,
	OurWriter_WriteTag,
	OurWriter_EndWriting,
};

READER OurReader = {
	OurReader_Probe,
	OurReader_BeginReading,
	OurReader_GetFileInfo,
	NULL,
	OurReader_LookAhead,
	OurReader_Read,
	OurReader_ReadTag,
	OurReader_SeekKeyFrame,
	OurReader_EndReading
};
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
static READER *s_pReaders = NULL;
static WRITER *s_pWriter = &OurWriter;

WRITER *GetWriter()
{
	return s_pWriter;
}

void SetWriter(WRITER *pWrtr)
{
	s_pWriter = pWrtr;
}

BOOL RegisterReader(READER *pRdr)
{
	READER *p = s_pReaders;
	while(p)
	{
		if(p == pRdr) return TRUE;
		p = p->next;
	}
	pRdr->next = NULL;
	s_pReaders = pRdr;
	return TRUE;
}

READER *FindSuitableReader(const char *sFileName, FILEINFO *pFi)
{
	READER *pRdr = s_pReaders;
	while(pRdr)
	{
		if(pRdr->Probe(sFileName, pFi) == E_OK) return pRdr;
		pRdr = pRdr->next;
	}
	return NULL;
}
WRITER* GetDefaultWriter()
{
	return &OurWriter;
}
READER *GetDefaultReader()
{
	return &OurReader;
}
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
