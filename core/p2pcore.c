#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#ifdef __LINUX__
#include <resolv.h>
#include <syslog.h>
#include <stdarg.h>
#endif
#include "rudp.h"
#include "netbase.h"
#include "p2pcore.h"
#include "p2pcore_ex.h"
#include "p2pcore_imp.h"
#include "timerq.h"
#include "upnp_igd_cp.h"
#include "stunc/det_nat.h"
#include "p2plog.h"

#ifdef __LINUX__
#include <resolv.h>
#include <linux/if.h>
#endif

void _pause(const char *prompt)
{
	char line[64];
	printf(prompt); fflush(stdin);
	fgets(line, 64, stdin);
}

#ifdef _DEBUG

#ifdef WIN32
#define dbg_p2p(fmt, ...)  Log(__FUNCTION__, fmt, __VA_ARGS__)
#else
#define dbg_p2p(fmt, args...) Log(__FUNCTION__, fmt, ##args)
#endif

#else

#ifdef WIN32
	#define dbg_p2p(fmt, __VA_ARGS__)
#else
	#define dbg_p2p(fmt, args...)
#endif

#endif

#define SAFE_FREE(x) if(x) { free(x); x=NULL; }

/* Timeout constant */
#define SESSION_TIMEOUT	3600000 //15000
#define CONNECT_TO_RELAYER_TIMEOUT	6000
#define WAIT_FOR_PEER_INFO_TIMEOUT	6000
/* Time value for heart-beat to p2p-server */
#define NORMAL_INTERVAL		25000	//for heart-beat
#define INACTIVE_INTERVAL	3600000	//for heart-beat
#define MAX_TIME_WAIT_FOR_ACK	4000	//for heart-beat
#define SESSION_REQUEST_TIMEOUT	6000

#define MAX_ACCEPTED_RUDPSOCK	10


//PnP Conn State
enum { 
	PSS_ALLOCATED,			//just allocated

	PSS_WAIT_FOR_SERVER_ACK,	//for caller. OP_CS_SESSION_INIT is sent, wait for ack from server
	PSS_WAIT_FOR_PEER_INFO,		//for caller. wait for peer's info.

	PSS_PUNCHING, 
	PSS_RUDP_CONNECTING,
	PSS_FAILED, 
	PSS_CONNECTED,

	PSS_CLOSED
};

static LIST_HEAD(s_P2pConnList);
static PA_MUTEX s_mutexConnList;
static PA_MUTEX s_PunchConcurrencyMutex;

static void PunchNewAndKillTimeoutedConn(unsigned int now);
static int _calleeConnInit(const struct sockaddr_in* pSvrAddr, const struct p2pcore_session_init* psi, unsigned int now);
static int handle_punch(P2PCONN* pconn, const struct p2pcore_punch* pdp, const struct sockaddr_in* ppeer_addr, RUDPSOCKET accepted_sock);
static int create_session_init_ack(uint8_t *buf, struct conn_bit_fields bits, const struct p2pcore_addr* paddrs, 
		const char* sn, const uint8_t* sess_id, uint32_t trans_id);
static PA_THREAD_RETTYPE __STDCALL tcp_punch_thread(void* data);
static void udp_punch_it(P2PCONN* pconn);
static int _P2pConnInitInternal(const struct sockaddr_in *sai_svr, const char *pcsSN, const uint8_t *sident/*LENGTH_OF_SIDENT*/, 
		const char *auth_str, int auth_len, void *pIdent, int ct, const uint8_t *sess_id);


#define _CALLCB(f) if(s_pCallbackFuncs && s_pCallbackFuncs->f) s_pCallbackFuncs->f

void sendCurrentPBClock(void *data, DWORD t);
//-------------------------------------------------------------

static P2PCORECBFUNCS *s_pCallbackFuncs = NULL;

static volatile BOOL s_bRunP2p;
static struct conn_bit_fields _stun_bf;
static p2pcore_transid_t s_TransId;
static PA_SPIN spin_trans_id;
static PA_HTHREAD thd_p2pcore = PA_HTHREAD_NULL;

typedef
struct local_conn_info {
	struct conn_bit_fields bits;
	struct p2pcore_addr addrs[10];
} LOCALCONNINFO;


//---------------------------------------------------
static unsigned short local_port_min=12001, local_port_max=12500;
static unsigned short local_port_cur=12001;

void set_local_port_range(unsigned short low, unsigned short hi)
{
	if(low < hi)
	{
		local_port_min = low;
		local_port_max = hi;
	}
	else
	{
		local_port_min = hi;
		local_port_max = low;
	}
	local_port_cur = local_port_min;
}
//---------------------------------------------------

int getPortOffset()
{
	int n, i;
	ETHERNIC nic[4];
	n = GetIpAddresses(nic, 4);
	for(i=0; i<n; i++)
	{
#ifdef __LINUX__
		if(nic[i].flags & (IFF_UP | IFF_RUNNING))
#endif
			return ntohl(nic[i].addr.s_addr) & 0xFF;
	}
	return 0;
}

int getUpnpMappingPort()
{
	return 23000 + getPortOffset();
}
static int p2p_main_port = 0;	//the port to receive notification from server(or caller in LAN)
int getP2pMainPort()
{
	return p2p_main_port;
}

static char __SN[32];
static RUDPSOCKET upnp_rudp_sock;
//static int upnp_tcp_sock = -1;
static int udp_sock_main = INVALID_SOCKET;
static char stun_server[48] = "stun.voipbuster.com";//"stunserver.org";
static struct sockaddr_in p2p_server;
static volatile unsigned int g_tickNow;


static void detectNat(void *p)
{
#if 0
	BOOL preservePort, hairpin;
	unsigned short delta, delta2;
	struct sockaddr_in stunsvr;

	init_sai(&stunsvr, stun_server, STUNT_SERVER_PORT);
	_stun_bf.nat = stunNatType(stunsvr.sin_addr.s_addr, &preservePort, &hairpin, &delta);
	dbg_p2p("detect stun NatType: %d, delta = %d", _stun_bf.nat, delta);
	if(_stun_bf.nat != 0)
	{
		_stun_bf.preserve_port = preservePort?1:0;
		_stun_bf.hairpin = hairpin?1:0;
		_stun_bf.delta = _stun_bf.nat == StunTypeDependentMapping?(delta<8?delta:0):0;
		if(_stun_bf.nat == StunTypeDependentMapping && delta < 8)
		{
			stunNatType(stunsvr.sin_addr.s_addr, &preservePort, &hairpin, &delta2);
			if(delta != delta2)
			{
				_stun_bf.delta = 0;
				dbg_p2p("Stun: port cannot predicted");
			}
		}
	}

	int interval;
	if(_stun_bf.nat == 0) interval = 30;
	else if(_stun_bf.nat == StunTypeDependentMapping && _stun_bf.delta == 0) interval = 300;
	else interval = 3600;
	TimerQueueQueueItem(SLOW_QUEUE, detectNat, NULL, interval*1000, "detectNat");
#else
//NatType simple_stun_type(DWORD ip, int *preserve_port, int *hairpin, unsigned short *delta);
	int r = detect_nat(stun_server);
	_stun_bf.nat = r&0x0F;
	_stun_bf.hairpin = (r&0x20)?1:0;
	_stun_bf.preserve_port = (r&0x10)?1:0;
	_stun_bf.delta = (_stun_bf.nat == StunTypeDependentMapping)?(r>>8):0;
	dbg_p2p("NAT detected, nat:%d, preserve port:%d, delta:%d", (int)_stun_bf.nat, (int)_stun_bf.preserve_port, (int)_stun_bf.delta);
#endif
}


static void init_punch_package(struct p2pcore_punch *pdp, const P2PCONN *pconn)
{
	init_p2pcore_header(&pdp->dh, pconn->is_caller?ST_CALLER:ST_CALLEE, OP_PUNCH, CLS_REQUEST, 0, 
			sizeof(struct p2pcore_punch)-sizeof(struct p2pcore_header) + pconn->tmp->auth_len + 1, 0);
	memcpy(pdp->sess_id, pconn->sess_id, LENGTH_OF_SESSION_ID);
	memset(pdp->sn, 0, LENGTH_OF_SN);
	strncpy(pdp->sn, pconn->is_caller?pconn->tmp->sess_req.sn:__SN, LENGTH_OF_SN);

	if(pconn->tmp->auth_str)
		memcpy(pdp->auth_str, pconn->tmp->auth_str, pconn->tmp->auth_len+1);
}

static void broadcastOnAllInterfaces(const struct sockaddr *dst, const void *s, int len)
{
	int i, n_nic;
	ETHERNIC an[5];
	struct sockaddr_in sai;


	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	n_nic = GetIpAddresses(an, 5);

	// PA_Send on each NIC
	for(i=0; i<n_nic; i++)
	{
#ifdef __LINUX__
		if(!(an[i].flags & IFF_UP)) continue;
#endif

		int sock = socket(AF_INET, SOCK_DGRAM, 0);
		sai.sin_addr = an[i].addr;
		if(bind(sock, (struct sockaddr*)&sai, sizeof(sai)) < 0)
			;

		int iopt = 1;
		PA_SetSockOpt(sock, SOL_SOCKET, SO_BROADCAST, &iopt, sizeof(int));
		if(PA_SendTo(sock, s, len, 0, (struct sockaddr*)dst, sizeof(struct sockaddr)) < 0)
			perror("send_to ");
		PA_SocketClose(sock);
	}
}

static void handleSearch(int sock_r)
{
	struct { struct p2pcore_header dh; char buff[400]; } st;
	struct sockaddr_in sa_clt;
	socklen_t sa_len = sizeof(sa_clt);
	int len = PA_RecvFrom(sock_r, &st, sizeof(st), 0, (struct sockaddr*)&sa_clt, &sa_len);
	if(len > 0)
	{
		if(P2PCORE_OP(&st.dh) == OP_SEARCH)
		{
			if(st.dh.length != 0)
			{
				st.buff[P2PCORE_DATA_LEN(&st.dh)] = '\0';
				if(strcmp(__SN, st.buff)) return;
			}

			st.dh.cls = CLS_RESPONSE;
			unsigned short *port = (unsigned short*)st.buff;
			*port = htons(getP2pMainPort());
			int len = sprintf(st.buff+2, __SN) + 3;
			init_p2pcore_header(&st.dh, ST_CALLEE, OP_SEARCH, CLS_RESPONSE, 0, len, 0);

			broadcastOnAllInterfaces((struct sockaddr*)&sa_clt, &st, len + sizeof(struct p2pcore_header));
		}
#if 0
		else if(strncmp("CMD *",buff, 5) == 0)
		{
			if(!tok || strcmp(tok, __SN) != 0)
			{
				dbg_p2p(" %s Not me.",tok);
				return;
			}

			struct NetworkInfo addrInfo; 

			if( s_pCallbackFuncs && s_pCallbackFuncs->SetNetworkInfo && s_pCallbackFuncs->SetNetworkInfo(&addrInfo))
				len = sprintf(buff,"RCMD * CTP/1.0\r\n result 0\r\n\r\n");
			else len = sprintf(buff,"RCMD * CTP/1.0\r\n result 1\r\n\r\n");

			sa_clt.sin_addr.s_addr = inet_addr("255.255.255.255");
			sa_clt.sin_port = htons(8999);
			broadcastOnAllInterfaces((struct sockaddr*)&sa_clt, buff, len);
			return;	
		}
#endif
		else
			return;
	}
}

BOOL CalleeIsRegistered()
{
	return __SN[0] != '\0';
}

static P2PCONN *allocConn()
{
	P2PCONN *pconn = (P2PCONN*)calloc(sizeof(P2PCONN), 1);
	pconn->dwTag = HP2PCONN_TAG;
	pconn->state = PSS_ALLOCATED;
	pconn->sock = INVALID_SOCKET;
	pconn->rudp_sock = INVALID_RUDPSOCKET;
	pconn->timeout_val = SESSION_TIMEOUT;

	INIT_LIST_HEAD(&pconn->list);
	PA_MutexLock(s_mutexConnList);
	list_add_tail(&pconn->list, &s_P2pConnList);
	PA_MutexUnlock(s_mutexConnList);
	return pconn;
}

