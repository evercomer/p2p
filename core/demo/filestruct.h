// FileStruct.h: interface for the FileStruct class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_FILESTRUCT_H__7B576E73_39ED_4AE0_8187_9399B64A7B68__INCLUDED_)
#define AFX_FILESTRUCT_H__7B576E73_39ED_4AE0_8187_9399B64A7B68__INCLUDED_


typedef struct _tagDVSEVENT { DWORD type; DWORD dwTime; } DVSEVENT;
typedef
struct _tagVideoFileHeader {
	DWORD dwTag;
	DWORD dwSize;
	DWORD dwVer;
	DWORD dwStreams;
	char  cHost[32];
	char  cDeviceName[32];
	DWORD dwChannel;
	char  cChannelName[32];
	DWORD tmStart;	//UTC
	DWORD tmEnd;	//UTC
	DWORD dwDevTypeId;
	DWORD nSamplesPerSec;
	DWORD nBitsPerSample;
	DWORD nChannels;
	DWORD dwExtra;
} VIDEOFILEHEADER_V102;

#define VIDEOFILEHEADER VIDEOFILEHEADER_V102

#define VALIDATEV264_OK		0
#define VALIDATEV264_BADFMT	1
#define VALIDATEV264_BADVER	2

extern UINT ValidateV264File(const char * sFileName);
extern UINT ValidateV264Header(VIDEOFILEHEADER *pHdr);

#endif // !defined(AFX_FILESTRUCT_H__7B576E73_39ED_4AE0_8187_9399B64A7B68__INCLUDED_)
