#include <netdb.h>
#include <resolv.h>
#include <ctype.h>
#include "linux_list.h"
#include "ctp.h"
#include "sender.h"
#include "rtspsvc.h"
#include "misc.h"
#include "netbase.h"
#include "mediatyp.h"
#include "p2psess.h"

#define dbg_sess dbg_msg


#define SNDBUFSIZ	(300<<10)
int CreateServiceSocket(int sock_type, unsigned long bind_ip, unsigned short port);
static void *RtspThread(void *);
static void removeTarget(SESSION *pSess, MEDIAINDEX media);

static  int HandlePassiveClient(int reset/*closed*/);
static void _HandleUdpSock(int sock);

volatile BOOL	s_bServing = TRUE;

static pthread_t s_thdCtp = 0;
static int fdRtsp = -1;
int g_RtpSock = -1;

//CLIENT 链, CTP/HTTP/RTSP客户全放入此链
static LIST_HEAD(s_ClientList);		//在CTPThread上下文的访问已加锁
static pthread_mutex_t	s_cltlst_mutex = PTHREAD_MUTEX_INITIALIZER;
int s_userCount = 0;

static unsigned long s_ulSessId;

void LockClientList()
{
	pthread_mutex_lock(&s_cltlst_mutex);
}
void UnlockClientList()
{
	pthread_mutex_unlock(&s_cltlst_mutex);
}

static void _LockClientList() {}
static void _UnlockClientList() {}

BOOL Serving()
{
	return s_bServing;
}

#define DEBUG_SESS
unsigned int getTimeLine() { return time(NULL); }
////////////////////////////////////////////////////////////////////////////////////////////////

/* 
 * 生成全局唯一的标识符，长度为 18  */
void genSessId(char *id)
{
	int i;
	for(i=0; i<14; ) {
		id[i] = rand()%0x4e + 0x31;
		if(!isalnum(id[i] )) continue;
		i++;
	}
	sprintf(id+8, "%04x",(unsigned int)((s_ulSessId++)&0xFFFF));
}

int LaunchRtspService()
{
	//srandom((unsigned int)time(NULL));
	s_ulSessId = rand()%1000;

	INIT_LIST_HEAD(&s_ClientList);

	s_bServing = TRUE;
	pthread_create(&s_thdCtp, NULL, RtspThread, NULL );

	return 0;
}

void WaitRtspThreadTerminate()
{
	void *p;
	if(s_thdCtp) pthread_join(s_thdCtp, &p);
}
void StopRtspService()
{
	s_bServing = FALSE;
}
void ExitRtspService()
{
	StopRtspService();
	WaitRtspThreadTerminate();
}



/*
 *  自由套接字队列, 所有accept的客户端连接先暂存这里。
 *  在收到客户数据包后，根据包的协议类型再作下一步处理
 */
#define MAX_FRESH_CON 32	//HTTP client may use many connections
typedef struct __tagFreeConn {
	int sock;
	char cookie[32];
} FREECONN;
static FREECONN free_cons[MAX_FRESH_CON];
static int add_free_con(int sock)	//return: count of fresh connection
{
	int i;
	for(i=0; i<MAX_FRESH_CON; i++)
	if(free_cons[i].sock == -1) 
	{ 
		free_cons[i].sock = sock; 
		free_cons[i].cookie[0] = 0;
		return i+1; 
	}
	close(free_cons[0].sock);
	for(i=0; i<MAX_FRESH_CON-1; i++)
		free_cons[i] = free_cons[i+1];
	free_cons[MAX_FRESH_CON-1].sock = sock;
	free_cons[MAX_FRESH_CON-1].cookie[0] = 0;
	return MAX_FRESH_CON;
}
static void remove_free_con(int index, unsigned int closeit)
{
	if(index < 0 || index >= MAX_FRESH_CON) return;
	int i;
	if(closeit) close(free_cons[index].sock);

	for(i=index; (i < MAX_FRESH_CON-1) && (free_cons[i+1].sock != -1); i++)
		free_cons[i] = free_cons[i+1];
	free_cons[i].sock = -1;
}
////////////////////////////////////////////////////////////////////////////////////////////