//! Close a CONNECTED session.
static void abortConn(HP2PCONN hconn, int err)
{
	if(!hconn->err)
	{
		hconn->err = err;
		if(hconn->sibling)
		{
			hconn->sibling->err = err;
			//recyled later
			hconn->sibling->state = PSS_CLOSED;
			hconn->state = PSS_CLOSED;
		}
		else
		{
			_CALLCB(ConnAbortionNotify)(hconn, err);
		}
	}
}

static void cleanAndFreeConn(HP2PCONN hconn)
{
	if(hconn->tmp)
	{
		if(PA_SocketIsValid(hconn->tmp->sock2)) 
			PA_SocketClose(hconn->tmp->sock2);
		if(hconn->tmp->out_sock)
			RUDPClose(hconn->tmp->out_sock);
		if(hconn->tmp->auth_str) free(hconn->tmp->auth_str);
		free(hconn->tmp);
	}
	if(hconn->rudp_sock) RUDPClose(hconn->rudp_sock);
	if(PA_SocketIsValid(hconn->sock)) PA_SocketClose(hconn->sock);
	if(hconn->sbuff) free(hconn->sbuff);
	free(hconn);
}

static void collectLocalConnInfo(LOCALCONNINFO *pLci, unsigned short local_port);
void collectLocalConnInfo(LOCALCONNINFO *pLci, unsigned short local_port)
{
	uint32_t local_ips[5];
	int i, n_local_ip;
	struct conn_bit_fields *pbf;

	memset(pLci, 0, sizeof(LOCALCONNINFO));
	//pbf = psi->bits.tcp?&_stunt_bf:&_stun_bf;
	pbf = &_stun_bf;

	n_local_ip = get_local_ips(local_ips, 5);
	for(i=0; i<n_local_ip; i++)
	{
		pLci->addrs[i].ip = local_ips[i];
		pLci->addrs[i].port = htons(local_port);
		pLci->addrs[i].flags = 0;
	}
	if(UpnpIgdCpGetNatMappedAddress(getUpnpMappingPort(), &pLci->addrs[i].ip, &pLci->addrs[i].port))
	{
		pLci->addrs[i].flags = 1;
		n_local_ip++;
	}
#if 0
	else if(_stun_bf.nat != StunTypeOpen)//To allow create port-mapping manually(23000+nnnn)
	{
		pLci->addrs[i].ip = 0;
		pLci->addrs[i].port = htons(getUpnpMappingPort());
		pLci->addrs[i].flags = 0;
		n_local_ip++;
	}
#endif
#ifdef _DEBUG
	for(i=0; i<n_local_ip; i++)
		dbg_p2p("local ip:port = %s:%d:%d", inet_ntoa(*((struct in_addr*)&pLci->addrs[i].ip)), 
				ntohs(pLci->addrs[i].port), pLci->addrs[i].flags);
#endif

	pLci->bits.reserved = 0;
	pLci->bits.tcp = 0;
	pLci->bits.auth= 0;
	pLci->bits.n_addr = n_local_ip;
	pLci->bits.nat = pbf->nat;
	pLci->bits.delta = pbf->delta;
	pLci->bits.hairpin = pbf->hairpin;
	pLci->bits.preserve_port = pbf->preserve_port;
}


void prepareForPunching(P2PCONN *pconn, const struct p2pcore_session_init *psi, unsigned int now)
{
	if(pconn->state == PSS_PUNCHING) return;

	int sock, sock2, sa_len;
	struct conn_bit_fields *pbf;
	struct sockaddr_in sai_tmp;

	//pbf = psi->bits.tcp?&_stunt_bf:&_stun_bf;
	pbf = &_stun_bf;
	sock = sock2 = INVALID_SOCKET;

	pconn->state = PSS_PUNCHING;

	if(!pconn->tmp)
	{
		pconn->tmp = (struct punch_tmp*)calloc(sizeof(struct punch_tmp), 1);
		pconn->tmp->sock2 = INVALID_SOCKET;
	}
	memcpy(pconn->sess_id, psi->sess_id, LENGTH_OF_SESSION_ID);
	pconn->bits = psi->bits;
	memcpy(pconn->tmp->addrs, psi->addrs, sizeof(struct p2pcore_addr)*pconn->bits.n_addr);

	// *  Create the socket(sock) to PA_Send acknowladgement. 
	//    For tcp, this socket will be closed but it's port is used for future punching.
	//    For udp, this socket is used for communication with client either.
	//
	//
	// ** If a SYMMETRICE NAT is not predictable but does preserve port, we create another socket to get a 
	//    new bound port, and hopt the external port to be the same with it.
	//
	//    * For tcp, this socket will be closed and use it's bound port instead of the port of [sock] for punching.
	//
	//    * For udp, this socket(sock2) is used for punching, [sock] will be kept until punching finished because 
	//      the server may not receive the ack and send the notification again. When this happens, ipcam should 
	//      re-send ack on [sock]. 
	//      When punching finished, [sock] is closed and set to [sock2], [sock2] is to -1.
	if(!PA_SocketIsValid(pconn->sock))
	{
		sock = NewSocketAndBind(psi->bits.tcp?SOCK_STREAM:SOCK_DGRAM, 0, 0);
		setblk(sock, 0);
	}
	else
		sock = pconn->sock;

	if( pconn->bits.ct == P2P_CONNTYPE_P2P && pbf->nat == StunTypeDependentMapping && pbf->delta == 0 && pbf->preserve_port )
	{
		if(!PA_SocketIsValid(pconn->tmp->sock2)) //tmp->sock2 might be created at the initialization of caller's conn
			sock2 = NewSocketAndBind(psi->bits.tcp?SOCK_STREAM:SOCK_DGRAM, 0, 0);
#ifdef VERBOSE
		sa_len = sizeof(sai_tmp);
		PA_GetSockName(sock2, (struct sockaddr*)&sai_tmp, &sa_len);
		dbg_p2p("Bind to new local port: %d for dependent mapping nat.", ntohs(sai_tmp.sin_port));
#endif
	}
	sa_len = sizeof(sai_tmp);
	PA_GetSockName( PA_SocketIsValid(sock2) ? sock2 : sock, (struct sockaddr*)&sai_tmp, &sa_len);
	pconn->tmp->local_port = ntohs(sai_tmp.sin_port);
	dbg_p2p("session local port: %d", pconn->tmp->local_port);
	if(pconn->bits.tcp && PA_SocketIsValid(sock2)) PA_SocketClose(sock2);

	pconn->sock = sock;
	if(!psi->bits.tcp) pconn->tmp->sock2 = sock2;
	pconn->tmp->n_try = 0;
	pconn->last_access = now;

#ifdef _DEBUG
	{
		int i, len; char line[256];
		for(i=len=0; i<16; i++) len+=sprintf(line+len, "%02X ", pconn->sess_id[i]);
		dbg_p2p("New session queued: %s, n_addr:%d, auth:%d", line, (int)pconn->bits.n_addr, (int)pconn->bits.auth);
	}
#endif
	//
	// Create address report as the ack
	//
	if(!pconn->is_caller)
	{
		LOCALCONNINFO lci;
		collectLocalConnInfo(&lci, pconn->tmp->local_port);
		lci.bits.ct = pconn->bits.ct;
		lci.bits.tcp = pconn->bits.tcp;
		pconn->tmp->bsa_len = create_session_init_ack((uint8_t*)pconn->tmp->bsa, lci.bits, lci.addrs, 
				__SN, psi->sess_id, P2PCORE_TID(&psi->dh));
	}
}

struct hb_data {
	unsigned int interval;	//interval to report itself
	unsigned int last_report;
	BOOL got_report_response;
	int report_retry_cnt;
};


/* Heart beat with p2p server */
static void heart_beat(struct hb_data *_hb, unsigned int now)
{
	if(_hb->got_report_response && (now - _hb->last_report >= _hb->interval))
	{
		uint32_t tmp;
		struct p2pcore_i_am_here diah;

		init_p2pcore_header(&diah.dh, ST_CALLEE, OP_DS_IAMHERE, CLS_REQUEST, 0, LENGTH_OF_SN, 0);
		memset(diah.sn, 0, LENGTH_OF_SN);
		strncpy((char*)diah.sn, __SN, LENGTH_OF_SN);
		diah.stun.nat = _stun_bf.nat;
		diah.stun.preserve_port = _stun_bf.preserve_port;
		diah.stun.hairpin = _stun_bf.hairpin;
		diah.stun.delta = _stun_bf.delta;
		diah.stun.upnp = UpnpIgdCpGetNatMappedAddress(getUpnpMappingPort(), &tmp, (unsigned short*)&tmp)?1:0;
		PA_SendTo(udp_sock_main, &diah, sizeof(diah), 0, (struct sockaddr*)&p2p_server, sizeof(p2p_server));

		_hb->got_report_response = 0;
		_hb->last_report = now;
	}
	else if(!_hb->got_report_response && (now - _hb->last_report > MAX_TIME_WAIT_FOR_ACK))
	{
		_hb->report_retry_cnt++;
		if(_hb->report_retry_cnt > 4)
		{
			//!* Some router may keep the old mapping even if the external ip changed.
			//  So we actively bind to a new local port, and discard the old mapping.
			int s = NewSocketAndBind(SOCK_DGRAM, 0, 0);
			PA_SocketClose(udp_sock_main);
			udp_sock_main = s;
			setblk(udp_sock_main, 0);

			int sa_len;
			struct sockaddr_in sai_tmp;
			sa_len = sizeof(sai_tmp);
			PA_GetSockName(udp_sock_main, (struct sockaddr*)&sai_tmp, &sa_len);
			p2p_main_port = ntohs(sai_tmp.sin_port);

			_CALLCB(NetworkStateChanged)(NETSTATE_SERVER_DOES_NOT_RESPONSE);
			_hb->report_retry_cnt = 0;
		}

		// So we'll send hear-beat immediately in next loop
		_hb->last_report = 0; _hb->got_report_response = 1;
	}
}

// News session notification & Heart beat acknowledgement
void handle_main_udp_sock(struct hb_data *_hb, unsigned now)
{
	union {
		struct p2pcore_session_init bsn;
		struct p2pcore_i_am_here_ack diaa;
		struct p2pcore_header dh;
		char buf[256];
	} uu;
	int len, sa_len;
	struct sockaddr_in sai;

	while(1)
	{
		sa_len = sizeof(sai);
		len = PA_RecvFrom(udp_sock_main, uu.buf, sizeof(uu), 0, (struct sockaddr*)&sai, &sa_len);
		if(len < 0) break;

		if(len < sizeof(struct p2pcore_header) || !check_p2pcore_header(&uu.bsn.dh)) continue;

		if(uu.bsn.dh.st == ST_SERVER)
		{
			if(P2PCORE_OP(&uu.bsn.dh) == OP_SD_SESSION_NOTIFY)
			{	
				dbg_bin("new session request:", uu.bsn.sess_id, 16);
				_calleeConnInit(&sai, &uu.bsn, now);
			}
			else if(P2PCORE_OP(&uu.bsn.dh) == OP_DS_IAMHERE)
			{
				if(sai.sin_addr.s_addr != p2p_server.sin_addr.s_addr) continue;

				_hb->interval = (uu.dh.status == P2PS_INACTIVE)?INACTIVE_INTERVAL:NORMAL_INTERVAL;
				len = P2PCORE_DATA_LEN(&uu.dh);
				if(P2PCORE_STATUS(&uu.dh) == P2PS_ADDRESS_CHANGED)
				{
					dbg_p2p("Device Change server address...");
					uu.diaa.data[len] = '\0';
					init_sai(&p2p_server, uu.diaa.data, P2PCORE_SERVICE_PORT);
				}
				_hb->got_report_response = 1;
				_hb->report_retry_cnt = 0;
				_CALLCB(NetworkStateChanged)(NETSTATE_OK);
			}
		}
	}
}

