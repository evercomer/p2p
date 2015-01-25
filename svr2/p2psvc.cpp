#include "p2pbase.h"
#include "svrdb.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
//#include <event2/event.h>
#include <event2/event-config.h>
#include <event.h>
#include <stdarg.h>
#include "linux_list.h"
#include "netbase.h"
#include "relaysvr.h"
//#include "threadpool.h"
#include "p2psvc.h"
#include "p2plog.h"
#include "timerq.h"


#define MAX_CONCURRENT_REQUESTS		1024	
#define AUTH_ON_DEVICE	0
/** Paragraph for events callback
 
  
                                        get session_init response
                                        from callee
     udp_sock{cb_upd} ------------------------->OK
           |              
           | get session_init 
           | from caller
          \|/
    handle_sc_sess_init()
           |
           | send ack back to caller;             
           | (queue wait node;
           | send notification)
           |
          \|/
        cb_sess_noti_timeouted
           |          /|\
           |-----------|
           |    re-send notification 
           |
          \|/
        REQUEST FAILED



                            OK
                            /|\
                             |
                             | got session_init response
                             |
                             |                       timeout
                         device-conn{cb_device_conn} ----------> closesocket/eveent_free()
                             |
                             | accept
                             |
                     tcp_sock_noti{cb_accept2}

 */
struct svc_stat {
	unsigned int t_run;	//运行时间
	unsigned int n_active_dev;

	unsigned int n_acc; //访问数
	unsigned int n_avg_aps; //最近n秒的平均访问量
	unsigned int n_peak_aps; //peak value of access-per-second
};
//------------------------------------------------------------------

// ** 设备（或客户端）应答等待队列 **
//
// ** 目前用于处理设备端的开始会话／连接应答
//
// ** 服务器可能同时向多个设备发出会话／连接开始请求，
//   应答的到来是异步的，要靠sess_id/conn_id作区分。
//

typedef
struct {
	struct list_head list;

	struct event *_event;	//event object used by cb_sess_noti_timeouted

	/* request(session_init) from caller */
	int sock_c;	                    //socket who received request
	struct p2pcore_addr addr_c;     //address the request from
	unsigned int tid_c;	            //trans id of request
	char	sn[LENGTH_OF_SN];

	BOOL	bFastSess;				//caller will be acked with callee's heart-beat address immediately, other than wait for a new one, and
									//callee will punch with the main port, other than create a new socket to reply and punch
	union { struct p2pcore_header dh; struct p2pcore_session_init psi; char buff[256]; } si_ack; //S->C

	/* notification to callee */
	unsigned int tid_noti;          //transaction id of notification to callee
	int	try_cnt;


	int udp_sock;                   //udp sock used to send notification, copy from ipcam object
	struct p2pcore_addr ext_addr;   //address the notification should send to

	int i_relaysvr;
	struct nat_info ipc_stun;
	uint8_t sess_id[LENGTH_OF_SESSION_ID];
	union { struct p2pcore_session_init bsn; uint8_t buf[256]; } si_noti; //S->D

} SESSREQWAITNODE;

static PA_SPIN spin_transId;
static uint32_t s_transId;	//id for the session request sent to callee
#define MAX_LOCAL_INTERFACE	10
static int n_sock; //= n_local_ip
static uint32_t local_ips[MAX_LOCAL_INTERFACE];
static evutil_socket_t udp_socks[MAX_LOCAL_INTERFACE];
static evutil_socket_t tcp_sock_noti = INVALID_SOCKET;	//accept connections from device(tcp p2p session response from device)

struct idx_cnt {
	int idx;
	int cnt;
};
struct IpPair {
	uint32_t local_ip;
	uint32_t ext_ip;
};
struct conn_to_relayer {
	PA_SOCKET sock; //到relayer的长连接
	struct event evt;
	int ok;
};

struct _Relayers {
	int count;
	struct idx_cnt *pIdxcnt;
	struct IpPair *pIptbl;
	struct conn_to_relayer *conn;
};
static struct _Relayers relayers;

static LIST_HEAD(__s_wait_list); //list of SESSREQWAITNODE(waiting for response)
static LIST_HEAD(__sWaitList2);	 //list of SESSREQWAITNODE, wait for response of OP_SC_SESSION_BEGIN from caller 
static LIST_HEAD(__sIpcamList);  //list of all dcs_ipcam objects
static PA_MUTEX __s_wait_list_mutex, __sWaitListMutex2;
static PA_MUTEX __s_ipcam_map_mutex;
static BOOL volatile s_bRun = TRUE;
static BOOL s_bAllowFastSess = FALSE;

static struct event_base *base0;
static struct p2psvc_cb *_p2psvc_cb;
#define _CALLCB(f) if(_p2psvc_cb && _p2psvc_cb->f) _p2psvc_cb->f
#define _CALLCBRI(f) (!_p2psvc_cb || !_p2psvc_cb->f)?0:_p2psvc_cb->f

#define LOCK_WAIT_LIST PA_MutexLock(__s_wait_list_mutex)
#define UNLOCK_WAIT_LIST PA_MutexUnlock(__s_wait_list_mutex)
#define LOCK_WAIT_LIST2 PA_MutexLock(__sWaitListMutex2)
#define UNLOCK_WAIT_LIST2 PA_MutexUnlock(__sWaitListMutex2)
#define LOCK_IPCAM_MAP PA_MutexLock(__s_ipcam_map_mutex)
#define UNLOCK_IPCAM_MAP PA_MutexUnlock(__s_ipcam_map_mutex)


static void scan_wait_queue(const struct sockaddr_in* peer, const struct p2pcore_session_init *pdbsa);
static int GetRelayServer(int i_relaysvr, unsigned long local_ip, struct p2pcore_addr *paddr);
static int SendRelayNotification(int iSvr, const uint8_t* sess_id);
static int handle_ipcam_charge(int sock, const struct p2pcore_charge *pdc);
static int handle_ipcam_activate(int sock, const struct p2pcore_activate_p2p *pdc);
static void cb_device_conn(PA_SOCKET fd, short what, void *arg);
static SESSREQWAITNODE * prepare_wait_node(int sock, const struct p2pcore_session_init *psr);
static void send_sess_init_noti(SESSREQWAITNODE *pnode);
static void connect_to_relayer(int index);
static void send_sess_begin_to_caller(SESSREQWAITNODE *pnode);
static void handle_heart_beat(int sk, const struct p2pcore_i_am_here *pdi, struct sockaddr_in *psai, time_t now);
static void handle_cs_sess_init(int sock, struct p2pcore_session_init *pcsi, const struct sockaddr_in *sai);
static void scan_wait_queue2(const struct sockaddr_in *sai, const struct p2pcore_session_init *pdbsa);
static void handle_query_address(int sock, struct sockaddr_in *sai);

//------------------------------------------------------------------
struct event * event_new_selfptr(struct event_base *base, int fd, int what, event_callback_fn cb) 
{
	struct event *e = event_new(base, 0, 0, cb, NULL); 
	event_set(e, fd, what, cb, e);
	return e;
}

static void _getTime(char *strTime)
{
	char sf[16];
#ifdef WIN32
	SYSTEMTIME t;
	GetLocalTime(&t);
	sprintf(sf, "%.06f", t.wMilliseconds/1000.0);
	sprintf(strTime, "%02d:%02d:%02d.%s", t.wHour, t.wMinute, t.wSecond, sf+2);
#else
	struct timeval tv;
	struct tm _tm;
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &_tm);
	sprintf(sf, "%.06f", tv.tv_usec/1000000.0);
	sprintf(strTime, "%02d:%02d:%02d.%s", _tm.tm_hour, _tm.tm_min, _tm.tm_sec, sf+2);
#endif
}

void gen_challenge(uint8_t c[LENGTH_OF_CHALLENGE])
{
	int i;
	for(i=0; i < LENGTH_OF_CHALLENGE; i++)
		c[i] = rand()&0xFF;
}

//tcp connection from callee
static void cb_accept2(PA_SOCKET fd, short what, void *arg)
{
	struct sockaddr_in sai;
	socklen_t sa_len;

	sa_len = sizeof(sai);
	int sk = accept(tcp_sock_noti, (struct sockaddr*)&sai, &sa_len);
	if(sk > 0)
	{
		struct timeval tv = { 2, 0 };
		struct event *ev = event_new_selfptr(base0, sk, EV_TIMEOUT|EV_READ, cb_device_conn);
		event_add(ev, &tv);
	}
}


