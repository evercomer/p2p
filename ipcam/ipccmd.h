#ifndef __ipccmd_h__
#define __ipccmd_h__

#include "platform_adpt.h"


#define CMD_REBOOT        0x101
//req:
//resp:

#define CMD_ECHO		0x102
//req: char str[]
//resp: char str[]


#define CMD_START_VIDEO		0x110	/// Start live video
//req: struct CmdStartVideoReq
//resp:

#define CMD_STOP_VIDEO		0x111	/// Stop live video
//req:
//resp:

#define CMD_START_AUDIO		0x112	/// Start live audio
//req:
//resp:

#define CMD_STOP_AUDIO		0x113	/// Stop live audio
//req:
//resp:

#define CMD_PTZ_CTRL		0x115
//req: struct CmdPtzCtrlReq
//resp: none

#define CMD_START_TALKBACK  0x116
#define CMD_TALKBACK		0x117
//req: byte0: media type, byte1~n: audio data
//resp: none

#define CMD_STOP_TALKBACK		0x118
//stop Talkback

#define CMD_GET_POWER_FREQ	0x119
//req: int32_t vchn;
//resp: int32_t freq;	//POWERFREQ_xxx

#define CMD_SET_POWER_FREQ	0x11A
//req: struct CmdSetPowerFreqReq
//resp:

#define CMD_GET_VIDEO_PARAMETER		0x11B	/// Get
//req: int32_t chno
//resp: struct dcs_video_param
#define CMD_SET_VIDEO_PARAMETER		0x11C	/// Set brmode/bps/fps/quality...
//req: 	struct CmdSetVideoParameterReq
//resp:

#define CMD_SNAPSHOT		0x11D
//req: struct CmdSnapshotReq
//resp: ushort end_flag;
//      ushort reserved;
//      char data[0];

#define CMD_DOWNLOAD_FILE	0x11E
//req: int32_t filepath_len; //length of file path
//     char filepath[0];
//resp: ushort end_flag;
//      ushort reserved;
//      char data[0];

#define CMD_LIST_WIFI_AP	0x11F
//req:
//resp: struct CmdListWifiApResp

#define CMD_GET_WIFI	0x120
//req:
//resp: struct WifiSettng

#define CMD_SET_WIFI	0x121
//req: struct WifiSetting
//resp:

#define CMD_GET_FLIP	0x122
//req: int32_t vchn;
//resp: int32_t flips;	//combine of VIDEOFLIP_HORZ and VIDEOFLIP_VERT

#define CMD_SET_FLIP	0x123
//req: CmdSetFlipReq
//resp:

#define CMD_CHANGE_PSWD	0x124
//req: char *new_pswd;
//resp:

#define CMD_GET_TIME 0x125
//req:	
//resp: struct DATimeInfo

#define CMD_SET_TIME 0x126
//req:	struct DATimeInfo
//resp: 

/* 
 * playback 
 */
#define CMD_LIST_RECORD    0x130
//req:struct CmdSearchRecordReq
//resp:struct CmdGetListSearchReocrdResp 

#define CMD_LOCATE_AND_PLAY			0x131
//req:struct CmdPlayBackReq 
//resp:

#define CMD_PB_SET_RATE             0x132
//req: enum EnumRateTimes 
//resp:

#define CMD_PB_PAUSEORREPLAY      0x133
//req:enum EnumVideoType 
//resp:

#define CMD_PB_TERMINATE      0x134
//req:CmdPlayBackReq
//resp:

#define CMD_SET_RECORD_PLAN              0x135
//req:struct CmdRecordPlanReq 
//resp:

#define CMD_GET_RECORD_PLAN            0x136//Video program
//req:
//resp:struct CmdGetRecordPlanListResp

#define  CMD_FORMAT_SDC           0x137
//req:      
//resp:


/* Power freq & Video Norm */
#define POWERFREQ_50HZ  0
#define POWERFREQ_60HZ  1
#define VIDEONORM_PAL  0
#define VIDEONORM_NTSC 1

typedef enum { 
	DEVTYPE_PHONE,
	DEVTYPE_PAD,
	DEVTYPE_PC
} DEVTYPE; //Type of access device

enum { 
	P2P_PTZ_STOP = 100, 
	P2P_PTZ_MOVE_UP, 
	P2P_PTZ_MOVE_UP_STOP,
	P2P_PTZ_MOVE_DOWN,
	P2P_PTZ_MOVE_DOWN_STOP,
	P2P_PTZ_MOVE_LEFT,
	P2P_PTZ_MOVE_LEFT_STOP,
	P2P_PTZ_MOVE_RIGHT, 
	P2P_PTZ_MOVE_RIGHT_STOP,
	P2P_PTZ_MOVE_UPLEFT, 
	P2P_PTZ_MOVE_UPLEFT_STOP,
	P2P_PTZ_MOVE_DOWNLEFT, 
	P2P_PTZ_MOVE_DOWNLEFT_STOP,
	P2P_PTZ_MOVE_UPRIGHT, 
	P2P_PTZ_MOVE_UPRIGHT_STOP,
	P2P_PTZ_MOVE_DOWNRIGHT, 
	P2P_PTZ_MOVE_DOWNRIGHT_STOP,
	P2P_PTZ_IRIS_IN, 
	P2P_PTZ_IRIS_IN_STOP, 
	P2P_PTZ_IRIS_OUT, 
	P2P_PTZ_IRIS_OUT_STOP, 
	P2P_PTZ_FOCUS_ON, 
	P2P_PTZ_FOCUS_ON_STOP, 
	P2P_PTZ_FOCUS_OUT, 
	P2P_PTZ_FOCUS_OUT_STOP, 
	P2P_PTZ_ZOOM_IN, 
	P2P_PTZ_ZOOM_IN_STOP, 
	P2P_PTZ_ZOOM_OUT, 
	P2P_PTZ_ZOOM_OUT_STOP, 