/* conn in initialized waiting */
static void handle_conn_in_waiting(P2PCONN *pconn, unsigned now)
{
	int sa_len, len;
	struct sockaddr_in sai_tmp;
	union {
		struct p2pcore_session_init bsn;
		struct p2pcore_punch	dp;
		struct p2pcore_header dh;
		char buf[512];
	} uu;

	sa_len = sizeof(sai_tmp);
	len = PA_RecvFrom(pconn->sock, uu.buf, 500, 0, (struct sockaddr*)&sai_tmp, &sa_len);
	if(len >= sizeof(struct p2pcore_header) && check_p2pcore_header(&uu.bsn.dh))
	{
		if(P2PCORE_OP(&uu.bsn.dh) == OP_CS_SESSION_INIT && uu.bsn.dh.cls == CLS_RESPONSE)
		{
			if(P2PCORE_STATUS(&uu.bsn.dh) == P2PS_CONTINUE)
			{
				dbg_p2p("Change state to PSS_WAIT_FOR_PEER_INFO");
				pconn->tmp->n_try = 0;
				pconn->state = PSS_WAIT_FOR_PEER_INFO;
			}
			else if(uu.bsn.dh.status == 0 || P2PCORE_STATUS(&uu.bsn.dh) == P2PS_CHANGE_CONN_TYPE) {
				if(uu.bsn.dh.status || pconn->bits.ct == P2P_CONNTYPE_RELAY) {
					PA_SocketClose(pconn->sock);
					pconn->sock = INVALID_SOCKET;
				}
				prepareForPunching(pconn, &uu.bsn, now);
				if(pconn->bits.ct == P2P_CONNTYPE_P2P)
				{
					if(!PA_SocketIsValid(pconn->tmp->sock2))
					{
						PA_SocketClose(pconn->sock);
						pconn->sock = pconn->tmp->sock2;
						pconn->tmp->sock2 = INVALID_SOCKET;
					}
					udp_punch_it(pconn);
					pconn->state = PSS_PUNCHING;
				}
				else if(pconn->tmp->tps_state == 0 && pconn->bits.ct == P2P_CONNTYPE_RELAY)
				{
					memset(&sai_tmp, 0, sizeof(sai_tmp));
					sai_tmp.sin_family = AF_INET;
					sai_tmp.sin_addr.s_addr = uu.bsn.addrs[0].ip;
					sai_tmp.sin_port = uu.bsn.addrs[0].port;
					connect(pconn->sock, (struct sockaddr*)&sai_tmp, sizeof(sai_tmp));
					pconn->tmp->tps_state = TPS_CONNECT;
					pconn->state = PSS_PUNCHING;
					dbg_p2p("caller begin connecting to relayer");
				}
			}
			else {
				_CALLCB(ConnFailed)(P2PCORE_STATUS(&uu.bsn.dh), pconn->pUserData);
				pconn->state = PSS_FAILED;
			}
		}
		else if(P2PCORE_OP(&uu.bsn.dh) == OP_SC_SESSION_BEGIN && uu.bsn.dh.cls == CLS_REQUEST)
		{
			dbg_p2p("Reveived: OP_SC_SESSION_BEGIN");
			struct p2pcore_header dh;
			init_p2pcore_header(&dh, ST_CALLER, OP_SC_SESSION_BEGIN, CLS_RESPONSE, 0, 0, P2PCORE_TID(&uu.bsn.dh));
			PA_SendTo(pconn->sock, &dh, sizeof(dh), 0, (struct sockaddr*)&sai_tmp, sizeof(sai_tmp));
			prepareForPunching(pconn, &uu.bsn, now);
			udp_punch_it(pconn);
		}
	}
}

static void process_tcp_conn_data(P2PCONN *pconn, unsigned int now)
{
	BYTE buf[2048], *buff;
	int buff_size, len;

	pconn->last_access = now;
	while(1)
	{
		if(pconn->rbuff)
		{
			buff = pconn->rbuff + pconn->rbuff_off;
			buff_size = pconn->rbuff_size - pconn->rbuff_off;
		}
		else
		{
			buff = (BYTE*)buf;
			buff_size = sizeof(buf);
		}

		len = PA_Recv(pconn->sock, buff, buff_size, 0);
		if(len < 0 && PA_SocketGetError() == EWOULDBLOCK)
			break;
		if(__LIKELY(len > 0))
		{
			_CALLCB(OnData)((HP2PCONN)pconn, buff, len);
			pconn->is_hb_sent = 0;
		}
		else
		{
			if(len == 0) dbg_p2p("peer closed");
			else dbg_p2p("problem on receiving packet: %d", PA_SocketGetError());	
			//dbg_p2p("***********Conn closed by peer");	
			abortConn(pconn, P2PE_CONN_ABORTED);
			break;
		}
	}
}

static void _ConnCreated(HP2PCONN hconn)
{
	if(hconn->bits.sibling_sess)
	{
		struct list_head *p, *q;
		list_for_each_safe(p, q, &s_P2pConnList)
		{
			P2PCONN *pconn = list_entry(p, P2PCONN, list);
			if(pconn->state == PSS_CONNECTED && pconn->bits.sibling_sess && 
					memcmp(hconn->sess_id, pconn->sess_id, LENGTH_OF_SESSION_ID) == 0)
			{
				hconn->sibling = pconn;
				pconn->sibling = hconn;
				hconn->sbuff = (char*)malloc(1500);
				pconn->sbuff = (char*)malloc(1500);
				hconn->sb_data_len = pconn->sb_data_len = 0;
				pconn->sb_size = pconn->sb_size = 1500;
				break;
			}
		}
	}
	else
		_CALLCB(ConnCreated)(hconn);
}

//--------------------------------------------------------------------
static RUDPSOCKCHNO rudp_r_set[64], rudp_w_set[16];	//boundary check
static int n_rsock, n_wsock;
static int sock_srch = INVALID_SOCKET;
static RUDPSOCKET accepted_ru_sock[MAX_ACCEPTED_RUDPSOCK];
static int n_accepted_ru_sock = 0;

static void _collectPollingSockets()
{
	int i;
	struct list_head *p;
	P2PCONN *pconn;

	n_rsock = n_wsock = 0;

	/* collect sockets for polling */
	if(PA_SocketIsValid(sock_srch)) RUDP_FD_SET(sock_srch, rudp_r_set, n_rsock);
	if(PA_SocketIsValid(udp_sock_main)) RUDP_FD_SET(udp_sock_main, rudp_r_set, n_rsock);
	if(upnp_rudp_sock) RUDP_SET(upnp_rudp_sock, -1, rudp_r_set, n_rsock);
	PA_MutexLock(s_mutexConnList);
	list_for_each(p, &s_P2pConnList)
	{
		pconn = list_entry(p, P2PCONN, list);
		if(__UNLIKELY(pconn->state == PSS_WAIT_FOR_PEER_INFO || pconn->state == PSS_WAIT_FOR_SERVER_ACK))
		{
			if(PA_SocketIsValid(pconn->sock)) RUDP_FD_SET(pconn->sock, rudp_r_set, n_rsock);
		}
		else if(__LIKELY(pconn->state == PSS_CONNECTED && !pconn->err && pconn->mode == P2PCONN_MODE_PUSH))
		{
			if(!pconn->sibling || !pconn->sibling->sb_data_len)
			{
				if(pconn->bits.tcp == 0 && pconn->rudp_sock)
					RUDP_SET(pconn->rudp_sock, 0, rudp_r_set, n_rsock);
				else if(PA_SocketIsValid(pconn->sock))
					RUDP_FD_SET(pconn->sock, rudp_r_set, n_rsock);
			}
			if(pconn->sibling && pconn->sibling->sb_data_len)
			{
				if(pconn->bits.tcp == 0 && pconn->rudp_sock)
					RUDP_SET(pconn->rudp_sock, pconn->sb_chno, rudp_w_set, n_wsock);
				else if(PA_SocketIsValid(pconn->sock))
					RUDP_FD_SET(pconn->sock, rudp_w_set, n_wsock);
			}
		}
		else if(__UNLIKELY(pconn->state == PSS_RUDP_CONNECTING))
			RUDP_SET(pconn->rudp_sock, 0, rudp_w_set, n_wsock);
		else if(__LIKELY(pconn->state == PSS_PUNCHING))
		{
			if(pconn->bits.tcp)
			{
				if(pconn->tmp->tps_state == TPS_CONNECT)
					RUDP_FD_SET(pconn->sock, rudp_w_set, n_wsock);
				else if(pconn->tmp->tps_state == TPS_CONNECTED)
					RUDP_FD_SET(pconn->sock, rudp_r_set, n_rsock);
			}
			else
			{
				if(pconn->tmp->out_sock != NULL)
				{
					if(pconn->tmp->oss == OSS_CONNECT) RUDP_SET(pconn->tmp->out_sock, 0, rudp_w_set, n_wsock);
					else if(pconn->tmp->oss == OSS_CONNECTED) RUDP_SET(pconn->tmp->out_sock, 0, rudp_r_set, n_rsock);
				}
				if(PA_SocketIsValid(pconn->sock))
					RUDP_FD_SET(pconn->sock, rudp_r_set, n_rsock);
				if(PA_SocketIsValid(pconn->tmp->sock2))
					RUDP_FD_SET(pconn->tmp->sock2, rudp_r_set, n_rsock);
			}
		}
	}
	PA_MutexUnlock(s_mutexConnList);
	//accepted rudp connections
	for(i=0; i<n_accepted_ru_sock; i++) 
		if(accepted_ru_sock[i] != INVALID_RUDPSOCKET) RUDP_SET(accepted_ru_sock[i], 0, rudp_r_set, n_rsock);
}