//callback for tcp_sock_noti
static void cb_sess_noti_timeouted(evutil_socket_t fd, short what, void *arg)
{
	SESSREQWAITNODE *pnode = (SESSREQWAITNODE*)arg;
	if(pnode->try_cnt < 6)
	{
		send_sess_init_noti(pnode);
		pnode->try_cnt ++;
	}
	else
	{
		Log(__FUNCTION__, "timeouted");
		list_del(&pnode->list);
		event_del(pnode->_event);
		event_free(pnode->_event);
		free(pnode);
	}
}

static void cb_sess_begin_timeouted(evutil_socket_t fd, short what, void *arg)
{
	SESSREQWAITNODE *pnode = (SESSREQWAITNODE*)arg;
	if(pnode->try_cnt < 6)
	{
		send_sess_begin_to_caller(pnode);
		pnode->try_cnt ++;
	}
	else
	{
		event_del(pnode->_event);
		event_free(pnode->_event);
		list_del(&pnode->list);
		free(pnode);
	}
}

#if 0
static void cb_client_conn(PA_SOCKET sock, short what, void *arg);
//client connection cb
void cb_client_conn(PA_SOCKET sock, short what, void *arg)
{
	struct event *me = (struct event*)arg;
	if(what & EV_READ) 
	{
		union {
			struct p2pcore_challenge dc;
			struct p2pcore_session_init sr;
			uint8_t buf[512];
		} ur;
		int len;
	       
		len = PA_Recv(sock, &ur, sizeof(ur), 0);
		dbg_msg("PA_Recv tcp req, op = %d\n", ur.dc.P2PCORE_OP(&dh));
		if(len < sizeof(struct p2pcore_header) || !check_p2pcore_header(&ur.dc.dh) || ur.dc.dh.st != ST_CALLER)
			goto finish_conn;

		if(len == sizeof(ur.dc) && ur.dc.P2PCORE_OP(&dh) == OP_GET_CHALLENGE)
		{
			if(ur.dc.dh.st == ST_CALLER)
				;//handle_client_challenge(sock, &ur.dc);
		}
		else if(ur.dc.P2PCORE_OP(&dh) == OP_CHARGE && ur.dc.dh.st == ST_CALLEE)
		{
			//handle_ipcam_charge(sock, (struct p2pcore_charge*)&ur);
		}
		else if(ur.dc.P2PCORE_OP(&dh) == OP_ACTIVATE_P2P && ur.dc.dh.st == ST_CALLEE)
		{
			handle_ipcam_activate(sock, (struct p2pcore_activate_p2p*)&ur);
		}
	}
	//else if(what & EV_TIMEOUT) ...

finish_conn:
	PA_SocketClose(sock);
	event_free(me);
}
#endif

void cb_udp(PA_SOCKET sk, short what, void *arg)
{
	int len, sa_len;
	struct sockaddr_in sai_from;
	union {
		struct p2pcore_header dh;
		struct p2pcore_i_am_here di;
		struct p2pcore_session_init pcsi;
		char buf[1000];
	};
	unsigned int now = 0;

	while(1)
	{
		sa_len = sizeof(sai_from);
		len = PA_RecvFrom(sk, buf, 1000, 0, (struct sockaddr*)&sai_from, (socklen_t*)&sa_len);
		if(len < 0) return; //EAGAIN

		if(len < sizeof(struct p2pcore_header) || !check_p2pcore_header(&dh)) continue;


		//Response from ipcam or client
		if(__LIKELY(dh.st == ST_CALLEE))
		{
			if(__LIKELY(P2PCORE_OP(&dh) == OP_DS_IAMHERE))
			{
				if(now == 0) now = time(NULL);
				handle_heart_beat(sk, &di, &sai_from, now);
			}
			else if(P2PCORE_OP(&dh) == OP_SD_SESSION_NOTIFY)
			{
				Log(__FUNCTION__, "SESSION_NOTIFY response from %s[%s:%d]", pcsi.sn, inet_ntoa(sai_from.sin_addr), ntohs(sai_from.sin_port));
				scan_wait_queue(&sai_from, &pcsi);
			}
		}
		else if(dh.st = ST_CALLER)
		{
			if(__LIKELY(P2PCORE_OP(&dh) == OP_CS_SESSION_INIT && dh.cls == CLS_REQUEST))
			{
				handle_cs_sess_init(sk, &pcsi, &sai_from);
			}
			else if(P2PCORE_OP(&dh) == OP_SC_SESSION_BEGIN && dh.cls == CLS_RESPONSE)
			{
				Log(__FUNCTION__, "SESSION_BEGIN response from %s:%d", inet_ntoa(sai_from.sin_addr), ntohs(sai_from.sin_port));
				scan_wait_queue2(&sai_from, &pcsi);
			}
			else if(P2PCORE_OP(&dh) == OP_QUERY_ADDRESS)
			{
				handle_query_address(sk, &sai_from);
			}
		}
	}
}

void cb_relayconn(PA_SOCKET fd, short what, void *arg)
{
	int err;
	char ips[16];
	struct conn_to_relayer *conn = &relayers.conn[(int)arg];
	struct IpPair *pair = &relayers.pIptbl[relayers.pIdxcnt[(int)arg].idx];

	if(what == EV_WRITE)
	{
		socklen_t optlen = sizeof(int);
		if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &optlen)<0)
					perror("getsockopt");
		else if(err == 0)
		{
			struct p2pcore_header dh;
			init_p2pcore_header(&dh, ST_SERVER, OP_DECLARE, CLS_REQUEST, 0, 0, 0);
			if(PA_Send(conn->sock, &dh, sizeof(dh), MSG_NOSIGNAL) > 0) //send something for verification
			{
				Log(__FUNCTION__, "Connect to relayer %s ok", IP2STR(pair->ext_ip, ips));
				conn->ok = 1;
				return;
			}
			else
				err = PA_SocketGetError();
		}
	}

	if(what == EV_TIMEOUT) err = ETIME;

	Log(__FUNCTION__, "Connect to relayer %s failed: %s", IP2STR(pair->ext_ip, ips), strerror(err));
	PA_SocketClose(conn->sock);
	TimerQueueQueueItem(g_pSlowTq, (SERVICEFUNC)connect_to_relayer, arg, 5000, "");
}

void cb_relayconn_read(PA_SOCKET fd, short what, void *arg)
{
	struct conn_to_relayer *conn = &relayers.conn[(int)arg];
	if(what == EV_TIMEOUT)
	{
		PA_SocketClose(conn->sock);
		connect_to_relayer((int)arg);
	}
	else
	{
		/* The data from relayer is useless at current, 
		 * it's only a way to know the relayer is still alive 
		 */
		union { struct p2pcore_header dh; char buff[256]; };
		int len = PA_Recv(conn->sock, buff, sizeof(buff), 0);
		if(len > 0)
			Log(__FUNCTION__, "relayer's response, op: %d", P2PCORE_OP(&dh));
		else if(len < 0 && PA_SocketGetError() == EAGAIN)
			return;
		if(len < sizeof(struct p2pcore_header) || !check_p2pcore_header(&dh) || P2PCORE_OP(&dh) != OP_RELAY_NOTIFY)
		{
			PA_SocketClose(conn->sock);
			connect_to_relayer((int)arg);
		}
	}
}

//cb for device's connection
void cb_device_conn(PA_SOCKET fd, short what, void *arg)
{
	struct event *me = (struct event*)arg;

	if(what & EV_READ) 
	{
		union {	struct p2pcore_header dh; struct p2pcore_session_init bsa; char buff[512]; } u;

		if(PA_Recv(fd, u.buff, 512, 0) >= sizeof(u.dh) && check_p2pcore_header(&u.dh) && P2PCORE_OP(&u.dh) == OP_SD_SESSION_NOTIFY)
		{
			struct sockaddr_in sai;
			socklen_t sa_len = sizeof(sai);
			getpeername(fd, (struct sockaddr*)&sai, &sa_len);
			scan_wait_queue(&sai, &u.bsa);
		}
	}

	PA_SocketClose(fd);
	event_del(me);
}

