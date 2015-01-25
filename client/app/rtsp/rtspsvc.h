#ifndef __session_h__
#define __session_h__

#include "platform_adpt.h"
#include "linux_list.h"
#include "sender.h"
#include "ctp.h"


typedef char SID[20];

typedef enum { RTSP_CLIENT, RTSP_ON_HTTP_CLIENT } CLIENT_TYPE;

typedef struct __tagClient {
	struct list_head  client_list;
	struct list_head  sess_list;	//SESSION
	
	int	sock;
	SID	cltId;
	
	/* rtsp client */
	unsigned long cseq;

	/* The time receiving the last request from client,  
	 *  if no request in 60", the client will be destroyed
	 */
	unsigned int lastRefresh;
} CLIENT;

typedef enum { 
	MEDIAINDEX_VIDEO,
	MEDIAINDEX_AUDIO,
	MEDIAINDEX_MAX
} MEDIAINDEX;

class P2pSession;
typedef struct __tagSESSION 
{           
	struct list_head sess_list;

	int             chn;	/*Video channel of this Session*/
	SID 		sessId;	/*session id*/

	TRANSPORTTYPE	trans_type;

	P2pSession	*pP2pSess;

	/* rtsp, set when SETUP */
	unsigned int	requested_medias;
	unsigned short	client_port[2];//for video and audio
	int		chnid[2];	//interleaved channel id, for video and audio

	RTP_TARGETHOST_S*	pTarget[MEDIAINDEX_MAX];	//Max streams can be sent simultanously
    
} SESSION, *LPSESSION;
//--------------------------------------------------------------------
int LaunchRtspService();
void ExitRtspService();

CLIENT *AddClient(int sock);	//Add client and add it to s_ClientList
void CloseClient(CLIENT *pclt);
void RemoveClient(CLIENT *pclt);
CLIENT *FindClient(const char *clientId);
SESSION *AddSession(CLIENT *clt, uint32 chn, TRANSPORTTYPE tt);
int RemoveSession(SESSION *pSess);
SESSION *FindSession(const char *sessId);
SESSION *FindClientSession(CLIENT *pclt, const char *sessId);
RTP_TARGETHOST_S *AddTarget(const char *sessId, MEDIAINDEX media/*video or audio*/, unsigned long sock_or_addr);
int RemoveTarget(SESSION *pSess, MEDIAINDEX media);
//-----------------------------------------------------------------------

void StopCTPService();

#endif