static PA_THREAD_RETTYPE __STDCALL P2pThread(void* p)
{
	struct sockaddr_in sai_tmp;
	int sa_len, i;

	struct hb_data _hb_data = { NORMAL_INTERVAL, 0, TRUE, 0 };
	/*
	 * 保留端口用于UPNP路由器映射
	 */
	//upnp_tcp_sock = CreateServiceSocket(SOCK_STREAM, 0, getUpnpMappingPort());
	upnp_rudp_sock = RUDPSocket();
	memset(&sai_tmp, 0, sizeof(sai_tmp));
	sai_tmp.sin_family = AF_INET;
	sai_tmp.sin_port = htons(getUpnpMappingPort());
	RUDPBind(upnp_rudp_sock, (struct sockaddr*)&sai_tmp, sizeof(sai_tmp));
	RUDPListen(upnp_rudp_sock, 5);

	if(CalleeIsRegistered())
	{
		sock_srch = CreateServiceSocket(SOCK_DGRAM, 0, 7999);

		udp_sock_main = CreateServiceSocket(SOCK_DGRAM, 0, 0);
		setblk(udp_sock_main, 0);
		sa_len = sizeof(sai_tmp);
		PA_GetSockName(udp_sock_main, (struct sockaddr*)&sai_tmp, &sa_len);
		p2p_main_port = ntohs(sai_tmp.sin_port);
	}

	dbg_p2p(">>>>>>>>>>>> P2P started <<<<<<<<<<<<");

	while(s_bRunP2p)
	{
		struct list_head *p, *q;
		P2PCONN *pconn;
		g_tickNow = PA_GetTickCount();

		if(CalleeIsRegistered() && p2p_server.sin_addr.s_addr)
			heart_beat(&_hb_data, g_tickNow);

		_collectPollingSockets();

		/* polling */
		struct timeval tv = { 0, 100000 };
		if(RUDPSelect(rudp_r_set, &n_rsock, rudp_w_set, &n_wsock, NULL, NULL, &tv) > 0)
		{
			union {
				struct p2pcore_session_init bsn;
				struct p2pcore_punch	dp;
				struct p2pcore_header dh;
				char buf[2048];
			} uu;
			int len;

			if(RUDP_FD_ISSET(sock_srch, rudp_r_set, n_rsock))
				handleSearch(sock_srch);
			if(PA_SocketIsValid(udp_sock_main) && RUDP_FD_ISSET(udp_sock_main, rudp_r_set, n_rsock))
				handle_main_udp_sock(&_hb_data, g_tickNow);

			/* Accept incoming connection */
			if(RUDP_ISSET(upnp_rudp_sock, rudp_r_set, n_rsock))
			{
				RUDPSOCKET rskt = NULL;
				sa_len = sizeof(sai_tmp);
				if(RUDPAccept(upnp_rudp_sock, &rskt, (struct sockaddr*)&sai_tmp, &sa_len) == 0)
				{
					dbg_p2p("incoming connection accepted. %s:%d", inet_ntoa(sai_tmp.sin_addr), ntohs(sai_tmp.sin_port));
					if(n_accepted_ru_sock >= MAX_ACCEPTED_RUDPSOCK)
					{
						RUDPClose(accepted_ru_sock[0]);
						for(i=1; i<n_accepted_ru_sock; i++) accepted_ru_sock[i-1] = accepted_ru_sock[i];
						n_accepted_ru_sock--;
					}
					accepted_ru_sock[n_accepted_ru_sock++] = rskt;
				}
			}
			/* Accepted connection */
			for(i=0; i<n_accepted_ru_sock; i++)
			{
				if(accepted_ru_sock[i] != INVALID_RUDPSOCKET && RUDP_ISSET(accepted_ru_sock[i], rudp_r_set, n_rsock))
				{
					int chno, err, has_sess = 0;
					if( (err = RUDPRecv(accepted_ru_sock[i], &chno, &uu, sizeof(uu), 0)) > 0 && check_p2pcore_header(&uu.dh) &&
							uu.dh.st == ST_CALLER && P2PCORE_OP(&uu.dh) == OP_PUNCH)
					{
						PA_MutexLock(s_mutexConnList);
						list_for_each(p, &s_P2pConnList)
						{
							pconn = list_entry(p, P2PCONN, list);
							if(pconn->state == PSS_PUNCHING && 
									memcmp(pconn->sess_id, uu.dp.sess_id, LENGTH_OF_SESSION_ID) == 0)
							{
								has_sess = 1;
								if(handle_punch(pconn, &uu.dp, NULL, accepted_ru_sock[i]) == 0)
									has_sess = 1;
								break;
							}
						}
						PA_MutexUnlock(s_mutexConnList);
					}
					if(!has_sess) RUDPClose(accepted_ru_sock[i]);
					accepted_ru_sock[i] = INVALID_RUDPSOCKET;
				}
			}
			while(n_accepted_ru_sock && accepted_ru_sock[n_accepted_ru_sock-1] == INVALID_RUDPSOCKET)
				n_accepted_ru_sock--;


			/*
			 * Conns in Waiting or Punching or Active
			 */
			PA_MutexLock(s_mutexConnList);
			list_for_each_safe(p, q, &s_P2pConnList)
			{
				pconn = list_entry(p, P2PCONN, list);
				/* Waiting */
				if(__UNLIKELY(pconn->state == PSS_WAIT_FOR_PEER_INFO || pconn->state == PSS_WAIT_FOR_SERVER_ACK))
				{/* for caller */
					if(PA_SocketIsValid(pconn->sock) && RUDP_FD_ISSET(pconn->sock, rudp_r_set, n_rsock))
					{
						handle_conn_in_waiting(pconn, g_tickNow);
					}
				}
				else if(pconn->bits.tcp)
				{   /* Punching */
					if(__UNLIKELY(pconn->state == PSS_PUNCHING && 
								PA_SocketIsValid(pconn->sock)/*tcp_punch_thread may be started and pconn->sock is closed*/))
					{
						union { struct p2pcore_punch dp; char buff[256]; } u;

						if(pconn->tmp->tps_state == TPS_CONNECT)
						{
							if(RUDP_FD_ISSET(pconn->sock, rudp_w_set, n_wsock))
							{
								int err, optlen = sizeof(int);
								if(PA_GetSockOpt(pconn->sock, SOL_SOCKET, SO_ERROR, &err, &optlen) == 0 && err == 0)
								{
									init_punch_package(&u.dp, pconn);
									if(PA_Send(pconn->sock, &u, P2PCORE_PACKET_LEN(&u.dp.dh), 0) < 0)
										perror("send punch");
									pconn->tmp->tps_state = TPS_CONNECTED;
									dbg_p2p("tps_state: CONNECTED");
								}
								else
								{
									pconn->tmp->tps_state = TPS_CONN_FAILED;
								}
							}
						}
						else if(pconn->tmp->tps_state == TPS_CONNECTED)
						{
							if(RUDP_FD_ISSET(pconn->sock, rudp_r_set, n_rsock))
							{
								if(pconn->bits.ct == P2P_CONNTYPE_RELAY) 
								{
									int len;
									if( (len = PA_Recv(pconn->sock, &u, sizeof(u), 0)) >= sizeof(struct p2pcore_punch) && 
											check_p2pcore_header(&u.dp.dh) && P2PCORE_OP(&u.dp.dh) == OP_PUNCH 
											/*&& ((u.dp.dh.st == pconn->is_caller)?ST_CALLEE:ST_CALLER)*/)
									{
										dbg_p2p("punch recevied");
										handle_punch(pconn, &u.dp, &sai_tmp, NULL);
									}
									else
									{
										if(len < 0) dbg_p2p("punch recv error: %d", PA_SocketGetError());
										else dbg_p2p("punch recv bytes: %d", len);
										if(pconn->is_caller) _CALLCB(ConnFailed)(P2PE_FAILED, pconn->pUserData);
										pconn->state = PSS_FAILED;
									}
								}
							}
						}
					}
					/* Data */
					else if(pconn->state == PSS_CONNECTED && !pconn->err)
					{
						if(RUDP_FD_ISSET(pconn->sock, rudp_r_set, n_rsock))
						{//Process Data
							process_tcp_conn_data(pconn, g_tickNow);
						}
					}
				}
				else //udp
				{
					if(__UNLIKELY(pconn->state == PSS_RUDP_CONNECTING))
					{
						/* for callee&caller */
						if(RUDP_ISSET(pconn->rudp_sock, rudp_w_set, n_wsock))
						{
							pconn->state = PSS_CONNECTED;
							dbg_p2p("rudp connect ok");
							_ConnCreated(pconn);
						}
					}
					else if(__UNLIKELY(pconn->state == PSS_PUNCHING))
					{
						PA_SOCKET s[2]; 
						int i, cnt = 0;
						if(PA_SocketIsValid(pconn->sock) && RUDP_FD_ISSET(pconn->sock, rudp_r_set, n_rsock)) 
							s[cnt++] = pconn->sock;
						if(PA_SocketIsValid(pconn->tmp->sock2) && RUDP_FD_ISSET(pconn->tmp->sock2, rudp_r_set, n_rsock)) 
							s[cnt++] = pconn->tmp->sock2;
						for(i=0; i<cnt; i++)
						{
							while(1) 
							{
								sa_len = sizeof(sai_tmp);
								len = PA_RecvFrom(pconn->sock, uu.buf, 500, 0, (struct sockaddr*)&sai_tmp, &sa_len);
								if(len < 0) break;
								if(len >= sizeof(struct p2pcore_header) && check_p2pcore_header(&uu.bsn.dh))
								{
									if(P2PCORE_OP(&uu.bsn.dh) == OP_PUNCH)
									{
										if(P2PCORE_STATUS(&uu.bsn.dh))
										{
											if(pconn->is_caller) _CALLCB(ConnFailed)(P2PCORE_STATUS(&uu.bsn.dh), pconn->pUserData);
											pconn->state = PSS_FAILED;
											dbg_p2p("Punching failed: %d", P2PCORE_STATUS(&uu.bsn.dh));
										}
										else
											handle_punch(pconn, (const struct p2pcore_punch*)&uu.dh, &sai_tmp, NULL);
									}
									else if(P2PCORE_OP(&uu.bsn.dh) == OP_SC_SESSION_BEGIN && uu.bsn.dh.cls == CLS_REQUEST)
									{
										struct p2pcore_header dh;
										init_p2pcore_header(&dh, ST_CALLER, OP_SC_SESSION_BEGIN, CLS_RESPONSE, 0, 0, P2PCORE_TID(&uu.bsn.dh));
										PA_SendTo(pconn->sock, &dh, sizeof(dh), 0, (struct sockaddr*)&sai_tmp, sizeof(sai_tmp));
									}
								}
							}
						}
						/* pconn->tmp->out_sock might be closed and set to NULL by handle_punch(...) */
						if(pconn->state == PSS_PUNCHING && pconn->tmp->out_sock)
						{
							if(pconn->tmp->oss == OSS_CONNECT && RUDP_ISSET(pconn->tmp->out_sock, rudp_w_set, n_wsock))
							{
								union { struct p2pcore_punch dp; char buf[256]; } u;
								init_punch_package(&u.dp, pconn);
								RUDPSend(pconn->tmp->out_sock, 0, &u.dp, P2PCORE_PACKET_LEN(&u.dp.dh), 0);
								pconn->tmp->oss = OSS_CONNECTED;
							}
							if(pconn->tmp->oss == OSS_CONNECTED && RUDP_ISSET(pconn->tmp->out_sock, rudp_r_set, n_rsock))
							{
								int chno;
								len = RUDPRecv(pconn->tmp->out_sock, &chno, uu.buf, 500, 0);
								if(len >= sizeof(struct p2pcore_header) && check_p2pcore_header(&uu.bsn.dh) && 
										P2PCORE_OP(&uu.bsn.dh) == OP_PUNCH)
								{
									if(P2PCORE_STATUS(&uu.bsn.dh))
									{
										if(pconn->is_caller) _CALLCB(ConnFailed)(P2PCORE_STATUS(&uu.bsn.dh), pconn->pUserData);
										pconn->state = PSS_FAILED;
										dbg_p2p("Punching failed: %d", P2PCORE_STATUS(&uu.bsn.dh));
									}
									else handle_punch(pconn, (const struct p2pcore_punch*)&uu.dh, NULL, NULL);
								}
							}
						}
					}
					else if(pconn->state == PSS_CONNECTED && !pconn->err && pconn->rudp_sock != INVALID_RUDPSOCKET)
					{
						if(RUDP_ISSET(pconn->rudp_sock, rudp_r_set, n_rsock))
						{
							int chno;
							pconn->last_access = g_tickNow;
							while(1)
							{
								BYTE *buff;
								int buff_size;

								buff = pconn->rbuff?(pconn->rbuff + pconn->rbuff_off):(BYTE*)uu.buf;
								buff_size = pconn->rbuff?(pconn->rbuff_size - pconn->rbuff_off):sizeof(uu.buf);

								len = RUDPRecv(pconn->rudp_sock, &chno, buff, buff_size, 0);
								if(len == ERUDP_AGAIN) break;
								if(len <= 0)
								{
									dbg_p2p(len?"Error in receiving rudp packet: %d":"Peer closed session", len);
									abortConn(pconn, P2PE_CONN_ABORTED);
									break;
								}
								else if(!pconn->sibling)
								{
									_CALLCB(OnData)((HP2PCONN)pconn, buff, len);
									pconn->is_hb_sent = 0;
								}
								else
								{
									int r = RUDPSend(pconn->sibling->rudp_sock, chno, buff, len, 0);
									if(r == ERUDP_AGAIN)
									{
										memcpy(pconn->sibling->sbuff, buff, len);
										pconn->sibling->sb_data_len = len;
										pconn->sibling->sb_chno = chno;
										break;
									}
									else if(r < 0)
									{
										abortConn(pconn, P2PE_CONN_ABORTED);
										break;
									}
								}
							}
						}
						else if(RUDP_ISSET(pconn->rudp_sock, rudp_w_set, n_wsock))
						{
							if(RUDPSend(pconn->rudp_sock, pconn->sb_chno, pconn->sbuff, pconn->sb_data_len, 0) >= 0)
								pconn->sb_data_len = 0;
							else
								abortConn(pconn, P2PE_CONN_ABORTED);
						}
					}

					/* Port prediction, only expect OP_PUNCH */  //state == PSS_PUNCHING ?
					if(pconn->tmp && PA_SocketIsValid(pconn->tmp->sock2) && RUDP_FD_ISSET(pconn->tmp->sock2, rudp_r_set, n_rsock))
					{
						sa_len = sizeof(sai_tmp);
						len = PA_RecvFrom(pconn->tmp->sock2, uu.buf, 500, 0, (struct sockaddr*)&sai_tmp, &sa_len);
						if(len > sizeof(struct p2pcore_header) && check_p2pcore_header(&uu.bsn.dh) && P2PCORE_OP(&uu.dh) == OP_PUNCH)
							handle_punch(pconn, (const struct p2pcore_punch*)&uu.dh, &sai_tmp, NULL);
					}
				}
			}
			PA_MutexUnlock(s_mutexConnList);
		}

		PunchNewAndKillTimeoutedConn(g_tickNow);
	}

	PA_SocketClose(udp_sock_main);
	RUDPClose(upnp_rudp_sock);
	//PA_SocketClose(upnp_tcp_sock);

	return NULL;
}