void cb_chk_inactive_ipcam(evutil_socket_t fd, short what, void *arg)
{
	unsigned int now = time(NULL);

#if 0
	LOCK_IPCAM_MAP;
	IPCAMMAP::iterator ii = ipcam_map.begin();
	while(ii != ipcam_map.end())
	{
		if(now - ii->second->last_access >= 65)
		{
			_CALLCB(on_heart_no_beat)(ii->second->sn);
			delete ii->second;
			ipcam_map.erase(ii++);
		}
		else
			ii ++;
	}
	UNLOCK_IPCAM_MAP;
#else
	//
	// 大量对象中只有少量超时,对有序链表的操作更快些.
	// 时间主要消耗在ipcam_map的find操作上
	//
	struct list_head *p, *q;
	LOCK_IPCAM_MAP;
	list_for_each_safe(p, q, &__sIpcamList)
	{
		struct dcs_ipcam *pipcam = (struct dcs_ipcam*)list_entry(p, struct dcs_ipcam, list);
		if(now - pipcam->last_access >= 65)
		{
			_CALLCB(on_heart_no_beat)(pipcam->sn);
			list_del(&pipcam->list);
			ipcam_map.erase(ipcam_map.find(pipcam->sn));
			Log(__FUNCTION__, "%s timeouted", pipcam->sn);
			delete pipcam;
		}
		else
			break;
	}
	UNLOCK_IPCAM_MAP;
#endif
}

#if 0
static int handle_client_challenge(int sock, const struct p2pcore_challenge *pdc);
static int handle_client_challenge(int sock, const struct p2pcore_challenge *pdc)
{
	int len;
	uint8_t challenge[LENGTH_OF_CHALLENGE];
	union {
		struct p2pcore_header dh;
		struct p2pcore_challenge dc;
		struct p2pcore_login dl;
		struct p2pcore_session_init dbsr;
		uint8_t buf[2000];		//addres report
	};

	/*
	 * Send back challenge or error status
	 */
	char user[20];
	memset(user, 0, 20);
	strncpyz(user, (const char*)pdc->sn_user, 20);

	if(!user_existed(user))
	{
		dbg_msg("user %s not existed\n", user);
		init_p2pcore_header(&dh, ST_SERVER, OP_GET_CHALLENGE, CLS_RESPONSE, P2PS_INVALID_USER, 0, 0);
	}
	else
	{
		gen_challenge(challenge);
		memcpy(dc.challenge, challenge, LENGTH_OF_CHALLENGE);
		init_p2pcore_header(&dh, ST_SERVER, OP_GET_CHALLENGE, CLS_RESPONSE, 0, LENGTH_OF_CHALLENGE, 0);
		dbg_msg("handle_client_challenge: PA_Send challenge back.\n");
	}
	PA_Send(sock, &dc, P2PCORE_PACKET_LEN(&dc.dh), 0);

	if(dh.status) goto quit;

	/*
	 * Get login request and verify it
	 */
	len = timed_recv(sock, &dl, 200, 4000);
	if(len != sizeof(struct p2pcore_login) || (len < sizeof(struct p2pcore_header)) || !check_p2pcore_header(&dh))
	{
		dbg_msg("PA_Recv: len = %d\n", len);
		goto quit;
	}

	if(P2PCORE_OP(&dh) != OP_LOGIN && P2PCORE_OP(&dh) != OP_CS_SESSION_INIT)
	{
		init_p2pcore_header(&dh, ST_SERVER, P2PCORE_OP(&dh), CLS_RESPONSE, P2PS_UNEXPECTED, 0, 0);
	}
	else if(!verify_account(user, challenge, P2PCORE_OP(&dh)==OP_LOGIN?dl.auth:NULL))
	{
		init_p2pcore_header(&dh, ST_SERVER, P2PCORE_OP(&dh), CLS_RESPONSE, P2PS_AUTH_FAILED, 0, 0);
	}
	else if(P2PCORE_OP(&dh) == OP_LOGIN)
	{
		struct p2pcore_cam_record *piis = NULL;
		struct p2pcore_cam_group *pgrp = NULL;
		unsigned int n_ipcam, n_grp, i;
		struct p2pcore_dev_list* pdl = (struct p2pcore_dev_list*)buf; 

		load_ipcams(user, &pgrp, &n_grp, &piis, &n_ipcam);
		dbg_msg("load ipcams: %d grps, %d cams\n", n_grp, n_ipcam);

		//header
		init_p2pcore_header(&pdl->dh, ST_SERVER, OP_LOGIN, CLS_RESPONSE, 0, 
				sizeof(struct p2pcore_dev_list) + n_grp * sizeof(struct p2pcore_cam_group) + 
					n_ipcam * sizeof(struct p2pcore_cam_record) - sizeof(struct p2pcore_header), 
				0);
		pdl->n_grp = htonl(n_grp);
		pdl->n_cam = htonl(n_ipcam);

		//ipcam list
		LOCK_IPCAM_MAP;
		for(i=0; i<n_ipcam; i++)
		{
			struct p2pcore_cam_record *pddr = &piis[i];
			struct dcs_ipcam *pdi = get_ipcam(pddr->sn);
			if(!pdi) 
			{
				pddr->online = 0;
			}
			else
			{
				pddr->online = 1;
				strncpyz(pddr->name, pdi->name, sizeof(pddr->name));
				pddr->stun = pdi->stun;
				pddr->version = pdi->version;
				pddr->packed_expire_date = time2packeddate(pdi->expire);
			}
		}
		UNLOCK_IPCAM_MAP;

		memcpy(pdl+1, pgrp, sizeof(struct p2pcore_cam_group) * n_grp);
		memcpy( (struct p2pcore_cam_group*)(pdl+1) + n_grp, piis, sizeof(struct p2pcore_cam_record) * n_ipcam);

		if(n_grp) free(pgrp);
		if(piis) free(piis);
		//dbg_bin("login response:", buf, 40);
	}

	PA_Send(sock, buf, P2PCORE_PACKET_LEN(&dh), 0);

	return 0;
}
#endif

/*
static int handle_ipcam_charge(int sock, const struct p2pcore_charge *pdc) 
{
	char key[LENGTH_OF_KEY+1], sn[LENGTH_OF_KEY+1];
	strncpyz(sn, (const char*)pdc->sn, sizeof(sn));
	strncpyz(key, (const char*)pdc->key, sizeof(key));
	
	char valid_date[12];
	struct p2pcore_charge_response dcr;
	if( p2pcore_charge_ipcam(sn, key, pdc->name, valid_date) != FALSE )
	{
		init_p2pcore_header(&dcr.dh, ST_SERVER, OP_CHARGE, CLS_RESPONSE, 0, sizeof(dcr.valid_date), 0);
		memcpy(dcr.valid_date, valid_date, 12);

		IPCAMMAP::iterator ii;
		ii = ipcam_map.find(pdc->sn);
		if(ii != ipcam_map.end())
			ii->second->expire = datestr2time(valid_date);
	}
	else
		init_p2pcore_header(&dcr.dh, ST_SERVER, OP_CHARGE, CLS_RESPONSE, P2PS_INVALID_KEY, 0, 0);
	PA_Send(sock, &dcr, P2PCORE_PACKET_LEN(&dcr.dh), 0);

	CloseSocket(sock);
	return 0;
}
*/

#if 0
static int handle_ipcam_activate(int sock, const struct p2pcore_activate_p2p *pap) 
{
	char key[LENGTH_OF_KEY+1], sn[LENGTH_OF_KEY+1];
	strncpyz(sn, (const char*)pap->sn, sizeof(sn));
	strncpyz(key, (const char*)pap->key, sizeof(key));
	
	dbg_msg("Received registration request from %s\n", sn);
	struct p2pcore_activate_response dar;
	if( register_ipcam(sn, key, dar.valid_date) )
	{
		init_p2pcore_header(&dar.dh, ST_SERVER, OP_ACTIVATE_P2P, CLS_RESPONSE, 0, sizeof(dar.valid_date), 0);
	}
	else
		init_p2pcore_header(&dar.dh, ST_SERVER, OP_ACTIVATE_P2P, CLS_RESPONSE, P2PS_INVALID_KEY, 0, 0);

	PA_Send(sock, &dar, P2PCORE_PACKET_LEN(&dar.dh), 0);

	CloseSocket(sock);
	return 0;
}
#endif

int willBeRelayedBy3rdParty(const struct p2pcore_session_init *psr, struct dcs_ipcam *p_real_tgt)
{
	return 0;
	if(psr->bits.nat == StunTypeDependentMapping && psr->bits.delta == 0 && 
			p_real_tgt->stun.nat == StunTypeDependentMapping && p_real_tgt->stun.delta == 0)
		return 1;
	return 0;
}
struct dcs_ipcam *selectSupperNode(struct dcs_ipcam *t)
{
	return NULL;
}

/* sock: socket the request received from 
 */