#if 0
static void copy_fdset(fd_set *tgt, fd_set *src, int maxfd)
{
	int i=0;
	FD_ZERO(tgt);
	for(; i<maxfd; i++)
		if(FD_ISSET(i, src)) FD_SET(i, tgt);
}
#endif

#define HTTP_SESSION_TIMEOUT	1800
#define CMD_BUFFER_SIZE 4000
static char ctpbuf[CMD_BUFFER_SIZE];

//检查不活动客户
static void _CheckInactiveClient(unsigned int now)
{
	struct list_head *p, *q;
	_LockClientList();
	list_for_each_safe(p, q, &s_ClientList)
	{
		CLIENT *pclt = list_entry(p, CLIENT, client_list);
		//dbg_sess("now = %u, lastRefresh = %u\n", now, pclt->lastRefresh);
		if(now > pclt->lastRefresh && (now - pclt->lastRefresh > 60)
			       	|| (now < pclt->lastRefresh) )
		{
			RemoveClient(pclt);
		}

	}
	_UnlockClientList();
}
	
void _HandleRTCPSocket(int fd)
{
	struct sockaddr sa;
	socklen_t sa_len;
	int len;
	char buf[200];
	RTCP_RR *prr = (RTCP_RR*)buf;

	sa_len = sizeof(sa);
	len = recvfrom(fd, buf, 200, 0, &sa, &sa_len);
	if(prr->rh.ver == 2 && prr->rh.pt == 201 && prr->rh.rc/* && 4*(ntohs(prr->length)+1) == len*/)
	{
		struct list_head *p;
		_LockClientList();
		list_for_each(p, &s_ClientList)
		{
			CLIENT *pclt = list_entry(p, CLIENT, client_list);
			pclt->lastRefresh = getTimeLine();
		}
		_UnlockClientList();
	}
}