//---------------------------------------------------------------------------
static void udp_punch_it(P2PCONN* pconn)
{
	int i;
	struct sockaddr_in sai;
	union { struct p2pcore_punch dp; char buff[256]; } u;

	init_punch_package(&u.dp, pconn);

	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	dbg_p2p("udp_punch_it: num of addresses = %d: ", (pconn->bits.n_addr));
	for(i = pconn->bits.n_addr - 1; i >= 0; i--)
	{
		sai.sin_addr.s_addr = pconn->tmp->addrs[i].ip;
		sai.sin_port = pconn->tmp->addrs[i].port; 
		if(pconn->tmp->addrs[i].flags & 1)
		{
			if(pconn->tmp->out_sock == NULL)
			{
				pconn->tmp->out_sock = RUDPSocket();
				int opt = 1; RUDPSetSockOpt(pconn->tmp->out_sock, OPT_NBLK, &opt, sizeof(int));
				RUDPConnect(pconn->tmp->out_sock, (struct sockaddr*)&sai, sizeof(sai));
				pconn->tmp->oss = OSS_CONNECT;
			}
		}
		else
		{
			int i, n = 1;
			if(pconn->bits.nat == StunTypeDependentMapping && pconn->bits.delta && !pconn->bits.preserve_port) 
				n = 4;	// already +delta at server side

			for(i=0; i<n; i++)
			{
				PA_SendTo(PA_SocketIsValid(pconn->tmp->sock2)?pconn->tmp->sock2:pconn->sock, &u, 
						P2PCORE_PACKET_LEN(&u.dp.dh), 0, (struct sockaddr*)&sai, sizeof(sai));
				dbg_p2p("%s:%d:%d, ", inet_ntoa(sai.sin_addr), (int)ntohs(sai.sin_port), pconn->tmp->addrs[i].flags);
				sai.sin_port = htons(ntohs(sai.sin_port) + pconn->bits.delta);
			}
		}
	}
	dbg_p2p("\n");
	pconn->tmp->n_try ++;
}


void PunchNewAndKillTimeoutedConn(unsigned int now)
{
	struct list_head *p, *q;

	PA_MutexLock(s_mutexConnList);
	list_for_each_safe(p, q, &s_P2pConnList)
	{
		P2PCONN* pconn = list_entry(p, P2PCONN, list);
		if(pconn->state == PSS_CONNECTED)
		{
			if(!pconn->err && now > pconn->last_access && pconn->timeout_val)
			{
				if(!pconn->is_hb_sent)
				{
					if(now - pconn->last_access > pconn->timeout_val/2)
					{
						_CALLCB(OnSendHb)(pconn);
						pconn->is_hb_sent = TRUE;
					}
				}
				else if(now - pconn->last_access > pconn->timeout_val)
				{
					dbg_p2p("session timeouted. now=%u, last_access=%u", now, pconn->last_access);
					abortConn(pconn, P2PE_CONN_TIMEOUTED);
				}
			}
		}
		else if(pconn->state == PSS_PUNCHING || pconn->state == PSS_RUDP_CONNECTING)
		{
			static const int _wait_ms[] = { 100, 400, 600, 800, 1000, 1500 };
			if(pconn->state == PSS_PUNCHING && pconn->tmp->n_try < sizeof(_wait_ms)/sizeof(int))
			{
				if(now - pconn->last_access >= _wait_ms[pconn->tmp->n_try])
				{
					dbg_p2p("tcp:%d", (int)pconn->bits.tcp);
					if(pconn->bits.tcp) pconn->tmp->n_try++;
					else {
						dbg_p2p("udp_punch_it(%dth), last_access:%d, now:%d", pconn->tmp->n_try, pconn->last_access, now);
						udp_punch_it(pconn);
					}
					pconn->last_access = now;
				}
			}
			else if(pconn->state == PSS_PUNCHING || (now - pconn->last_access) > 4000)
			//timeouted
			{
				if(pconn->is_caller)
				{
					if(!pconn->bits.tcp && pconn->bits.ct == P2P_CONNTYPE_P2P)
					{
						/* Change to relay */
						pconn->tmp->n_try = 0;
						pconn->tmp->sess_req.bits.ct = P2P_CONNTYPE_RELAY;
						pconn->tmp->sess_req.bits.tcp = 1;
						pconn->tmp->sess_req.bits.n_addr = 0;
						P2PCORE_SET_DATA_LEN(&pconn->tmp->sess_req.dh, 
								sizeof(struct p2pcore_session_init) - sizeof(struct p2pcore_header));
						PA_SpinLock(spin_trans_id);
						P2PCORE_SET_TID(&pconn->tmp->sess_req.dh, s_TransId);
						s_TransId++;
						PA_SpinUnlock(spin_trans_id);
						list_add_tail(p, &s_P2pConnList);
						continue;
					}
					else
					{
						_CALLCB(ConnFailed)(P2PE_TIMEOUTED, pconn->pUserData);
						pconn->state = PSS_FAILED;
					}
				}
				else
					pconn->state = PSS_FAILED;
			}
		}
		else if(pconn->state == PSS_WAIT_FOR_SERVER_ACK)
		{
			static const int _udpWaitMs[] = { 100, 500, 800, 1200, 1500 }; //4s ?
			if(pconn->tmp->n_try < sizeof(_udpWaitMs)/sizeof(int))
			{
				if(now - pconn->last_access >= _udpWaitMs[pconn->tmp->n_try])
				{
					PA_SendTo(pconn->sock, &pconn->tmp->sess_req, pconn->tmp->bsa_len, 0, 
							(const struct sockaddr*)&pconn->tmp->p2p_serv, sizeof(pconn->tmp->p2p_serv));
					pconn->tmp->n_try++;
					pconn->last_access = now;
				}
			}
			else
			{
				_CALLCB(ConnFailed)(P2PE_SERVER_NO_RESPONSE, pconn->pUserData);
				pconn->state = PSS_FAILED;
			}
		}
		else if(pconn->state == PSS_WAIT_FOR_PEER_INFO)
		{
			static const int _sessReqWaitMs[] = { 100, SESSION_REQUEST_TIMEOUT };
			if(pconn->tmp->n_try < sizeof(_sessReqWaitMs)/sizeof(int))
			{
				if(now - pconn->last_access >= _sessReqWaitMs[pconn->tmp->n_try])
				{
					pconn->tmp->n_try++;
					pconn->last_access = now;
				}
			}
			else
			{
				_CALLCB(ConnFailed)(P2PE_NO_RESPONSE, pconn->pUserData);
				pconn->state = PSS_FAILED;
			}
		}
		else if(pconn->state == PSS_CLOSED)
		{
			list_del(p);
			cleanAndFreeConn(pconn);
		}
		else if(pconn->state == PSS_FAILED)
		{
			list_del(p);
			cleanAndFreeConn(pconn);
			/*
			if(PA_SocketIsValid(pconn->sock)) PA_SocketClose(pconn->sock);
			if(pconn->tmp)
			{
				if(PA_SocketIsValid(pconn->tmp->sock2)) 
					PA_SocketClose(pconn->tmp->sock2);
				if(pconn->tmp->out_sock)
					RUDPClose(pconn->tmp->out_sock);
				if(pconn->tmp->auth_str) free(pconn->tmp->auth_str);
				free(pconn->tmp);
			}
			free(pconn);
			*/
		}
	}
	PA_MutexUnlock(s_mutexConnList);
}

int _calleeConnInit(const struct sockaddr_in* pSvrAddr, const struct p2pcore_session_init* psi, unsigned int now)
{
	P2PCONN *pconn;
	struct list_head *p;
	int sock;
	struct sockaddr_in sai_tmp;

	// Received on udp_main_sock, need to
	// Check to see session is not queued, 
	//
	dbg_bin("_calleeConnInit, sess_id: ", psi->sess_id, LENGTH_OF_SESSION_ID);
	list_for_each(p, &s_P2pConnList)
	{
		pconn = list_entry(p, P2PCONN, list);
		if(memcmp(pconn->sess_id, psi->sess_id, LENGTH_OF_SESSION_ID) == 0)
		{
			sock = pconn->sock;
			goto ack;
		}
	}

	/* Queue the session request */
	pconn = allocConn();
	if(psi->dh.status == P2PS_CHANGE_MAIN_PORT)
	{
		pconn->sock = udp_sock_main;

		/* Create new socket for:
		 * 	1. response for notification
		 * 	2. heart-beat 
		 */
		int sa_len = sizeof(sai_tmp);
		udp_sock_main = CreateServiceSocket(SOCK_DGRAM, 0, 0);
		setblk(udp_sock_main, 0);
		sa_len = sizeof(sai_tmp);
		PA_GetSockName(udp_sock_main, (struct sockaddr*)&sai_tmp, &sa_len);
		p2p_main_port = ntohs(sai_tmp.sin_port);
	}
	prepareForPunching(pconn, psi, now);


ack:
	if(__UNLIKELY(pconn->bits.tcp)) //tcp
	{
		if(__UNLIKELY(psi->bits.ct != P2P_CONNTYPE_P2P))
			PA_SendTo(udp_sock_main, pconn->tmp->bsa, pconn->tmp->bsa_len, 0, (struct sockaddr*)pSvrAddr, sizeof(struct sockaddr));
		if(pconn->tmp->tps_state == 0)
		{
			//PA_Send ack
			memset(&sai_tmp, 0, sizeof(sai_tmp));
			if(psi->bits.ct == P2P_CONNTYPE_RELAY) //tcp & relay
			{	//relaysvr's address
				sai_tmp.sin_family = AF_INET;
				sai_tmp.sin_addr.s_addr = pconn->tmp->addrs[0].ip;
				sai_tmp.sin_port = pconn->tmp->addrs[0].port;
				dbg_p2p("relay by %s:%d......", inet_ntoa(sai_tmp.sin_addr), ntohs(sai_tmp.sin_port));
			}
			else //tcp & p2p
			{
				//p2psvr's port, which receives ack of tcp
				memcpy(&sai_tmp, pSvrAddr, sizeof(sai_tmp));
				sai_tmp.sin_port = htons(ntohs(sai_tmp.sin_port)+1);
			}
			connect(pconn->sock, (struct sockaddr*)&sai_tmp, sizeof(sai_tmp));
			pconn->tmp->tps_state = TPS_CONNECT;
		}
	}
	else
	{
		PA_SendTo(psi->dh.status==P2PS_CHANGE_MAIN_PORT?udp_sock_main:pconn->sock, 
				pconn->tmp->bsa, pconn->tmp->bsa_len, 0, (struct sockaddr*)pSvrAddr, sizeof(struct sockaddr));
		udp_punch_it(pconn);	//Is it useful to punch so early?
	}

	if(pconn->bits.sibling_sess)
	{
		_P2pConnInitInternal(pSvrAddr, psi->sn, NULL/*sident*/, NULL/*auth_str*/, 0/*auth_len*/, 
				NULL/*pIdent*/, P2P_CONNTYPE_P2P, psi->sess_id2);
	}

	return 0;
}

static int callee_tcp_punch_cb(int sock, int status, void* data)
{
	P2PCONN* pconn = (P2PCONN*)data;
	struct p2pcore_punch dp;

	switch(status)
	{
		case SOCK_STATUS_ACCEPTED:
		case SOCK_STATUS_CONNECTED:
			return CHECKCONNECTION_CONTINUE;

		case SOCK_STATUS_READABLE:
			dbg_p2p("%d recved client tcp punch", sock);
			if(PA_Recv(sock, &dp, sizeof(dp), 0) >= sizeof(struct p2pcore_header) && 
					check_p2pcore_header(&dp.dh) &&
					memcmp(dp.sess_id, pconn->sess_id, LENGTH_OF_SESSION_ID) == 0)
			{
				if(dp.dh.st != ST_CALLEE) return CHECKCONNECTION_CONTINUE;
				if(dp.dh.status == 0)
				{
					dbg_p2p("client tcp punch checked");
					pconn->sock = sock;
					if(handle_punch(pconn, &dp, NULL, NULL) == 0)
						return CHECKCONNECTION_OK;
					else
						return 0;
				}
				else
					return -dp.dh.status;
			}
			else
				return CHECKCONNECTION_FAKE;

			break;
	}
	return -10000;
}