SESSREQWAITNODE * prepare_wait_node(int sock, const struct p2pcore_session_init *psr, const struct sockaddr_in *psai)
{
	int status;
	struct dcs_ipcam *pipcam, *p_supper = NULL;
	SESSREQWAITNODE *pnode = NULL;

	Log(__FUNCTION__, "Session request. sn: %s", psr->sn);

	// Locate ipcam object
	LOCK_IPCAM_MAP;
	pipcam = get_ipcam(psr->sn);
	if(!pipcam)
	{
		Log(__FUNCTION__, "ipcam offline");
		status = P2PS_CALLEE_OFFLINE;
	}
	else if(time(NULL) > pipcam->expire)
	{
		status = P2PS_INACTIVE;
	}
	else
		status = _CALLCBRI(on_session_init)(pipcam->sn, psr->bits.ct);
	if(status)
	{
		UNLOCK_IPCAM_MAP;
	}

quit:
	if(status)
	{
		struct p2pcore_header dh;
		Log(__FUNCTION__, "Failed with status: %d", status);
		init_p2pcore_header(&dh, ST_SERVER, OP_CS_SESSION_INIT, CLS_RESPONSE, status, 0, 0);
		PA_SendTo(sock, &dh, sizeof(struct p2pcore_header), 0, (const struct sockaddr*)psai, sizeof(struct sockaddr));
		if(pnode) free(pnode);
		return NULL;
	}

	//PS:AP1
	if(willBeRelayedBy3rdParty(psr, pipcam))
	{
		p_supper = selectSupperNode(pipcam);
		if(p_supper) pipcam = p_supper;
	}

	pnode = (SESSREQWAITNODE*)malloc(sizeof(SESSREQWAITNODE));
	pnode->ipc_stun = pipcam->stun;
	UNLOCK_IPCAM_MAP;

	INIT_LIST_HEAD(&pnode->list);
	pnode->try_cnt = 0;
	pnode->bFastSess = 0;
	pnode->ext_addr = pipcam->ext_addr;
	pnode->udp_sock = pipcam->sock;
	memcpy(pnode->sn, psr->sn, LENGTH_OF_SN);

	pnode->sock_c = sock;
	pnode->addr_c.ip = psai->sin_addr.s_addr;
	pnode->addr_c.port = psai->sin_port;
	pnode->tid_c = P2PCORE_TID(&psr->dh);



	/* Generate Session ID, then: 
	 * 	1. prepare notification
	 * 	2. notify it to ipcam and get response
	 * 	3. return it to client; 
	 */
	union {
		struct p2pcore_session_init bsn;	//S->D
		uint8_t buf[256];
	};


	/*
	 * 1. Prepare notification to ipcam
	 */
	struct sockaddr_in sai;
	socklen_t sa_len;
	int n_addr;
	BOOL bRelay;

	pnode->i_relaysvr = -1;
	bRelay = psr->bits.ct == P2P_CONNTYPE_RELAY/*&&pipcam->allow_relay*/ || 
		/*when p2p seems not possible, change to relay */
		(pnode->ipc_stun.nat == StunTypeDependentMapping && psr->bits.nat == StunTypeDependentMapping);

	memset(&bsn, 0, sizeof(bsn));

	/* Prepare notification data */

	dbg_msg("psr->bits.sibling_sess: %d\n", psr->bits.sibling_sess);
	if(psr->bits.sibling_sess)
		memcpy(bsn.sess_id, psr->sess_id2, LENGTH_OF_SESSION_ID);
	else
	{
		gen_unique_id(bsn.sess_id);
		dbg_bin("New session: ", bsn.sess_id, LENGTH_OF_SESSION_ID);
	}
	if(p_supper)
		gen_unique_id(bsn.sess_id2);
	bsn.bitf = 0;//psr->bits;
	bsn.bits.auth = AUTH_ON_DEVICE;
	if(bRelay)
	{
		n_addr = 1;
		bsn.bits.n_addr = 1;
		bsn.bits.nat = StunTypeOpen;
		bsn.bits.delta = 0;
		bsn.bits.ct = P2P_CONNTYPE_RELAY;
		bsn.bits.tcp = 1;

		sa_len = sizeof(sai);
		PA_GetSockName(sock, (struct sockaddr*)&sai, &sa_len);
		if((pnode->i_relaysvr = GetRelayServer(-1, sai.sin_addr.s_addr, &bsn.addrs[0])) < 0 ||
				SendRelayNotification(pnode->i_relaysvr, bsn.sess_id) < 0)
		{
			status = P2PS_RELAY_SERVER_UNVAILABLE;
			goto quit;
		}

		memcpy(&pnode->si_ack, &bsn, sizeof(bsn) + sizeof(p2pcore_addr));
		//Tell caller to change to relay
		init_p2pcore_header(&pnode->si_ack.dh, ST_SERVER, OP_CS_SESSION_INIT, CLS_RESPONSE, P2PS_CHANGE_CONN_TYPE, 
				sizeof(bsn) - sizeof(bsn.dh) + sizeof(struct p2pcore_addr), 
				P2PCORE_TID(&psr->dh));
	}
	else
	{
		bsn.bits.nat = psr->bits.nat;
		bsn.bits.delta = psr->bits.delta;
		bsn.bits.ct = psr->bits.ct;
		bsn.bits.sibling_sess = (p_supper?1:0);
		n_addr = bsn.bits.n_addr = psr->bits.n_addr;
		memcpy(&bsn.addrs, psr->addrs, sizeof(struct p2pcore_addr)*n_addr);
		/*填写客户端的外部地址*/
		int i; for(i=0; i<n_addr; i++) if(bsn.addrs[i].ip == psai->sin_addr.s_addr) break;
		if(i >= n_addr)
			//if(psr->bits.nat != StunTypeOpen)
		{
			//append it to the end if not an open-nat
			bsn.addrs[n_addr].ip = psai->sin_addr.s_addr;
			bsn.addrs[n_addr].port = psai->sin_port;
			bsn.addrs[n_addr].flags = 0;
			bsn.bits.n_addr ++;
		}
		//如果要做端口预测(for symmetric nat), 修改外部端口
		if(bsn.bits.nat == StunTypeDependentMapping)
		{
			int iext = bsn.bits.n_addr - 1;
			if(bsn.bits.delta)
				bsn.addrs[iext].port = 	htons(ntohs(bsn.addrs[iext].port) + bsn.bits.delta);
			else if(bsn.bits.preserve_port)
				bsn.addrs[iext].port = bsn.addrs[0].port;
		}

		if(s_bAllowFastSess && pnode->ipc_stun.nat != StunTypeDependentMapping)
			pnode->bFastSess = TRUE;
		if(__UNLIKELY(pnode->bFastSess))
		{
			init_p2pcore_header(&pnode->si_ack.dh, ST_SERVER, OP_CS_SESSION_INIT, CLS_RESPONSE, 0, 
					sizeof(p2pcore_session_init) - sizeof(p2pcore_header) + sizeof(struct p2pcore_addr), 
					P2PCORE_TID(&psr->dh));

			memcpy(pnode->si_ack.psi.sess_id, bsn.sess_id, LENGTH_OF_SESSION_ID);
			pnode->si_ack.psi.addrs[0] = pipcam->ext_addr;
			pnode->si_ack.psi.bitf = 0;
			pnode->si_ack.psi.bits.nat = pnode->ipc_stun.nat;
			pnode->si_ack.psi.bits.preserve_port = pnode->ipc_stun.preserve_port;
			pnode->si_ack.psi.bits.hairpin = pnode->ipc_stun.hairpin;
			//PS:AP
			pnode->si_ack.psi.bits.ct = P2P_CONNTYPE_P2P;
			pnode->si_ack.psi.bits.tcp = 0;
			pnode->si_ack.psi.bits.n_addr = 1;
		}
		else
		{
			//Send a p2pcore_header with status = P2PS_CONTINUE,
			//to tell caller that we have received its request, and caller 
			//should continue to wait for further callee's info.
			init_p2pcore_header(&pnode->si_ack.dh, ST_SERVER, OP_CS_SESSION_INIT, CLS_RESPONSE, P2PS_CONTINUE, 0, P2PCORE_TID(&psr->dh));
		}
	}

	memcpy(pnode->sess_id, bsn.sess_id, LENGTH_OF_SESSION_ID);
	LOCK_WAIT_LIST;
	list_add(&pnode->list, &__s_wait_list);
	UNLOCK_WAIT_LIST;


#ifdef _DEBUG
	char line[256];
	int i, len;
	len = sprintf(line, "client info: nat=%d:%d, preserve port=%d", psr->bits.nat, psr->bits.delta, psr->bits.preserve_port);
	for(i=0; i<bsn.bits.n_addr; i++)
		len += sprintf(line+len, ", %s:%d:%d", inet_ntoa(*((struct in_addr*)&bsn.addrs[i].ip)), ntohs(bsn.addrs[i].port), bsn.addrs[i].flags);
	Log(__FUNCTION__, line);
#endif

	pnode->tid_noti = s_transId;
	PA_SpinLock(spin_transId);
	init_p2pcore_header(&bsn.dh, ST_SERVER, OP_SD_SESSION_NOTIFY, CLS_REQUEST, pnode->bFastSess?P2PS_CHANGE_MAIN_PORT:0, 
			sizeof(bsn) - sizeof(bsn.dh) + sizeof(struct p2pcore_addr)*bsn.bits.n_addr, 
			s_transId++);
	PA_SpinUnlock(spin_transId);


	int bsn_len = P2PCORE_PACKET_LEN(&bsn.dh);
	memcpy(&pnode->si_noti.bsn, &bsn, bsn_len);
	return pnode;
}