/* 接收客户连接并放入自由连接对列 */
int _AcceptConnection(int sock)
{
	struct sockaddr_in sa;
	int len = sizeof(sa);
	int fd = PA_Accept(sock, (struct sockaddr*)&sa, &len);
	if(fd < 0) 
	{
		perror("accept");
		return -1;//Do more to restart service
	}

	dbg_sess("accept from %s:%d\n", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
	//{ int opt; len = sizeof(opt); getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &opt, &len); dbg_sess("SO_SNDBUF=%d\n", opt); }
	//len = SNDBUFSIZ; setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &len, sizeof(len));

	return add_free_con(fd);
}

extern int HandleRTSPCommand(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body);

/* Function: Receive a full request data
 * Return:  -1 -- recv error, refer errno to see the reason
 *  	     0 -- socket peer closed
 *  	    -2 -- bad request
 *          >0 -- length of request
 * Remark: sock must be confirmed readable. If return > 0, caller should check the ctpbuf[0] to see if 
 * 	  it is '$', which is the first byte of interleaved RTCP packed for RTSP client, value pointed 
 * 	  by popt and preq is invalid in this situation
 */
static int RecvRequest(int sock, REQUESTLINE *preq, REQUESTOPTIONS *popt)
{
	int len, len2 = 0;

	len = recv(sock, ctpbuf, CMD_BUFFER_SIZE, 0);
	if(len <= 0) { if(len < 0) perror("recv"); dbg_sess("ctp request len: %d\n", len); return len; }
	if(ctpbuf[0] == '$') return len;

	ctpbuf[len] = '\0';
	dbg_sess("sock %d:\n%s", sock, ctpbuf);
	if( ParseRequestLine(ctpbuf, preq) || ParseRequestOptions(preq->header, popt) )
	{
		dbg_sess("******** Error request ********\n");
		return -2;
	}
#if 1
	if(popt->content_length)
	while( (popt->content_length == 32767) && (len == popt->body - ctpbuf) 	//first send of http-tunnelled rtsp
			  || ((popt->content_length < 32767) && (popt->content_length > len - (popt->body - ctpbuf))) 
		)
	{
		if( (len2 = timed_recv(sock, ctpbuf+len, CMD_BUFFER_SIZE-len, 500000)) > 0)
		{
			ctpbuf[len + len2] = '\0';
			dbg_sess(ctpbuf+len);
			len += len2;
		}
		else if(popt->content_length != 32767)
		{
			dbg_sess("\nread part2 of request failed\n");
			return -2;
		}
	}
#else
	if(popt->content_length &&
			 (len == popt->body - ctpbuf) 	//first send of http-tunnelled rtsp
			  //popt->content_length < 32767 && (popt->content_length > len - (popt->body - ctpbuf)) 
		)
	{
		if( (len2 = timed_recv(sock, ctpbuf+len, CMD_BUFFER_SIZE-len, 500000)) > 0)
		{
			ctpbuf[len + len2] = '\0';
			dbg_sess(ctpbuf+len);
			len += len2;
		}
		else if(popt->content_length != 32767)
		{
			dbg_sess("\nread part2 of request failed\n");
			return -2;
		}
	}
#endif
	return len;
}

//扫描客户链表
static void _ScanClientList(unsigned int now, const fd_set *rfds, const fd_set *efds)
{
	struct list_head *p, *q;
	int len;
	REQUESTLINE req_line;
	REQUESTOPTIONS reqopt;

	_LockClientList();
	list_for_each_safe(p, q, &s_ClientList)
	{	
		CLIENT *pclt = list_entry(p, CLIENT, client_list);

		if(pclt->sock > 0 && FD_ISSET(pclt->sock, rfds))
		{
			len = RecvRequest(pclt->sock, &req_line, &reqopt);
			dbg_sess("RecvRequest: %d\n", len);
			switch(len)
			{
			case -1:	//len may be -1 when peer closed
			case 0:
				if(len == -1 && (errno == EINTR || errno == EAGAIN)) continue;
				RemoveClient(pclt);
				break;
			case -2:
				dbg_sess("parse request error.\n");
				break;
			default:
				ctpbuf[len] = '\0';
				if(ctpbuf[0] == '$') //RTCP report
				{
					pclt->lastRefresh = getTimeLine();
				}
				else
				{
					unsigned int _now = getTimeLine();
					pclt->lastRefresh = _now;

					if(reqopt.content_length && reqopt.content_length < strlen(reqopt.body))
					{
						dbg_sess("receive request in second call of recv(...)\n");
						recv(pclt->sock, ctpbuf+len, CMD_BUFFER_SIZE-len, 0);
					}
					if(strcmp("RTSP/1.0", req_line.proto_ver) == 0)
					{
						HandleRTSPCommand(pclt, &req_line, &reqopt, reqopt.body);
					}
				}
			}//end of switch
		}
		else 
		{
			if(now - pclt->lastRefresh > 60)
			{
				RemoveClient(pclt);
			}
		}
	}//list_for_each...
	_UnlockClientList();
}

/* 扫描自由连接队列
 * 基本原则是：一个客户可以同时有多个HTTP连接，这些连接在free_cons里排队；
 * 		CTP客户（同时最多）只能有一个CTP连接，CTP连接套接字保存在CLIENT结构中 
 * 		HTTP客户允许有多个CTP连接，套接字保存在free_cons里,只有一个可以请求图像 ?
 */
static void _ScanFreshConnection(unsigned int now, const fd_set *rfds, const fd_set *efds)
{
	int i, len;
	for(i=0; (i < MAX_FRESH_CON) && (free_cons[i].sock != -1); )
	{
		if(FD_ISSET(free_cons[i].sock, efds)) { remove_free_con(i, 1); continue; }
		if(!FD_ISSET(free_cons[i].sock, rfds)) { i++; continue; }

		REQUESTLINE req_line;
		REQUESTOPTIONS reqopt;
		len = RecvRequest(free_cons[i].sock, &req_line, &reqopt);
		if(len <= 0 || ctpbuf[0] == '$')
		{
			dbg_sess("Delete Pending %dth connecion: %d\n", i, free_cons[i].sock);
			remove_free_con(i, 1);
			continue;
		}
		ctpbuf[len] = '\0';

		int rlt, sock = free_cons[i].sock;
		CLIENT *pclt = NULL;

		if(strncmp(req_line.proto_ver, "RTSP/1.", 7) == 0)
		{
			remove_free_con(i, 0);
			if(strcmp("OPTIONS", req_line.method) && strcmp("DESCRIBE", req_line.method))
			{
				close(sock);
				continue;
			}

			pclt = AddClient(sock);

			pclt->lastRefresh = now;
			pclt->cseq = reqopt.cseq - 1;	//For VLC, Cseq is not reset to 0 when TEARDOWN
			rlt = HandleRTSPCommand(pclt, &req_line, &reqopt, reqopt.body);
			if(rlt)
			{
				sprintf(ctpbuf, "RTSP/1.0 %d %s\r\n\r\n", rlt, CTPReasonOfErrorCode(rlt));
				dbg_sess("HandleRTSPCommand: %s", ctpbuf);
				send(sock, ctpbuf, strlen(ctpbuf), 0);
				RemoveClient(pclt);	//OPTIONS & DESCRIBE should not be failed
			}
		}

		i++;
	}
}
//---------------------------------------------------------------------
void addfd(int fd, fd_set *set, int *maxfd)
{
	if(fd > -1)
	{
		FD_SET(fd, set);
		if(fd > *maxfd) *maxfd = fd;
	}
}
int initFdSet(fd_set *set, int *maxFd)
{
	int i, n = 0, maxfd = -1;
	struct list_head *p;

	FD_ZERO(set);
	for(i=0; i < MAX_FRESH_CON && free_cons[i].sock > 0; i++)
	{
		FD_SET(free_cons[i].sock, set);
		if(maxfd < free_cons[i].sock) maxfd = free_cons[i].sock;
		n++;
	}
	list_for_each(p, &s_ClientList)
	{
		CLIENT *pclt = list_entry(p, CLIENT, client_list);
		if(pclt->sock >= 0)
		{
			FD_SET(pclt->sock, set);
			if(pclt->sock > maxfd) maxfd = pclt->sock;
			n++;
		}
	}
	addfd(fdRtsp, set, &maxfd);
	n ++;

	*maxFd = maxfd;
	return n;
}

static void *RtspThread(void *data)
{
	int i, fdRtcp;

	fdRtsp = CreateServiceSocket(SOCK_STREAM, 0, 5540);
	g_RtpSock = CreateServiceSocket(SOCK_DGRAM, 0, SERVER_RTP_PORT);
	i = SNDBUFSIZ/*Max is 0x32800*/; 
	setsockopt(g_RtpSock, SOL_SOCKET, SO_SNDBUF, &i, sizeof(int));

	fdRtcp = CreateServiceSocket(SOCK_DGRAM, 0, SERVER_RTP_PORT+1);

	for(i=0; i<MAX_FRESH_CON; i++) { free_cons[i].sock = -1; free_cons[i].cookie[0] = 0; }

	s_bServing = TRUE;
	while(s_bServing)
	{
		int rlt, maxfd, n_fd;
		struct timeval tv;
		fd_set rfds, efds;
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		n_fd = initFdSet(&rfds, &maxfd);
		addfd(fdRtcp, &rfds, &maxfd);
		memcpy(&efds, &rfds, sizeof(rfds));
		rlt = select(maxfd+1, &rfds, NULL, &efds, &tv); 

		unsigned int now = getTimeLine();

		if(rlt < 0) { if(errno == EINTR) continue;  perror("RtspThread select"); printf("n_fd = %d\n", n_fd); s_bServing = FALSE; break; }
		if(rlt == 0)
		{
			_CheckInactiveClient(now); 
			continue; 
		}

		if(fdRtsp > 0 && FD_ISSET(fdRtsp, &rfds)) _AcceptConnection(fdRtsp);
		if(fdRtcp > 0 && FD_ISSET(fdRtcp, &rfds)) _HandleRTCPSocket(fdRtcp);

		_ScanClientList(now, &rfds, &efds);
		_ScanFreshConnection(now, &rfds, &efds);
	}//while

	struct list_head *p, *q;
	_LockClientList();
	list_for_each_safe(p, q, &s_ClientList)
	{
		CLIENT *pclt = list_entry(p, CLIENT, client_list);
		RemoveClient(pclt);
	}
	_UnlockClientList();
	if(fdRtsp > 0) close(fdRtsp);
	if(g_RtpSock > 0) close(g_RtpSock);

	dbg_sess("RtspThread terminated\n");
	return NULL;
}

//* Add client and add it to s_ClientList
CLIENT *AddClient(int sock)	
{
	CLIENT *pclt = (CLIENT*)calloc(sizeof(CLIENT), 1);
	INIT_LIST_HEAD(&pclt->sess_list);
	pclt->sock = sock;

	pclt->lastRefresh = getTimeLine();
	genSessId(pclt->cltId);

	LockClientList();
	list_add_tail(&pclt->client_list, &s_ClientList);
	UnlockClientList();

	s_userCount++;

	return pclt;
}

extern int ss_sscanf(char seperator, const char *buf, const char *fmt, ...);

CLIENT *FindClient(const char *clientId)
{
	struct list_head *p;
	list_for_each(p, &s_ClientList)
	{
		CLIENT *pclt = list_entry(p, CLIENT, client_list);
		if(strcmp(pclt->cltId, clientId) == 0) return pclt;
	}
	return NULL;
}

void CloseClient(CLIENT *pclt)
{
	struct list_head *p, *q;
	list_for_each_safe(p, q, &pclt->sess_list)
	{
		SESSION *psess = list_entry(p, SESSION, sess_list);
		RemoveSession(psess);
	}
	if(pclt->sock > 0)
	{
		close(pclt->sock);
		pclt->sock = -1;
	}

	dbg_sess("------------>CloseClient.\n");
}
void RemoveClient(CLIENT *pclt)
{
	LockClientList();
	CloseClient(pclt);
	list_del(&pclt->client_list);
	UnlockClientList();

	free(pclt);
	s_userCount--;
}
SESSION *AddSession(CLIENT *clt, uint32 vchn, TRANSPORTTYPE tt)
{
	SESSION *psess = (SESSION*)calloc(sizeof(SESSION), 1);
	psess->chn = vchn;
	psess->trans_type = tt;
	genSessId(psess->sessId);
	list_add_tail(&psess->sess_list, &clt->sess_list);
	return psess;
}

static void removeTarget(SESSION *pSess, MEDIAINDEX media)
{
	dbg_sess("removeTarget: media=%d\n", media);
	RTP_TARGETHOST_S *ptgt = pSess->pTarget[media];
	if(ptgt)
	{
		RTSPSENDER *pSender = &((RTSPSENDER*)pSess->pP2pSess->m_pRtspSender)[media]; 
		
		pthread_mutex_lock(&pSender->mutex);
		list_del(&ptgt->target_list);
		pthread_mutex_unlock(&pSender->mutex);

		//if(pSess->trans_type == TRANSPORT_RTSP && ptgt->sock > 0) close(ptgt->sock);
		//if(ptgt->my_sndbuf) free(ptgt->my_sndbuf);
		free(ptgt);
		pSess->pTarget[media] = NULL;
		dbg_sess("removeTarget 4\n");
	}
}

int RemoveSession(SESSION *pSess)
{	
	int i;
	for(i=0; i<MEDIAINDEX_MAX; i++)
	{
		if(pSess->pTarget[i]) { removeTarget(pSess, (MEDIAINDEX)i); }
	}
	list_del(&pSess->sess_list);
	free(pSess);
	dbg_sess("session removed\n");
	return 0;
}

SESSION *FindSession(const char *sessId)
{
	struct list_head *p;
	if(!sessId) return NULL;
	list_for_each(p, &s_ClientList)
	{
		struct list_head *ps;
		CLIENT *pclt = list_entry(p, CLIENT, client_list);
		list_for_each(ps, &pclt->sess_list)
		{
			SESSION *psess = list_entry(ps, SESSION, sess_list);
			if(strcmp(psess->sessId, sessId) == 0) return psess;
		}
	}
	return NULL;
}

SESSION *FindClientSession(CLIENT *pclt, const char *sessId)
{
	struct list_head *ps;
	if(!sessId) return NULL;
	list_for_each(ps, &pclt->sess_list)
	{
		SESSION *psess = list_entry(ps, SESSION, sess_list);
		if(strcmp(psess->sessId, sessId) == 0) return psess;
	}
	return NULL;
}

RTP_TARGETHOST_S *AddTarget(const char *sessId, MEDIAINDEX media/*video or audio*/, unsigned long sock_or_addr)
{
	if(media >= MEDIAINDEX_MAX)
	{
		dbg_sess("media:%d >= MEDIAINDEX_MAX: %d \n", media);
		return NULL;
	}
	SENDERTYPE st=SENDERTYPE_RTP; //TODO
	SESSION *psess = FindSession(sessId);
	if(!psess) { dbg_sess("Can't find sessid: %s\n", sessId); return NULL; }
	if(!psess->pP2pSess) { dbg_sess("SETUP not called, pP2pSess is NULL.\n"); return NULL; }
	if(psess->pTarget[media]) { dbg_sess("AddTarget: Target already existed\n"); return psess->pTarget[media]; }
	
	dbg_sess("AddTarget: trans_type = %d, media = %d", psess->trans_type, media);
	RTP_TARGETHOST_S *ptgt = (RTP_TARGETHOST_S*)calloc(sizeof(RTP_TARGETHOST_S), 1);
	ptgt->trans_type = psess->trans_type;
	switch(psess->trans_type)
	{
	case TRANSPORT_RTSP:
		ptgt->sock = sock_or_addr; 
		{
			struct timeval tv = { 0, 500000 };
			int opt, optlen=sizeof(int);
			opt = SNDBUFSIZ/*Max is 0x32800*/; 
			setsockopt(ptgt->sock, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(int));
			//opt = 1; setsockopt(ptgt->sock, IPPROTO_RTSP, TCP_NODELAY, &opt, sizeof(int));
			if(setsockopt(ptgt->sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) perror("setsockopt SO_SNDTIMEO");

			opt = sizeof(struct sockaddr_in);
			PA_GetPeerName(ptgt->sock, (struct sockaddr*)&ptgt->remote_addr, &opt);
		} 
		if(psess->trans_type == TRANSPORT_RTSP) st = SENDERTYPE_RTSP;
		else st = SENDERTYPE_RTSP;

		break;

	case TRANSPORT_UDP: 
		ptgt->sock = -1;
		memcpy(&ptgt->remote_addr, (void*)sock_or_addr, sizeof(struct sockaddr_in)); 
		//ptgt->hostState = TARGETSTATE_REQ_IFrame;
		st = SENDERTYPE_RTP;
		break;
	}

	dbg_sess("remote host: %s:%d\n", inet_ntoa(ptgt->remote_addr.sin_addr), ntohs(ptgt->remote_addr.sin_port));
	if(media == MEDIAINDEX_VIDEO)
	{
		ptgt->hostState = TARGETSTATE_REQ_IFrame;
	}
	else
		ptgt->hostState = TARGETSTATE_Sending;
	

	RTSPSENDER *pSender = &((RTSPSENDER*)psess->pP2pSess->m_pRtspSender)[st];
	pthread_mutex_lock(&pSender->mutex);
	list_add_tail(&ptgt->target_list, &pSender->target_list);
	pthread_mutex_unlock(&pSender->mutex);
	
	psess->pTarget[media] = ptgt;

	return ptgt;
}

int RemoveTarget(SESSION *pSess, MEDIAINDEX media)
{
	if(pSess->pTarget[media])
	{
		removeTarget(pSess, media);
	}
	return 0;
}

//-------------------------------------------------------------------