static int caller_tcp_punch_cb(int sock, int status, void* data)
{
	int len;
	union { struct p2pcore_header dh; struct p2pcore_punch dp; char buff[256]; } u;
	P2PCONN *pconn = (P2PCONN*)data;

	switch(status)
	{
	case SOCK_STATUS_ACCEPTED:
		//return CHECKCONNECTION_FAKE;
	case SOCK_STATUS_CONNECTED:
		init_punch_package(&u.dp, pconn);
		if(send(sock, (char*)data, P2PCORE_PACKET_LEN((struct p2pcore_header*)data), 0) < 0)
		{
			int err = PA_SocketGetError();
			if(err == ECONNABORTED)
			{
				//send(sock, (char*)data, P2PCORE_PACKET_LEN((struct p2pcore_header*)data), 0);
			}
		}
		return CHECKCONNECTION_CONTINUE;

	case SOCK_STATUS_READABLE:
		len = recv(sock, (char*)&u, sizeof(u), 0);
		if(len <= 0) 
		{
			int err = PA_SocketGetError();
			if(err == ECONNRESET)		//Simultineously open, reseted by OS
				return CHECKCONNECTION_RESETED;
		}
		if(len >= sizeof(struct p2pcore_header) && check_p2pcore_header(&u.dh) && P2PCORE_OP(&u.dh) == OP_PUNCH &&
                        u.dh.cls == CLS_RESPONSE && u.dh.st == ST_CALLEE)
			return (u.dh.status == 0)?CHECKCONNECTION_OK:-u.dh.status;
		break;
	}
	return -10000;
}


PA_THREAD_RETTYPE __STDCALL tcp_punch_thread(void* data)
{
	P2PCONN *pconn = (P2PCONN*)data;
	int sock;
	if(p2pcore_tcp_punch(pconn->tmp->local_port, -1/*upnp_tcp_sock*/, pconn->bits, pconn->tmp->addrs, &sock, 
				pconn->is_caller?caller_tcp_punch_cb:callee_tcp_punch_cb, pconn) == 0)
	{
		dbg_p2p("tcp_punch_thread -> connected.\n");
		if(pconn->tmp->auth_str) free(pconn->tmp->auth_str);
		free(pconn->tmp);
		pconn->tmp = NULL;
		pconn->state = PSS_CONNECTED;
		_ConnCreated(pconn);
		pconn->sock = sock;
	}
	else
	{
		//set to a large value, so the PunchNewAndKillTimeoutedConn() will delete ths failed session
		//pconn->tmp->n_try = 100;	
		//
		//Don't do that.
		/* 服务器可能重发了开始会话请求，而PUNCHING操作已经开始并最终失败。PUNCHING返回后,
		   PnPConn对象如果被立即撤销，则重发的会话请求会被当作新的会话处理.
		   所以应该让会话延迟撤销
		*/
		pconn->last_access = PA_GetTickCount();
		pconn->state = PSS_FAILED;
	}
	return (PA_THREAD_RETTYPE)0;
}


int handle_punch(P2PCONN* pconn, const struct p2pcore_punch* pdp, const struct sockaddr_in* ppeer_addr, RUDPSOCKET accepted_rsock)
{
	if(pconn->state != PSS_PUNCHING)
	{
		dbg_p2p("Conn is activated or died");
		return -1;
	}

	if(memcmp(pconn->sess_id, pdp->sess_id, LENGTH_OF_SESSION_ID))
	{
		dbg_p2p("unmatched session id.");
		return -1;
	}

	dbg_p2p("%s", pconn->bits.tcp?"tcp":"udp");
	if(pconn->bits.auth || pconn->bits.ct == P2P_CONNTYPE_LOCAL)
	{
		//calculate and compare ac
		BOOL bAuthOk = FALSE;
		if(s_pCallbackFuncs && s_pCallbackFuncs->VerifyAuthString) 
			bAuthOk = s_pCallbackFuncs->VerifyAuthString(pdp->auth_str);
		if(!bAuthOk)
		{
			struct p2pcore_header dh;
			init_p2pcore_header(&dh, ST_CALLEE, OP_PUNCH, CLS_RESPONSE, P2PS_AUTH_FAILED, 0, P2PCORE_TID(&pdp->dh));
			if(pconn->bits.tcp)
				PA_Send(pconn->sock, &dh, sizeof(dh), 0);
			else
				PA_SendTo(pconn->sock, &dh, sizeof(dh), 0, (struct sockaddr*)ppeer_addr, sizeof(struct sockaddr_in));

			//set to a large value, so the PunchNewAndKillTimeoutedConn() will delete ths failed session
			pconn->tmp->n_try = 100; 
			return -1;
		}
	}

	if(pconn->bits.tcp)
	{
		socklen_t sa_len = sizeof(pconn->peer_addr);
		getpeername(pconn->sock, (struct sockaddr*)&pconn->peer_addr, &sa_len);

		{ int opt = 0x40000; //max is 0x32800
		PA_SetSockOpt(pconn->sock, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(int));
		}
	}
	else if(ppeer_addr) //udp
	{
		dbg_p2p("establish udp session ...");
		if(pconn->tmp->out_sock) { RUDPClose(pconn->tmp->out_sock); pconn->tmp->out_sock = NULL; }
		if(PA_SocketIsValid(pconn->tmp->sock2)) 
		{ 
			PA_SocketClose(pconn->sock); pconn->sock = pconn->tmp->sock2; pconn->tmp->sock2 = INVALID_SOCKET; 
		}

		union { struct p2pcore_punch punch; char buff[256]; } u;
		init_punch_package(&u.punch, pconn);
		PA_SendTo(pconn->sock, &u.punch, P2PCORE_PACKET_LEN(&u.punch.dh), 0, (struct sockaddr*)ppeer_addr, sizeof(struct sockaddr_in));
		memcpy(&pconn->peer_addr, ppeer_addr, sizeof(struct sockaddr));

		pconn->rudp_sock = RUDPSocketFromUdp(pconn->sock);
		int opt=1; RUDPSetSockOpt(pconn->rudp_sock, OPT_NBLK, &opt, sizeof(opt));
		pconn->sock = -1;
		if(RUDPConnect(pconn->rudp_sock, (const struct sockaddr*)&pconn->peer_addr, sizeof(struct sockaddr)) != ERUDP_AGAIN)
		{
			pconn->state = PSS_FAILED;
			dbg_p2p("rudp connect failed");
			return -1;
		}
		else
		{
			pconn->state = PSS_RUDP_CONNECTING;
			if(pconn->tmp->auth_str) free(pconn->tmp->auth_str);
			free(pconn->tmp);
			pconn->tmp = NULL;
			dbg_p2p("rudp connecting....");
			return 0;
		}
	}
	else //rudp?
	{
		if(PA_SocketIsValid(pconn->tmp->sock2)) { PA_SocketClose(pconn->tmp->sock2); pconn->tmp->sock2 = INVALID_SOCKET; }
		if(PA_SocketIsValid(pconn->sock)) { PA_SocketClose(pconn->sock); pconn->sock = INVALID_SOCKET; }
		if(pconn->tmp->out_sock)
		{
			//RUDPSend(pconn->tmp->out_sock, 0, &punch, sizeof(struct p2pcore_punch), 0);
			pconn->rudp_sock = pconn->tmp->out_sock;
			pconn->tmp->out_sock = NULL;
		}
		else if(accepted_rsock)
		{
			//RUDPSend(accepted_rsock, 0, &punch, sizeof(struct p2pcore_punch), 0);
			pconn->rudp_sock = accepted_rsock;
			if(pconn->tmp->out_sock) { RUDPClose(pconn->tmp->out_sock); pconn->tmp->out_sock = NULL; }
			int opt = 1; RUDPSetSockOpt(pconn->rudp_sock, OPT_NBLK, &opt, sizeof(int));
		}
		RUDPGetPeerName(pconn->rudp_sock, (struct sockaddr*)&pconn->peer_addr);
	}

	if(pconn->tmp->auth_str) free(pconn->tmp->auth_str);
	free(pconn->tmp);
	pconn->tmp = NULL;
	pconn->state = PSS_CONNECTED;
/*
 * 	already set to non-block mode at previous steps
	if(pconn->rudp_sock)
	{
		int opt = 1; RUDPSetSockOpt(pconn->rudp_sock, OPT_NBLK, &opt, sizeof(int));
	}
	else if(pconn->sock > 0)
		setblk(pconn->sock, 0);
*/
	_ConnCreated(pconn);

	return 0;
}

int create_session_init_ack(uint8_t *buf, struct conn_bit_fields bits, const struct p2pcore_addr* paddrs, 
		const char* sn, const uint8_t* sess_id, uint32_t trans_id)
{
	struct p2pcore_session_init* psna = (struct p2pcore_session_init*)buf;
	init_p2pcore_header(&psna->dh, ST_CALLEE, OP_SD_SESSION_NOTIFY, CLS_RESPONSE, 0, 
			sizeof(struct p2pcore_session_init) + sizeof(struct p2pcore_addr)*bits.n_addr - sizeof(struct p2pcore_header),
			trans_id);
	psna->bits = bits;
	memset(psna->sident, 0, LENGTH_OF_SIDENT);
	memset(psna->sn, 0, LENGTH_OF_SN);
	memcpy(psna->sn, sn, LENGTH_OF_SN);
	memcpy(psna->sess_id, sess_id, LENGTH_OF_SESSION_ID);

	memcpy(psna->addrs, paddrs, sizeof(struct p2pcore_addr)* bits.n_addr);

	return P2PCORE_PACKET_LEN(&(psna->dh));
}
#if 0
int sencRecvOverUdp(int sock, const struct p2pcore_head *pOut, struct sockaddr_in *pTgt, void *pIn, int size, const int *wait_ms, int n_wait)
{
	int i;
	for(i=0; i<n_wait; i++)
	{
		struct sockaddr_in sai;
		socklen_t sa_len;
		int len;

		sa_len = sizeof(sai);
		PA_SendTo(sock, pOut, P2PCORE_PACKET_LEN(pOut), 0, 
				(const struct sockaddr*)pTgt, sizeof(struct sockaddr_in));
		sa_len = sizeof(sai);
		len = timed_recv_from(sock, pIn, size, &sai, &sa_len, wait_ms[i]);
		if(len <= 0) { continue; }
		return len;
	}
	return P2PE_NO_RESPONSE;
}
#endif
//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
int _P2pConnInitInternal(const struct sockaddr_in *sai_svr, const char *pcsSN, const uint8_t *sident/*LENGTH_OF_SIDENT*/, 
		const char *auth_str, int auth_len, void *pIdent, int ct, const uint8_t *sess_id)
{
	struct sockaddr_in sai;
	socklen_t sa_len;
	P2PCONN *pconn;
	union {	struct p2pcore_session_init req; uint8_t buf[256]; } u;
	int udp_sock, sock2 = INVALID_SOCKET;

	if(!pcsSN) return P2PE_INVALID_PARAM;

	udp_sock = NewSocketAndBind(SOCK_DGRAM, 0, 0);
	if(!PA_SocketIsValid(udp_sock))
		return P2PE_SOCKET_ERROR;
	setblk(udp_sock, 0);

	sa_len = sizeof(sai);
	if(_stun_bf.nat == StunTypeDependentMapping && _stun_bf.preserve_port)
	{
		sock2 = NewSocketAndBind(SOCK_DGRAM, 0, 0);
	}
	else
		PA_GetSockName(udp_sock, (struct sockaddr*)&sai, &sa_len);

	// Request session
	memset(&u.req, 0, sizeof(u.req));
	u.req.bits.ct = ct==P2P_CONNTYPE_AUTO?P2P_CONNTYPE_P2P:ct;
	if(sess_id) 
	{
		memcpy(u.req.sess_id, sess_id, LENGTH_OF_SESSION_ID);
		u.req.bits.sibling_sess = 1;
	}
	if(ct == P2P_CONNTYPE_RELAY) 
	{
		u.req.bits.tcp = 1;
	}
	else
	{
		LOCALCONNINFO lci;
		collectLocalConnInfo(&lci, ntohs(sai.sin_port));
		u.req.bits = lci.bits;
		memcpy(u.req.addrs, lci.addrs, sizeof(struct p2pcore_addr)*lci.bits.n_addr);
	}
	strncpy(u.req.sn, pcsSN, LENGTH_OF_SN);
	if(sident) memcpy(u.req.sident, sident, LENGTH_OF_SIDENT);
	init_p2pcore_header(&u.req.dh, ST_CALLER, OP_CS_SESSION_INIT, CLS_REQUEST, 0, 
			sizeof(u.req)-sizeof(struct p2pcore_header) + u.req.bits.n_addr * sizeof(struct p2pcore_addr), s_TransId ++);

	pconn = allocConn();
	pconn->last_access = PA_GetTickCount();
	pconn->sock = udp_sock;
	pconn->pUserData = pIdent;
	pconn->is_caller = 1;
	pconn->bits = u.req.bits;
	pconn->tmp = (struct punch_tmp*)calloc(sizeof(struct punch_tmp), 1);
	pconn->tmp->sock2 = sock2;
	if(auth_str) {
		pconn->tmp->auth_str = (char*)malloc(auth_len+1);
		memcpy(pconn->tmp->auth_str, auth_str, auth_len+1);
		pconn->tmp->auth_len = auth_len;
	}

	pconn->state = PSS_WAIT_FOR_SERVER_ACK;
	pconn->tmp->bsa_len = P2PCORE_DATA_LEN(&u.req.dh);
	memcpy(&pconn->tmp->sess_req, &u.req, pconn->tmp->bsa_len);
	memcpy(&pconn->tmp->p2p_serv, sai_svr, sizeof(struct sockaddr));
	if(PA_SendTo(udp_sock, &u.req, P2PCORE_PACKET_LEN(&u.req.dh), 0, (const struct sockaddr*)sai_svr, sizeof(struct sockaddr_in)) < 0)
		perror("sendto");

	return 0;
}