void send_sess_init_noti(SESSREQWAITNODE *pnode)
{
/* 
 *    Send the notification to ipcam via udp, the response may be udp(udp session)
 *    or tcp(tcp session), so that we have to associate a response with a request 
 *    by tid_noti/trans_id and handle the response in a callback function
 */
	struct sockaddr_in sai;
	int bsn_len = P2PCORE_PACKET_LEN(&pnode->si_noti.bsn.dh);

	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	sai.sin_addr.s_addr = pnode->ext_addr.ip;
	sai.sin_port = pnode->ext_addr.port;
	PA_SendTo(pnode->udp_sock, &pnode->si_noti.bsn, bsn_len, 0, (struct sockaddr*)&sai, sizeof(sai));
}

/// Send session_init response back to client
static void send_sess_begin_to_caller(SESSREQWAITNODE *pnode)
{
	struct sockaddr_in sai;
	int ack_len = P2PCORE_PACKET_LEN(&pnode->si_ack.dh);

	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	sai.sin_addr.s_addr = pnode->addr_c.ip;
	sai.sin_port = pnode->addr_c.port;
	PA_SendTo(pnode->sock_c, &pnode->si_ack, ack_len, 0, (struct sockaddr*)&sai, sizeof(sai));
}

static void scan_wait_queue(const struct sockaddr_in *sai, const struct p2pcore_session_init *pdbsa)
{
	struct list_head *p;

	LOCK_WAIT_LIST;

	list_for_each(p, &__s_wait_list)
	{
		SESSREQWAITNODE *pnode = list_entry(p, SESSREQWAITNODE, list);
		if( pnode->tid_noti == P2PCORE_TID(&pdbsa->dh) 
				// && memcmp(pnode->sess_id, dbsa.sess_id, LENGTH_OF_SESSION_ID) == 0 
			)
		{
			if(pnode->bFastSess) 
			{
				LOCK_IPCAM_MAP;
				struct dcs_ipcam *pipcam = get_ipcam(pnode->sn);
				if(pipcam) {
					pipcam->ext_addr.ip = sai->sin_addr.s_addr;
					pipcam->ext_addr.port = sai->sin_port;
				}
				UNLOCK_IPCAM_MAP;
				break;
			}

			memcpy(&pnode->si_ack, pdbsa, P2PCORE_PACKET_LEN(&pdbsa->dh));
			pnode->si_ack.dh.cls = CLS_REQUEST;
			pnode->si_ack.dh.status = 0;
			P2PCORE_SET_OP(&(pnode->si_ack.dh), OP_SC_SESSION_BEGIN);

			/* */
			int i, n_addr = pnode->si_ack.psi.bits.n_addr;
			for(i=0; i<n_addr; i++) if(pnode->si_ack.psi.addrs[i].ip == sai->sin_addr.s_addr) break;
			if(i >= n_addr)
			{
				pnode->si_ack.psi.addrs[n_addr].ip = sai->sin_addr.s_addr;
				pnode->si_ack.psi.addrs[n_addr].port = sai->sin_port;
				pnode->si_ack.psi.bits.n_addr ++;
			}
			P2PCORE_SET_DATA_LEN(&pnode->si_ack.dh, 
					sizeof(struct p2pcore_session_init) + pnode->si_ack.psi.bits.n_addr*sizeof(struct p2pcore_addr) 
					- sizeof(struct p2pcore_header));

			send_sess_begin_to_caller(pnode);
#if 1
			list_del(&pnode->list);
			event_del(pnode->_event);

			/* Move wait list 2 */
			struct timeval tv = { 1, 0 };
			pnode->try_cnt = 0;
			list_add_tail(&pnode->list, &__sWaitList2);
			event_assign(pnode->_event, base0, NULL, EV_TIMEOUT|EV_PERSIST, cb_sess_begin_timeouted, (void*)pnode);
			event_add(pnode->_event, &tv);
#endif
			break;
		}
	}

	UNLOCK_WAIT_LIST;
}

void scan_wait_queue2(const struct sockaddr_in *sai, const struct p2pcore_session_init *pdbsa)
{
	struct list_head *p;

	LOCK_WAIT_LIST2;

	list_for_each(p, &__sWaitList2)
	{
		SESSREQWAITNODE *pnode = list_entry(p, SESSREQWAITNODE, list);
		if( pnode->tid_noti == P2PCORE_TID(&pdbsa->dh) 
				// && memcmp(pnode->sess_id, dbsa.sess_id, LENGTH_OF_SESSION_ID) == 0 
			)
		{
			Log(__FUNCTION__, "session invitation to %s finished", pdbsa->sn);
			list_del(&pnode->list);
			event_del(pnode->_event);
			event_free(pnode->_event);
			free(pnode);

			break;
		}
	}

	UNLOCK_WAIT_LIST2;
}

//session_init request from client
void handle_cs_sess_init(int sock, struct p2pcore_session_init *pcsi, const struct sockaddr_in *sai)
{
	struct list_head *p;
	SESSREQWAITNODE *pnode;

	/* check to see if the request exists */
	LOCK_WAIT_LIST;
	list_for_each(p, &__s_wait_list)
	{
		pnode = list_entry(p, SESSREQWAITNODE, list);
		dbg_msg("wait node, ip:%s, port:%d, tid:%X\n", inet_ntoa(*((struct in_addr*)&pnode->addr_c)), ntohs(pnode->addr_c.port), pnode->tid_c);
		if(pnode->addr_c.ip == sai->sin_addr.s_addr && pnode->addr_c.port == sai->sin_port && pnode->tid_c == P2PCORE_TID(&pcsi->dh))
		{
			UNLOCK_WAIT_LIST;
			goto send_ack;
		}
	}
	UNLOCK_WAIT_LIST;

	/* request nearly finished */
	LOCK_WAIT_LIST2;
	list_for_each(p, &__sWaitList2)
	{
		pnode = list_entry(p, SESSREQWAITNODE, list);
		dbg_msg("wait node in list2, ip:%s, port:%d, tid:%X\n", inet_ntoa(*((struct in_addr*)&pnode->addr_c)), ntohs(pnode->addr_c.port), pnode->tid_c);
		if(pnode->addr_c.ip == sai->sin_addr.s_addr && pnode->addr_c.port == sai->sin_port && pnode->tid_c == P2PCORE_TID(&pcsi->dh))
		{
			UNLOCK_WAIT_LIST2;
			goto send_ack;
		}
	}
	UNLOCK_WAIT_LIST2;


	Log(__FUNCTION__, "SESSION_INIT request from %s:%d,tid:%X", inet_ntoa(sai->sin_addr), ntohs(sai->sin_port), P2PCORE_TID(&pcsi->dh));
	if( (pnode = prepare_wait_node(sock, pcsi, sai)) )
	{
		struct timeval tv = { 0, 300*1000 };
		struct event *evt = event_new(base0, -1, EV_PERSIST|EV_TIMEOUT, cb_sess_noti_timeouted, (void*)pnode);
		send_sess_init_noti(pnode);
		pnode->_event = evt;
		event_add(evt, &tv);
	}
	else
		return;

send_ack:
	PA_SendTo(sock, &pnode->si_ack, P2PCORE_PACKET_LEN(&pnode->si_ack.dh), 0, 
			(struct sockaddr*)sai, sizeof(struct sockaddr));
}