	P2P_PTZ_SET_PSP, 
	P2P_PTZ_CALL_PSP, 
	P2P_PTZ_DELETE_PSP, 

	P2P_PTZ_BEGIN_CRUISE_SET, 
	P2P_PTZ_SET_CRUISE, 
	P2P_PTZ_END_CRUISE_SET, 
	P2P_PTZ_CALL_CRUISE, 
	P2P_PTZ_DELETE_CRUISE, 
	P2P_PTZ_STOP_CRUISE, 

	P2P_PTZ_AUTO_SCAN, 
	P2P_PTZ_AUTO_SCAN_STOP,

	P2P_PTZ_RAINBRUSH_START, 
	P2P_PTZ_RAINBRUSH_STOP,
	P2P_PTZ_LIGHT_ON, 
	P2P_PTZ_LIGHT_OFF,

	P2P_PTZ_MAX 
};//ptz


typedef enum {
	WIFI_ENCTYPE_INVALID,
	WIFI_ENCTYPE_NONE,
	WIFI_ENCTYPE_WEP,
	WIFI_ENCTYPE_WPA_TKIP,
	WIFI_ENCTYPE_WPA_AES,
	WIFI_ENCTYPE_WPA2_TKIP,
	WIFI_ENCTYPE_WPA2_AES
} WIFI_ENCTYPE;//the type of wifi

typedef enum {
	RT_ALL,	
	RT_NORMAL , 
	RT_EVENTS_TRIGGERED, //事件触发录像，只作查询参数。具体类型如下：
	RT_MD,
	RT_INPUT, 
	RT_SOUND, 
	RT_OTHER 
}EnumRecordTYPE;

struct CmdStartVideoReq {
	int32_t channel;
	int32_t devType;
	unsigned short quality;	//1~100: lowest~highest
	int32_t mediaChn;
};

struct CmdStartAudioReq {
	int32_t mediaChn;
};

struct CmdPtzCtrlReq {
	int32_t channel;
	int32_t code;	//P2P_PTZ_xxx
	int32_t para1, para2;
};

struct CmdSnapshotReq {
	int32_t channel;
	int32_t quality;
};//Capture

struct dcs_date {
	uint16_t year;
	uint8_t mon;
	uint8_t mday;
} __PACKED__;

struct dcs_datetime {
	uint16_t year;
	uint8_t mon;
	uint8_t mday;
	uint8_t hour;
	uint8_t min;
	uint8_t sec;
	uint8_t dummy;
} __PACKED__;


struct dcs_event {
	int32_t event;
	struct dcs_datetime dtt;
};
struct CmdListEventResp {
	uint32_t n_item;//uint16_t xxxx  n_item;
	struct dcs_event items[0];
};

/*
struct CmdSetVideoParameterReq {//set fps ,kbps...
	int32_t chno;
	struct dcs_video_param video_param;
};
*/

struct CmdSetPowerFreqReq {
	int32_t vchn;
	int32_t freq;	//POWERREQ_xxx
};

#define TEST_MIN(v,h,m) (v.m_mask[h][m/32] & (1<<(m%32)))	//Test to see if hour of h, minute of m has record


struct _tagWifiSetting {
	char essid[32];
	char key[32];
	WIFI_ENCTYPE enctype;
};
#define CmdGetWifiResp _tagWifiSetting
#define CmdSetWifiReq _tagWifiSetting

struct WifiAP {
	char essid[32];
	WIFI_ENCTYPE enctype;
	uint32_t quality;//unsigned short quality;
};
//CMD_LIST_WIFI_AP
struct CmdListWifiApResp {
	int32_t nAP;	// < 0: No wifi nic
	struct WifiAP aps[0];
};

//--------------------------------------
#define VIDEOFLIP_HORZ	0x01
#define VIDEOFLIP_VERT	0x02
struct CmdSetFlipReq {
	int32_t vchn;	//0
	int32_t flips;	//combination of VIDEOFLIP_xxx
};
//----------------------------------------------
#define	P2PET_PB_CLOCK		0x10000
//data: struct dcs_datetime dt;

/* palyback*/
struct CmdPlayBackReq
{
	uint32_t chn;  //chn    
	EnumRecordTYPE  eventType;//event's type               
	struct dcs_datetime tRec;      //from
	UINT timelen;      //s
};//search records

struct dcs_time 
{
	unsigned char hour,minute,second,dummy;
};
/*
struct  CmdSetRecordPlanListResp  //record's list
{
	uint32_t n_item;
	struct CmdRecordPlanReq items[5];// the max is 5
};
struct  CmdGetRecordPlanListResp  //record's list
{
	uint32_t n_item;
	struct CmdRecordPlanReq items[0];
};
*/

#endif
