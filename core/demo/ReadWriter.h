#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "platform_adpt.h"

//流类型
#define RECORD_STREAM_INVALID	0
#define RECORD_STREAM_VIDEO		1
#define RECORD_STREAM_AUDIO		2
#define RECORD_STREAM_INDICES	0xA1
#define RECORD_STREAM_TAGS		0xA2

//文件版本
#define V264FILE_VERSION	0x0103

//帧/包标志
#define STREAMFLAG_KEYFRAME	1

/*附加信息名称*/
#define TAG_DEVICENAME		(const char*)1		//string
#define TAG_HOST		(const char*)2		//string
#define TAG_CHANNELNAME		(const char*)3		//string
#define TAG_CHANNELID		(const char*)4		//uint
#define TAG_STARTTIME		(const char*)5		//unsigned long
#define TAG_ENDTIME		(const char*)6		//unsigned long
#define TAG_DEVICETYPEID	(const char*)7		//unsigned long
//"EVENTx"(x = 0,1,2...)	报警事件
//	内容: 时间 + 类型 + 事件相关数据
//			- IO_EVENT: 时间 + IO_EVENT + 触发通道号(1)
//			- MD_EVENT: 时间 + MD_EVENT
//			- AB_EVENT: 时间 + AB_EVENT + AB事件详细描述(N)
typedef struct {
	DWORD timeStamp;
	UINT  event;
	UINT  inputChannel;		//event = IO_EVENT 时
} EVENTTAG;

typedef struct _Writer {
	BOOL (*BeginWriting)(const char* sFileName, DWORD streamTypes, void** ppData);
	BOOL (*Write)(DWORD streamType, DWORD timeStamp, BYTE *buf, DWORD len, DWORD flag, void *data);
	BOOL (*WriteTag)(const char *TagName, const void *Tag, UINT len, void *data);
	void (*EndWriting)(void *pData);
} WRITER;


typedef struct _FileInfo {
	DWORD	dwDevTypeId;				//Must set to 0 if not supported.
	char	cHost[32];
	char	cDeviceName[32];
	DWORD	dwChannel;
	char	cChannelName[32];
	DWORD	tmStart;	//UTC
	DWORD	tmEnd;		//UTC
	DWORD	dwFileLength;
	DWORD	dwDuration;		//以毫秒计的文件播放时长
	UINT	nSamplesPerSec;
	UINT	nBitsPerSample;
	UINT	nChannels;
} FILEINFO;

typedef struct _Reader {
	DWORD (*Probe)(const char *sFileName, FILEINFO *pFi);
	DWORD (*BeginReading)(const char *sFileName, void **ppData);
	DWORD (*GetFileInfo)(FILEINFO *pFi, void *data);
	DWORD (*GetDownloadProgress)(DWORD *ts, DWORD *len, void *pData);
	DWORD (*LookAhead)(DWORD *streamType, DWORD *timeStamp, DWORD *flag, DWORD *size, void *data);	//获取下一次读操作将返回帧/包的类型信息
	DWORD (*Read)(DWORD *streamType, DWORD *timeStamp, BYTE *buf, /*INOUT*/DWORD *len, DWORD *flag, void *data);
	DWORD (*ReadTag)(const char *TagName, void *Tag, UINT *len, void *data);
	DWORD (*SeekKeyFrame)(DWORD timeStamp, void *data);		//定位到最近靠前的关键帧，返回关键帧的timeStamp. 接口不支持定位应返回到第一帧的位置(timeStamp == 0)
	void (*EndReading)(void *pData);

	struct _Reader *next;
} READER;


WRITER* GetDefaultWriter();
WRITER *GetWriter();
void SetWriter(WRITER *pWrtr);

READER *GetDefaultReader();
READER* GetDefaultRemoteReader();
BOOL RegisterReader(READER *pReader);
READER *FindSuitableReader(const char *sFileName, FILEINFO *pFi);

DWORD DefaultRepair(const char *fn);
//=======================================================================================
//=======================================================================================
#ifdef __cplusplus
}
#endif