void handle_heart_beat(int sk, const struct p2pcore_i_am_here *pdi, struct sockaddr_in *psai, time_t now)
{
	union {
		struct p2pcore_header dh;
		struct p2pcore_i_am_here_ack diaa;
		char buf[1000];
	};

	struct dcs_ipcam *pipcam = get_ipcam(pdi->sn);
	if(!pipcam)	//Not in memory
	{
		Log("heart beat", "Load %s",pdi->sn);
		if(!verify_ipcam(pdi->sn, NULL))//dar.key)) 
		{
			dbg_msg("verify_ipcam failed.\n");
			return;
		}
		LOCK_IPCAM_MAP;
		pipcam = load_ipcam(pdi->sn);
		UNLOCK_IPCAM_MAP;
	}
	if(!pipcam || pipcam->expire < time(NULL))
	{
		init_p2pcore_header(&dh, ST_SERVER, OP_DS_IAMHERE, CLS_RESPONSE, P2PS_INACTIVE, 0, 0);
	}
	else
	{
		Log("heart beat", "%-16s: %s:%d. stun=%d:%d:%c:%c", (char*)pdi->sn, inet_ntoa(psai->sin_addr), ntohs(psai->sin_port),
			pdi->stun.nat, pdi->stun.delta, pdi->stun.preserve_port?'p':'_', pdi->stun.upnp?'u':'_');
		pipcam->ext_addr.ip = psai->sin_addr.s_addr;
		pipcam->ext_addr.port = psai->sin_port;
		pipcam->stun = pdi->stun;
		/*
		   pipcam->stunt = pdi->stunt;
		   pipcam->sdc = pdi->sdc;
		   pipcam->need_ac = 0;
		   */
		if((pdi->version & 0xFF) != 0x10) pipcam->version = 0;
		else pipcam->version = pdi->version;

		pipcam->last_access = now;
		pipcam->sock = sk;	//PA_Send on this socket later

		int err;
		char new_server[48];
		struct p2pcore_addr ext_addr = { psai->sin_addr.s_addr, psai->sin_port, 0 };
		if(_p2psvc_cb && _p2psvc_cb->on_heart_beat) 
			err = _p2psvc_cb->on_heart_beat(pdi->sn, new_server);
		else
			err = 0;
		switch(err)
		{
			default:
				init_p2pcore_header(&dh, ST_SERVER, OP_DS_IAMHERE, CLS_RESPONSE, 
						P2PS_OK, sizeof(struct p2pcore_addr), 0);
				diaa.ext_addr = ext_addr;
				break;
			case P2PS_ADDRESS_CHANGED:
				init_p2pcore_header(&dh, ST_SERVER, OP_DS_IAMHERE, CLS_RESPONSE, 
						P2PS_ADDRESS_CHANGED, sizeof(struct p2pcore_addr) + strlen(new_server)+1, 0);
				diaa.ext_addr = ext_addr;
				strcpy(diaa.data, new_server);
				break;
			case P2PS_INACTIVE:
				init_p2pcore_header(&dh, ST_SERVER, OP_DS_IAMHERE, CLS_RESPONSE, P2PS_INACTIVE, 0, 0);
				break;
		}

		//Move the newest callee to the end of list
		list_del(&pipcam->list);
		list_add_tail(&pipcam->list, &__sIpcamList);
	}
	PA_SendTo(sk, &dh, P2PCORE_PACKET_LEN(&dh), 0, (struct sockaddr*)psai, sizeof(struct sockaddr));
}

void handle_query_address(int sock, struct sockaddr_in *sai)
{
	union {
		struct p2pcore_header dh;
		struct p2pcore_query_address_response dq;
	};

	init_p2pcore_header(&dh, ST_SERVER, OP_QUERY_ADDRESS, CLS_RESPONSE, 0, 
			sizeof(dq)-sizeof(dh), P2PCORE_TID(&dh));
	dq.ext_addr.ip = sai->sin_addr.s_addr;
	dq.ext_addr.port = sai->sin_port;
	dq.ext_addr.flags = 0;

	struct sockaddr_in tmp;
	int sa_len;
	sa_len = sizeof(tmp);
	PA_GetSockName(sock, (struct sockaddr*)&tmp, &sa_len);

	dq.server2.ip = 0;
	dq.server2.port = 0;
	dq.server2.flags = 0;
	PA_SendTo(sock, &dq, sizeof(dq), 0, (struct sockaddr*)&sai, sa_len);

	dbg_msg("Reiceved address query from: %s:%d at ", inet_ntoa(sai->sin_addr), (int)ntohs(sai->sin_port));
	dbg_msg("%s:%d\n", inet_ntoa(tmp.sin_addr), ntohs(tmp.sin_port));
}


static PA_THREAD_RETTYPE __STDCALL main_service_thread(void *p)
{
	struct event *ev;
	struct event evt_sock_noti, evt_sock_udp[n_sock];
	int i;

	base0 = event_base_new();
	if(!base0) { exit(-1); }
	//event_init();	//init current_base

	if(relayers.count)
	{
		int i;
		relayers.conn = (struct conn_to_relayer*)malloc(sizeof(struct conn_to_relayer)*relayers.count);
		for(i=0; i<relayers.count; i++)
		{
			connect_to_relayer(i);
		}
	}


	if(tcp_sock_noti != INVALID_SOCKET)
	{
		evutil_make_socket_nonblocking(tcp_sock_noti);
		event_assign(&evt_sock_noti, base0, tcp_sock_noti, EV_READ|EV_PERSIST, cb_accept2, &evt_sock_noti);
		event_add(&evt_sock_noti, NULL);
	}

	for(i=0; i<n_sock; i++)
	{
		evutil_make_socket_nonblocking(udp_socks[i]);
		event_assign(&evt_sock_udp[i], base0, udp_socks[i], EV_READ|EV_PERSIST, cb_udp, NULL);
		event_add(&evt_sock_udp[i], NULL);
	}

	struct timeval tv = { 60, 0 };
	ev = event_new(base0, -1, EV_TIMEOUT|EV_PERSIST, cb_chk_inactive_ipcam, NULL);
	event_add(ev, &tv);

	event_base_dispatch(base0);
	//event_dispatch();
	event_free(ev);
	event_base_free(base0);

	LOCK_IPCAM_MAP;
	IPCAMMAP::iterator ii = ipcam_map.begin();
	while(ii != ipcam_map.end())
	{
		delete ii->second;
		ipcam_map.erase(ii++);
	}
	UNLOCK_IPCAM_MAP;

	printf("main thread terminated.\n");

	return (PA_THREAD_RETTYPE)0;
}


///////////////////////////////////////////////////////////////////////////////////////////
//
// 1.激活码校验服务 Activation-code verification service
// 2.IPCAM状态查询
// 3.IPCAM信息更新
//
int printStatus(struct dcs_ipcam *pipcam, char *buff)
{
	char sip[16];
	//sn, expair,sdc|nas|online,extern_ip,external_port,nat
	return sprintf(buff, "%s,%s,%d,%s,%d", (char*)pipcam->sn, 
			date2str(pipcam->expire, buff+80), 
			1,//((pipcam->sdc|pipcam->nas)?2:0) | 1, 	//bit 0: online, 1: sdc or nas
			IP2STR(pipcam->ext_addr.ip, sip),
			pipcam->stun.nat);
}
extern BOOL verify_activate_code(const char *sn, const char *activate_code);
static PA_THREAD_RETTYPE __STDCALL _OtherService(void *p)
{
	int sock = CreateServiceSocket(SOCK_DGRAM, 0, OTHER_SERVICES_PORT);
	struct sockaddr addr;
	unsigned int sa_len;
	char buff[100];
	int len;

	while(s_bRun)
	{
		sa_len = sizeof(addr);
		if( (len = timed_recv_from(sock, buff, sizeof(buff), &addr, &sa_len, 1000)) > 0)
		{
			if(buff[0] == '?') //"?" + "SERIALNUMBER", Query status
			{
				struct dcs_ipcam *pipcam;
				char sn[LENGTH_OF_SN];
				int all;

				buff[len] = '\0';
				all = strcmp(buff+1, "all") == 0;

				LOCK_IPCAM_MAP;
				if(all)
				{
					IPCAMMAP::iterator ii = ipcam_map.begin();
					while(ii != ipcam_map.end())
					{
						len = printStatus(ii->second, buff);
						PA_SendTo(sock, buff, len, 0, &addr, sizeof(addr));
						ii++;
					}
					PA_SendTo(sock, "\r\n", 2, 0, &addr, sizeof(addr));
					dbg_msg("list all.\n");
				}
				else
				{
					char *ns = buff+1;
					pipcam = get_ipcam(sn); //? + SN
					if(pipcam) len = printStatus(pipcam, buff);
					else
					{
						struct dcs_ipcam ipcam;
						if(load_ipcam_info(buff+1, &ipcam))
						{
							//sn,expire,0
							len = sprintf(buff, "%s,%s,0", sn, date2str(ipcam.expire, buff+80));
						}
						else
						{
							//sn,0
							len = sprintf(buff, "%s,0", sn);
						}
					}
					PA_SendTo(sock, buff, len, 0, &addr, sizeof(addr));
				}
				UNLOCK_IPCAM_MAP;

			}
			else if(buff[0] == '&') //内存信息更新
			{
				IPCAMMAP::iterator ii;
				char *sn = buff+1;
				buff[len] = '\0';
				LOCK_IPCAM_MAP;
				if((ii = ipcam_map.find(sn)) != ipcam_map.end())
				{
					load_ipcam_info(sn, ii->second);
				}
				UNLOCK_IPCAM_MAP;
			}
			else if(buff[0] == 'a') //alive query
			{
				PA_SendTo(sock, buff, len, 0, &addr, sa_len);
			}
			else if(buff[0] == 's')	// Status of server
			{
				//num of ipcam online [, totla]
				len = sprintf(buff, "%d", ipcam_map.size());
				PA_SendTo(sock, buff, len, 0, &addr, sizeof(addr));
			}
			else	// Activate & Charge
			{
				// Format:   SERIALNUMBER:ACTIVATIONCODE

				char *colon;
				buff[len] = '\0';
				if( (colon = strchr(buff, ':')) )
				{
					char *s, *t;
					*colon++ = '\0';
					t = s = colon;
					while(*s)
					{
						if(!isalnum(*s)) s++;
						else *t++ = *s++;
					}
					*t = '\0';

					if(verify_activate_code(buff, colon))
						PA_SendTo(sock, "1", 1, 0, &addr, sa_len);
					else
						PA_SendTo(sock, "0", 1, 0, &addr, sa_len);
				}
			}
		}
	}
	dbg_msg("_OtherService exit\n");
	CloseSocket(sock);

	return (PA_THREAD_RETTYPE)0;
}