int P2pConnInitEx(const char *p2psvr, const char *pcsSN, const uint8_t *sident/*LENGTH_OF_SIDENT*/, 
		const char *auth_str, int auth_len, void *pIdent, int ct)
{
	struct sockaddr_in sai_svr;
	if(init_sai(&sai_svr, p2psvr, P2PCORE_SERVICE_PORT) != 0)
			return P2PE_CANNOT_RESOLVE_HOST;
	sai_svr.sin_port = htons(P2PCORE_SERVICE_PORT);
	return _P2pConnInitInternal(&sai_svr, pcsSN, sident, auth_str, auth_len, pIdent, ct, NULL);
}

int P2pConnInit(const char *p2psvr, const char *pcsSN, const uint8_t *sident/*LENGTH_OF_SIDENT*/, 
		const char *auth_str, int auth_len, void *pIdent)
{
	return P2pConnInitEx(p2psvr, pcsSN, sident, auth_str, auth_len, pIdent, P2P_CONNTYPE_AUTO);
}

int P2pConnSetMode(HP2PCONN hconn, int mode)
{
	if(hconn->dwTag != HP2PCONN_TAG) return P2PE_INVALID_CONN_OBJECT;
	if(hconn->err) return hconn->err;
	hconn->mode = mode;
	if(mode == P2PCONN_MODE_PULL)
	{
		hconn->timeout_val = 0;
	}
	else
	{
		hconn->last_access = g_tickNow;
		hconn->timeout_val = SESSION_TIMEOUT;
	}

	return 0;
}

int P2pConnGetMode(HP2PCONN hconn)
{
	if(hconn->dwTag != HP2PCONN_TAG) return P2PE_INVALID_CONN_OBJECT;
	return hconn->mode;
}

int P2pConnCloseAsync(HP2PCONN hconn)
{
	if(hconn->dwTag != HP2PCONN_TAG) return P2PE_INVALID_CONN_OBJECT;
	hconn->state = PSS_CLOSED;
	return 0;
}

int P2pConnClose(HP2PCONN hconn)
{
	if(hconn->dwTag != HP2PCONN_TAG) return P2PE_INVALID_CONN_OBJECT;

	if(PA_ThreadGetCurrentHandle() == thd_p2pcore)
		hconn->state = PSS_CLOSED;
	else
	{
		PA_MutexLock(s_mutexConnList);
		list_del(&hconn->list);
		cleanAndFreeConn(hconn);
		PA_MutexUnlock(s_mutexConnList);
	}
	return 0;
}

int P2pConnWaitR(HP2PCONN hconn, int wait_ms)
{
	if(hconn->dwTag != HP2PCONN_TAG || hconn->state != PSS_CONNECTED) return P2PE_INVALID_CONN_OBJECT;
	if(hconn->err) return hconn->err;
	if(hconn->mode == P2PCONN_MODE_PUSH) return P2PE_NOT_ALLOWED;

	struct timeval tv;

	tv.tv_sec = wait_ms/1000;
	tv.tv_usec = (wait_ms%1000)*1000;
	if(hconn->bits.tcp)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(hconn->sock, &rfds);
		return select(hconn->sock+1, &rfds, NULL, NULL, &tv);
	}
	else
	{
		return RUDPSelectSock(hconn->rudp_sock, -1, RUDPSELECT_READABLE, &tv);
	}
}

int P2pConnWaitW(HP2PCONN hconn, int phy_chno, int wait_ms)
{
	if(hconn->dwTag != HP2PCONN_TAG || hconn->state != PSS_CONNECTED) return P2PE_INVALID_CONN_OBJECT;
	if(hconn->err) return hconn->err;

	struct timeval tv;
	P2PCONN *pconn = (P2PCONN*)hconn;

	tv.tv_sec = wait_ms/1000;
	tv.tv_usec = (wait_ms%1000)*1000;
	if(pconn->bits.tcp)
	{
		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(pconn->sock, &wfds);
		return select(pconn->sock+1, NULL, &wfds, NULL, &tv);
	}
	return RUDPSelectSock(pconn->rudp_sock, phy_chno, RUDPSELECT_WRITABLE, &tv);
}

int P2pConnRecv(HP2PCONN hconn, BYTE *pBuff, int size)
{
	if(hconn->dwTag != HP2PCONN_TAG || hconn->state != PSS_CONNECTED) return P2PE_INVALID_CONN_OBJECT;
	if(hconn->err) return hconn->err;

	int rlen;
	P2PCONN *pconn = (P2PCONN*)hconn;
	if(pconn->mode == P2PCONN_MODE_PUSH) return P2PE_NOT_ALLOWED;

	if(pconn->bits.tcp)
	{
		rlen = PA_Recv(pconn->sock, pBuff, size, 0);
		if(rlen < 0)
		{
			if(PA_SocketGetError() != EWOULDBLOCK) return P2PE_AGAIN;
			return -1;
		}
	}
	else
	{
		int chno;
		if( (rlen = RUDPRecv(pconn->rudp_sock, &chno, pBuff, size, 0)) < 0 )
		{
			if(rlen == ERUDP_AGAIN) 
				return P2PE_AGAIN;
			return -1;
		}
	}
	return rlen;
}

//! retval >=0 bytes sent
//! retval <0 error code
int P2pConnSendV(HP2PCONN hconn, int phy_chno, PA_IOVEC *v, int size_v, int wait_ms)
{
	if(hconn->dwTag != HP2PCONN_TAG || hconn->state != PSS_CONNECTED) return P2PE_INVALID_CONN_OBJECT;
	if(hconn->err) return hconn->err;

	int r;
	P2PCONN *pconn = (P2PCONN*)hconn;
	if(pconn->bits.tcp)
	{
		if(wait_ms && (r = timed_wait_fd_w(pconn->sock, wait_ms)) <= 0)
		{
			return r==0?P2PE_TIMEOUTED:P2PE_SOCKET_ERROR;
		}
#ifdef WIN32
		DWORD dwBytesSent;
		if(WSASend(pconn->sock, v, size_v, &dwBytesSent, 0, NULL, NULL) == SOCKET_ERROR)
		{
			if(WSAGetLastError() != WSAEWOULDBLOCK) return P2PE_AGAIN;
			return -1;
		}
		return dwBytesSent;
#elif defined(__LINUX__)
		struct msghdr msg;
		msg.msg_iov = v;
		msg.msg_iovlen = size_v;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = 0;
		msg.msg_name = (void*)&pconn->peer_addr;
		msg.msg_namelen = sizeof(struct sockaddr_in);
		if( (r = sendmsg(pconn->sock, &msg, MSG_NOSIGNAL)) < 0)
		{
			if(errno == EAGAIN) return P2PE_AGAIN;
			perror("P2pConnSendV==>sendmsg");
			return -1;
		}
		return r;
#endif
	}
	else
	{
		if(wait_ms)
		{
			struct timeval tv;
			tv.tv_sec = wait_ms/1000;
			tv.tv_usec = (wait_ms%1000)*1000;
			if( (r = RUDPSelectSock(pconn->rudp_sock, phy_chno, RUDPSELECT_WRITABLE, &tv)) <= 0)
				return r==0?P2PE_TIMEOUTED:P2PE_SOCKET_ERROR;
		}
		if( (r = RUDPSendV(pconn->rudp_sock, phy_chno, v, size_v, 0)) < 0)
		{
			if(r == ERUDP_AGAIN) return P2PE_AGAIN;
			return r;
		}
		return r;
	}
	return 0;
}

int P2pConnSend(HP2PCONN hconn, int phy_chno, void *pData, int len, int wait_ms)
{
	PA_IOVEC v;
	PA_IoVecSetPtr(&v, pData);
	PA_IoVecSetLen(&v, len);
	return P2pConnSendV(hconn, phy_chno, &v, 1, wait_ms);
}

int P2pConnGetInfo(HP2PCONN hconn, P2PCONNINFO *info)
{
	P2PCONN *pconn = (P2PCONN*)hconn;
	if(pconn->dwTag != HP2PCONN_TAG) return P2PE_INVALID_CONN_OBJECT;
	info->ct = pconn->bits.ct;
	memcpy(&info->peer, &pconn->peer_addr, sizeof(struct sockaddr_in));
	return pconn->err;
}

int P2pConnSetTimeout(HP2PCONN hconn, int sec/*default 15*/)
{
	if(hconn->dwTag != HP2PCONN_TAG || hconn->state != PSS_CONNECTED) return P2PE_INVALID_CONN_OBJECT;
	if(hconn->err) return hconn->err;

	P2PCONN *pconn = (P2PCONN*)hconn;
	int r = pconn->timeout_val/1000;
	if(sec != 0)
	{
		if(sec < 5) sec = 5;
		if(sec > 25) sec = 25;
		pconn->timeout_val = sec*1000;
	}
	return r;
}


int P2pConnSetUserBuffer(HP2PCONN hconn, void *pBuff, int size)
{
	P2PCONN *pconn = (P2PCONN*)hconn;
	if(pconn->dwTag != HP2PCONN_TAG) return P2PE_INVALID_CONN_OBJECT;
	pconn->rbuff = (BYTE*)pBuff;
	pconn->rbuff_size = size;
	pconn->rbuff_off = 0;
	return 0;
}

int P2pConnSetUserBufferOffset(HP2PCONN hconn, int offset)
{
	P2PCONN *pconn = (P2PCONN*)hconn;
	if(pconn->dwTag != HP2PCONN_TAG) return P2PE_INVALID_CONN_OBJECT;
	pconn->rbuff_off = offset;
	return 0;
}

///* Associate a user's pointer to session
int P2pConnSetUserData(HP2PCONN hconn, void *pUser)
{
	P2PCONN *pconn = (P2PCONN*)hconn;
	if(pconn->dwTag != HP2PCONN_TAG) return P2PE_INVALID_CONN_OBJECT;
	pconn->pUserData = pUser;
	return 0;
}