//===========================================================================================
//===========================================================================================
//===========================================================================================
//===========================================================================================
//===========================================================================================
#define Index2Conn(iLocal, iRelayer) &relayer.conns[]
//! \brief 根据接收到请求的地址(local_ip)，选择一个匹配的 relayer 的地址
//! \param i_relaysvr - index of relay-server. when less than 0, the function will choose one
//! \param local_ip  - local ip which the relay-server's interface will match
//! \param paddr    - buffer to receive server's address matched to local_ip
/// \return >=0 if a relayer's address is chosen
int GetRelayServer(int i_relaysvr, unsigned long local_ip, struct p2pcore_addr *paddr)
{
	int i;

	if(relayers.count == 0) return -1;

	if(i_relaysvr < 0) i_relaysvr = rand()%relayers.count;
	else if(relayers.count == 1) i_relaysvr = 0;
	if(!relayers.conn[i_relaysvr].ok) {
		for(i=(i_relaysvr+1)%relayers.count; i!=i_relaysvr; i = (i+1)%relayers.count)
			if(relayers.conn[i].ok) break;
		if(i == i_relaysvr) return -1;
	}

	paddr->ip = 0;
	paddr->port = htons(RELAYSERVER_MEDIA_PORT);
	int idx = relayers.pIdxcnt[i_relaysvr].idx;
	for(i=0; i<relayers.pIdxcnt[i_relaysvr].cnt; i++, idx++)
	{
		if(local_ip == relayers.pIptbl[idx].local_ip || relayers.pIptbl[idx].local_ip == 0)
		{
			paddr->ip = relayers.pIptbl[idx].ext_ip;
			Log("GetRelayServer", "select %dth relay-server: %s", i_relaysvr, inet_ntoa(*((struct in_addr*)&paddr->ip)));
			return i_relaysvr;
		}
	}
	return -1;
}

//Send notification to relay-server,
//when relay-server gets response from ipcam, it notify dcssvr
int SendRelayNotification(int i_relayer, const uint8_t* sess_id)
{
	struct p2pcore_relay_notification drn;
	init_p2pcore_header(&drn.dh, ST_SERVER, OP_RELAY_NOTIFY, CLS_REQUEST, 0, 
			sizeof(drn) - sizeof(drn.dh), 0);
	memcpy(drn.sess_id, sess_id, LENGTH_OF_SESSION_ID);

	struct conn_to_relayer *conn =&relayers.conn[i_relayer];
	if(PA_Send(conn->sock, &drn, sizeof(drn), MSG_NOSIGNAL) < 0 && PA_SocketGetError() != EAGAIN)
	{
		perror("SendRelayNotifiction");
		PA_SocketClose(conn->sock);
		conn->ok = 0;
		//event_del(&conn->evt);
		connect_to_relayer(i_relayer);

		return -1;
	}
	else
	{
		struct timeval tv = { 5, 0 };
		//event_set(&conn->evt, conn->sock, EV_READ|EV_TIMEOUT, cb_relayconn_read, (void*)i_relayer);
		event_assign(&conn->evt, base0, conn->sock, EV_READ|EV_TIMEOUT, cb_relayconn_read, (void*)i_relayer);
		event_add(&relayers.conn[i_relayer].evt, &tv);
	}

	return 0;
}
//===========================================================
//===========================================================
#ifndef WIN32
#include "ExtractSetting.h"
#endif
//Return: number of servers
/*
  There can be multiple relay-servers. 
  When a relay request is served, p2p server randomly 
  chooses one relay-server, send the service request to it,
  and send the relay-server's address as peer address to
  both camera and client.

  Each relay-server can be descripted like following:

  ;Relay-server 只有一个IP:
  ;map any local ip to a same server ip
  [relay1]
  * = ip.of.relayer1
 
  ;Relayer-server 有多个IP,但p2p服务器只有一个; 
  ;或者p2p有多个IP但无需特别指定对应关系(任意IP间都能顺利连接)：
  ;随便选择一个p2p服务器可达的relayer的地址
  [relay2]
  * = any.addr.of.relayer2

  ;Relay-server 和 p2p 服务器都有多个地址, 并且需要特别指定
  ;对应关系(例如电信网通双线机房,特定IP对间才能顺利连接):
  ;Request arrived at local ip A will be routed to 
  ;relayer's ip B
  [relay3]
  local.ip.addr.1 = relayer3.ip.addr.1
  local.ip.addr.2 = relayer3.ip.addr.2
  ...

  本函数根据配置文件，在*ppIptbl中建立转发服务IP与p2p服务器本地IP的对照表.
  例如，如果p2p服务器有两个IP(参数nLocalIp=2)：
  	local_ip1
	local_ip2
  配置文件中有两个转发服务器，内容如前面的[relay2]和[relay3]，则*ppIptbl返回：

  	|----------------|
	|                |
	|  |--[relay2{0,1}] [relay3{1,2}]        <-- *ppIdxcnt
	|  |
	|  |
	|  |
	|  |
	|  |             *ppIptbl
	|  |   ____________/\___________
	|  |  /                         \
    |  |->[0,         ip_of_relayer2]	 ==> 0 表示不特定本地IP
	|
  	|---->[local_ip1, ip1_of_relayer3] 
	      [local_ip2, ip2_of_relayer3]
 */