///* Get user's pointer associated with session
int P2pConnGetUserData(HP2PCONN hconn, void **ppUser)
{
	P2PCONN *pconn = (P2PCONN*)hconn;
	if(pconn->dwTag != HP2PCONN_TAG) return P2PE_INVALID_CONN_OBJECT;
	*ppUser = pconn->pUserData;
	return 0;
}


#if 0
void P2pNotifyAll(const void *pData, UINT nDataLen)
{
	struct list_head *p;
	P2PCONN *pconn;
	struct p2pcore_header dh;

	init_p2pcore_header(&dh, ST_CALLEE, OP_EVENT, 0, CLS_REQUEST, nDataLen, 0);
	dh.chno = 0;
	dh.end = 1;//isLast?1:0;

	PA_MutexLock(s_mutexConnList);
	list_for_each(p, &s_P2pConnList)
	{
		pconn = list_entry(p, P2PCONN, list);
		sendConnPacket(pconn, &dh, pData, nDataLen);
	}
	PA_MutexUnlock(s_mutexConnList);
}
#endif
//------------------------------------------------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------
#define MAX_KERNEL_SERVER	6
static char kernel_servers[MAX_KERNEL_SERVER][LENGTH_OF_SERVER];
static int n_ker_server = 0, i_ker_server = -1;

#if 0
/** 获取服务器  */
static PA_THREAD_RETTYPE _ThreadLogin(void *p)
{
	PA_ThreadCloseHandle(PA_ThreadGetCurrentHandle());

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	int cnt = 0;
	while(1)
	{
		struct sockaddr_in sai;
		if(init_sai(&sai, kernel_servers[i_ker_server], P2PCORE_SERVICE_PORT-1) == 0)
		{
			union {
				struct p2pcore_header dh;
				struct p2pcore_query_server_req req;
				struct p2pcore_query_server_resp resp;
				char buff[1024];
			} u;
			init_p2pcore_header(&u.dh, ST_CALLEE, OP_QUERY_SERVER, CLS_REQUEST, 0, 
					sizeof(struct p2pcore_query_server_req) - sizeof(struct p2pcore_header) + LENGTH_OF_SN, 0);
			u.req.flags = QSF_STUN_SERVER;
			u.req.n_q = 1;
			strcpy(u.req.sn[0], __SN);
			PA_SendTo(sock, &u, P2PCORE_PACKET_LEN(&u.dh), 0, (struct sockaddr*)&sai, sizeof(sai));
			if(timed_wait_fd(sock, 5000) > 0)
			{
				int sa_len = sizeof(sai);
				int len = PA_RecvFrom(sock, &u, sizeof(u), 0, (struct sockaddr*)&sai, &sa_len);
				if(len > sizeof(u.dh) && check_p2pcore_header(&u.dh) && u.dh.op == OP_QUERY_SERVER)
				{
					char *p = u.buff + sizeof(struct p2pcore_query_server_resp);
					struct sn_server_pair *ssp = (struct sn_server_pair*)(p + LENGTH_OF_SERVER * u.resp.n_stun_svr);
					struct sockaddr_in sai_tmp;

					// 返回多个stun服务器时, 随机选择一个
					char *s = p + LENGTH_OF_SERVER * (rand()%u.resp.n_stun_svr);
					if(init_sai(&sai_tmp, s, 3478) == 0)
					{
						strcpy(stun_server, s);
						detectNat(NULL);
					}
					else
					{
						dbg_p2p("resolve stun server failed\n");
						PA_Sleep(10000);
						continue;
					}

					//
					if(ssp[0].server[0] && strcmp(ssp[0].sn, __SN) == 0)
					{
						if(init_sai(&p2p_server, ssp[0].server, P2PCORE_SERVICE_PORT) == 0)
						{
							break;
						}
						else {
							dbg_p2p("resolve p2p server failed\n");
							PA_Sleep(10000);
						}
					}
					else {
						/* Invalid __SN */
						dbg_p2p("No p2p server for sn %s\n", __SN);
						break;
					}
				}
			}
		}
		i_ker_server ++;
		i_ker_server %= n_ker_server;
		cnt++;

		if(cnt % n_ker_server == 0)
			PA_Sleep(30000);
		else
			PA_Sleep(5000);
	}
	PA_SocketClose(sock);
	return (PA_THREAD_RETTYPE)0;
}
#endif

static PA_THREAD_RETTYPE __STDCALL _TimeConsumingInitializationTask(void *p)
{
	PA_ThreadCloseHandle(PA_ThreadGetCurrentHandle());

	while(s_bRunP2p)
	{
		struct sockaddr_in sai_tmp;
		if(init_sai(&sai_tmp, stun_server, 3478) != 0)
		{
			dbg_p2p("resolving failed.");
			PA_Sleep(5000);
#ifdef __LINUX__
			dbg_p2p("retrying...");
			res_close();
			res_init();
#endif
		}
		else
			break;
	}
	if(!s_bRunP2p) return (PA_THREAD_RETTYPE)0;
	dbg_p2p("servers resolved.");
#if 0
	PA_ThreadCreate(_ThreadLogin, NULL);
#else
	detectNat(NULL);
	init_sai(&p2p_server, kernel_servers[0], P2PCORE_SERVICE_PORT);
#endif
	/* 初始化随机数发生器 */
	unsigned int ip_gw, ip_loc;
	GetDefaultRoute(&ip_gw, &ip_loc);
	srand(time(NULL) * ip_loc);
	/* 随机选择一个服务器 */
	i_ker_server = rand() % n_ker_server;

	return (PA_THREAD_RETTYPE)0;
}

int P2pCoreInitialize(const char *svrs[], int n_svr, const char *sn, P2PCORECBFUNCS *cbs)
{
	if(thd_p2pcore) return 0;

	s_pCallbackFuncs = cbs;

	/* 复制服务器 */
	int i;
	if(n_svr > MAX_KERNEL_SERVER) n_svr = MAX_KERNEL_SERVER;
	for(i=0; i<n_svr && svrs[i]; i++)
		strcpy(kernel_servers[i], svrs[i]);
	n_ker_server = n_svr;


	if(sn) strcpy(__SN, sn);
	PA_MutexInit(s_mutexConnList);
	PA_SpinInit(spin_trans_id);

	g_pSlowTq = TimerQueueCreate();
	UpnpIgdCpInitialize();
	UpnpIgdCpAddPortMap("p2p", getUpnpMappingPort(), getUpnpMappingPort(), IGD_PORTTYPE_UDP);
	RUDPStart();

	s_bRunP2p = TRUE;
	PA_ThreadCreate(_TimeConsumingInitializationTask, NULL);
	PA_Sleep(500);

	if(thd_p2pcore == PA_HTHREAD_NULL)
		thd_p2pcore = PA_ThreadCreate(P2pThread, NULL);

	return 0;
}

void P2pCoreTerminate()
{
	s_bRunP2p = FALSE;
}

void P2pCoreCleanup()
{
	struct list_head *p, *q;

	s_bRunP2p = FALSE;
	PA_ThreadWaitUntilTerminate(thd_p2pcore);
	PA_ThreadCloseHandle(thd_p2pcore);

	list_for_each_safe(p, q, &s_P2pConnList)
	{
		P2PCONN *pconn = list_entry(p, P2PCONN, list);
		cleanAndFreeConn(pconn);
	}

	PA_MutexUninit(s_mutexConnList);
	PA_SpinUninit(spin_trans_id);
	thd_p2pcore = PA_HTHREAD_NULL;

	RUDPCleanup();

	UpnpIgdCpStop();
	if(g_pSlowTq) TimerQueueDestroy(g_pSlowTq);
	g_pSlowTq = NULL;
}

DWORD P2pCoreGetVersion()
{
	return 0x01000001;
}

BOOL P2pCoreEnumCallee(LPENUMCALLEE *ppEnumDev, UINT *pNDev, const char *pszSN)
{
	PA_SOCKET sk;
	int salen, iopt;
	struct sockaddr_in sa;
	ENUMCALLEE *pEnumDev;
	int size, cnt;
	struct { struct p2pcore_header dh; char buff[32]; } req;
	struct { struct p2pcore_header dh; ENUMCALLEE e; } resp;

	sk = socket(AF_INET, SOCK_DGRAM, 0);
	if(sk == INVALID_SOCKET) return FALSE;
	sa.sin_family = AF_INET;
	sa.sin_port = htons(7999);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	int opt = 1;
	PA_SetSockOpt(sk, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
	memset(sa.sin_zero, 0, sizeof(sa.sin_zero));
	bind(sk, (struct sockaddr*)&sa, sizeof(sa));
	salen = sizeof(sa);
	PA_GetSockName(sk, (struct sockaddr*)&sa, &salen);

	iopt = 1; PA_SetSockOpt(sk, SOL_SOCKET, SO_BROADCAST, (const char*)&iopt, sizeof(iopt));

	init_p2pcore_header(&req.dh, ST_CALLER, OP_SEARCH, 0, CLS_REQUEST, pszSN?(strlen(pszSN)+1):0, 0);
	if(pszSN) strcpy(req.buff, pszSN);
	size = (pszSN && *pszSN)?2:10;
	cnt = 0;
	pEnumDev = (LPENUMCALLEE)malloc(sizeof(ENUMCALLEE) * size);

	int i;
	for (i = 0; i < 1; i++) 
	{
		struct sockaddr_in safrom;
		int sasize, len;
		fd_set rfds;
		struct timeval tv;

		sa.sin_addr.s_addr = inet_addr("255.255.255.255");
		PA_SendTo(sk, &req, P2PCORE_PACKET_LEN(&req.dh), 0, (const struct sockaddr*)&sa, sizeof(sa));
		while(1)
		{
			FD_ZERO(&rfds);
			FD_SET(sk, &rfds);
			int err;
			tv.tv_sec = 1; tv.tv_usec = 0;
			if((err = select(sk+1, &rfds, NULL, NULL, &tv)) <= 0) 
			{
				break;
			} 
			else 
			{
				sasize = sizeof(safrom);
				len = PA_RecvFrom(sk, &resp, sizeof(resp), 0, (struct sockaddr*)&safrom, &sasize);
				if(len == sizeof(resp) && check_p2pcore_header(&resp.dh) && 
						resp.dh.cls == CLS_RESPONSE && P2PCORE_OP(&resp.dh) == OP_SEARCH)
				{
					if(pszSN && *pszSN)
					{
						if(strcmp(pszSN, pEnumDev[cnt].sn)) continue;
					}

					int ii;
					for(ii=0; ii<cnt; ii++)
					{
						if(strcmp(pEnumDev[ii].sn, resp.e.sn) == 0) break;
					}
					if(ii >= cnt) {
						memcpy(&pEnumDev[cnt], &resp.e, sizeof(ENUMCALLEE));
						cnt++;
					}
					if(pszSN) break;

					if(cnt >= size) {
						size += 5;
						pEnumDev = (ENUMCALLEE*)realloc(pEnumDev, sizeof(ENUMCALLEE)*size);
					}
				}
			}
		}
	}

	PA_SocketClose(sk);
	if(cnt){ 
		*ppEnumDev = pEnumDev;
	}else {
		free(pEnumDev);
		*ppEnumDev = NULL;
	}
	*pNDev = cnt;
	return cnt > 0;
}

void P2pCoreGetNatInfo(P2PCORENATINFO* pNi)
{
	uint32_t tmp;

	pNi->stunType = (NatType)_stun_bf.nat;
	pNi->delta_u = _stun_bf.delta;
	//pNi->stuntType = (NatType)_stunt_bf.nat;
	//pNi->delta_t = _stunt_bf.delta;
	pNi->bUpnpIgd = UpnpIgdCpGetNatMappedAddress(getUpnpMappingPort(), &tmp, (unsigned short*)&tmp)?1:0;
	//pNi->bMultiNat = upnp_nat_ext_ip && upnp_nat_ext_ip != internet_ip;
}

void P2pCoreEnumConn(ENUMCONNCB cb, void *pUser)
{
	struct list_head *p, *q;
	PA_MutexLock(s_mutexConnList);
	list_for_each_safe(p, q, &s_P2pConnList)
	{
		P2PCONN *pconn = list_entry(p, P2PCONN, list);
		if(pconn->state == PSS_CONNECTED)
			if(!cb(pconn, pUser)) break;
	}
	PA_MutexUnlock(s_mutexConnList);
	return;
}