static int BuildRelayServerTable(const char *conf_file, const uint32_t *loc_ips, int nLocalIp, 
		/*OUT*/struct IpPair **ppIptbl, /*OUT*/struct idx_cnt **ppIdxcnt)
{
	char cont[1024];
	int i, j;
	int nServer = 0, sizeServer = 5, sizePair = 0, nPair = 0;
	struct IpPair *pIptbl = NULL;
	struct idx_cnt *pIdxcnt = NULL;


	pIdxcnt = (struct idx_cnt*)realloc(pIdxcnt, sizeof(struct idx_cnt) * sizeServer);
	pIdxcnt[0].idx = pIdxcnt[0].cnt = 0;
	for(i=1; ;i++ )
	{
		char *s, sec[20];
		sprintf(sec, "relay%d", i);
#ifdef WIN32
		if(GetPrivateProfileSection(sec, cont, 1024, conf_file) < 10)
#else
		if(ReadSection(conf_file, sec, cont, 1024) < 10)
#endif
			break;

		s = cont;
		dbg_msg("Section [%s]\n", sec);
		while(*s)
		{
			dbg_msg("%s\n", s);
			unsigned long addr1, addr2;
			if(*s == '*')
			{
				uint32_t addr = inet_addr(s+2);
				dbg_msg("Relayer: * <--> %s\n", s+2);
				if(nPair <= sizePair) {
					sizePair += 10;
					pIptbl = (struct IpPair*)realloc(pIptbl, sizeof(struct IpPair) * sizePair);
				}
				pIptbl[nPair].local_ip = 0;
				pIptbl[nPair++].ext_ip = addr;
			}
			else
			{
				char *eq = strchr(s, '=');
				*eq = '\0';
				addr1 = inet_addr(s);
				addr2 = inet_addr(eq+1);
				*eq = '=';
				dbg_msg("Relayer: %s <--> ", inet_ntoa(*(struct in_addr*)&addr1));
				dbg_msg("%s\n", inet_ntoa(*(struct in_addr*)&addr2));
				for(j=0; j<nLocalIp; j++)
				{
					if(loc_ips[j] == addr1)
					{
						if(nPair <= sizePair) {
							sizePair += 10;
							pIptbl = (struct IpPair*)realloc(pIptbl, sizeof(struct IpPair) * sizePair);
						}
						pIptbl[nPair].local_ip = addr1;
						pIptbl[nPair++].ext_ip = addr2;
						break;
					}
				}
			}
			s += strlen(s) + 1;
		}

		if(nServer+1 >= sizeServer)
		{
			sizeServer += 5;
			pIdxcnt = (struct idx_cnt*)realloc(pIdxcnt, sizeof(struct idx_cnt) * sizeServer);
		}
		pIdxcnt[nServer].cnt = nPair - pIdxcnt[nServer].idx;
		nServer ++;
		pIdxcnt[nServer].idx = nPair;
	}

	dbg_msg("Number of relay servers: %d\n", nServer);
	if(nServer == 0) { 
		free(pIptbl); *ppIptbl = NULL;
		free(pIdxcnt); *ppIdxcnt = NULL;
	}
	else {
		*ppIptbl = pIptbl;
		*ppIdxcnt = pIdxcnt;
	}
	return nServer;
}

void connect_to_relayer(int index)
{
	struct conn_to_relayer *conn = &relayers.conn[index];
	struct sockaddr_in sai;
	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	sai.sin_port = htons(RELAYSERVER_NOTIFY_PORT);

	struct IpPair *pair = &relayers.pIptbl[relayers.pIdxcnt[index].idx];
	struct timeval tv = { 300, 0 }; //try every 5'
	conn->ok = 0;
	conn->sock = NewSocketAndBind(SOCK_STREAM, pair->local_ip, 0);
	setblk(conn->sock, 0);
	event_assign(&conn->evt, base0, conn->sock, EV_TIMEOUT|EV_WRITE, cb_relayconn, (void*)index);
	event_add(&conn->evt, &tv);
	sai.sin_addr.s_addr = pair->ext_ip;
	Log(__FUNCTION__, "connect to relayer %s:%d", inet_ntoa(sai.sin_addr), ntohs(sai.sin_port));
	if(connect(conn->sock, (struct sockaddr*)&sai, sizeof(sai)) < 0 && PA_SocketGetError() != EINPROGRESS)
		perror("connect");
}

void init_relayers(const char *conf_file)
{
	relayers.count = BuildRelayServerTable(conf_file, local_ips, n_sock, &relayers.pIptbl, &relayers.pIdxcnt);
}

void free_relayers()
{
	if(relayers.count)
	{
		int i;
		for(i=0; i<relayers.count; i++)
		{
			if(relayers.conn[i].sock != INVALID_SOCKET)
				PA_SocketClose(relayers.conn[i].sock);
		}
		free(relayers.pIdxcnt);
		free(relayers.conn);
		free(relayers.pIptbl);
		memset(&relayers, 0, sizeof(relayers));
	}
}

int init_p2p_svc()
{
	int i;
	char dbname[20], dbuser[20], dbpswd[20];
	char conf_file[256];
	int keyidx;

#ifdef WIN32
	GetModuleFileName(NULL, conf_file, sizeof(conf_file));
	strcpy(strrchr(conf_file, '\\')+1, "p2psvr.ini");

	GetPrivateProfileString("db", "dbname", "p2pcamdb", dbname, sizeof(dbname), conf_file);
	GetPrivateProfileString("db", "dbuser", "admin", dbuser, sizeof(dbuser), conf_file);
	GetPrivateProfileString("db", "dbpswd", "admin", dbpswd, sizeof(dbpswd), conf_file);
	keyidx = GetPrivateProfileInt("db", "keyindex", 0, conf_file);
	s_bAllowFastSess = GetPrivateProfileInt("options", "fastsess", 0, conf_file);
#else
	strcpy(conf_file, "/etc/tasp2p/p2psvr.conf");

	KEYVAL kv_db[] = {
		{ "dbname", KEYVALTYPE_STRING, dbname, 20 },
		{ "dbuser", KEYVALTYPE_STRING, dbuser, 20 },
		{ "dbpswd", KEYVALTYPE_STRING, dbpswd, 20 },
		{ "keyidx", KEYVALTYPE_INT, &keyidx },
		{ "fastsess", KEYVALTYPE_INT, &s_bAllowFastSess },
		{ NULL }
	};
	KEYVAL kv_opt[] = {
		{ "fastsess", KEYVALTYPE_INT, &s_bAllowFastSess },
		{ NULL }
	};
	if(ExtractSetting(conf_file, "db", kv_db) < 3) 
	{
		fprintf(stderr, "Read configure file /etc/tasp2p/p2psvr.conf failed.\n");
		exit(-1);
	}
	ExtractSetting(conf_file, "options", kv_db); 
#endif

	if(init_db(dbname, dbuser, dbpswd) != 0) return -1;
	//LoadActivationKey(keyidx);

	g_pSlowTq = TimerQueueCreate();

	n_sock = get_local_ips(local_ips, MAX_LOCAL_INTERFACE);

	relayers.count = BuildRelayServerTable(conf_file, local_ips, n_sock, &relayers.pIptbl, &relayers.pIdxcnt);

	PA_SpinInit(spin_transId);
	s_transId = time(NULL);

#if 0
	/* 如果准备支持TCP穿透，创建一个套接字接收来自被叫的应答连接 */
	/* We don't support tcp nat-passthrough anymore */

	tcp_sock_noti = CreateServiceSocket(SOCK_STREAM, 0, DCS_SERVICE_PORT+1);
	if(tcp_sock_noti == INVALID_SOCKET) return -1;
#endif

	for(i=0; i<n_sock; i++)
	{
		dbg_msg("Local ip: %s\n", inet_ntoa(*((struct in_addr*)&local_ips[i])));
		int sk = CreateServiceSocket(SOCK_DGRAM, local_ips[i], P2PCORE_SERVICE_PORT);
		if(sk == INVALID_SOCKET) return -1;

		udp_socks[i] = sk;
	}

	PA_MutexInit(__s_wait_list_mutex);
	PA_MutexInit(__sWaitListMutex2);
	PA_MutexInit(__s_ipcam_map_mutex);

	//ThreadPoolInit(MAX_CONCURRENT_REQUESTS);


	return 0;
}

static PA_HTHREAD s_thdOther, s_thdMain;
void run_p2p_svc(BOOL new_thread)
{
	s_bRun = TRUE;
	s_thdOther = PA_ThreadCreate(_OtherService, NULL);
	if(new_thread)
	{
		s_thdMain = PA_ThreadCreate(main_service_thread, NULL);
	}
	else
	{
		main_service_thread(NULL);
	}
}

void signal_stop_service()
{
	s_bRun = FALSE;
	event_base_loopbreak(base0);
}

void clean_p2p_svc()
{
	int i;

	PA_ThreadWaitUntilTerminate(s_thdOther);
	if(s_thdMain)
		PA_ThreadWaitUntilTerminate(s_thdMain);

	PA_ThreadCloseHandle(s_thdOther);
	if(s_thdMain) PA_ThreadCloseHandle(s_thdMain);

	if(tcp_sock_noti != INVALID_SOCKET) PA_SocketClose(tcp_sock_noti);
	for(i=0; i<n_sock; i++)
		PA_SocketClose(udp_socks[i]);
	tcp_sock_noti = INVALID_SOCKET;

	PA_SpinUninit(spin_transId);

	PA_MutexUninit(__s_wait_list_mutex);
	PA_MutexUninit(__sWaitListMutex2);
	PA_MutexUninit(__s_ipcam_map_mutex);

	dbg_msg("1\n");
	TimerQueueDestroy(g_pSlowTq);
	g_pSlowTq = NULL;
	dbg_msg("2\n");

	//ThreadPoolUninit();
	uninit_db();
	free_relayers();
}

void p2psvc_set_cb(struct p2psvc_cb *cb)
{
	_p2psvc_cb = cb;
}


