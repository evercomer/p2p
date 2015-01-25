#include "platform_adpt.h"
#include "relaysvr.h"
#include "p2pbase.h"
#include "netbase.h"
//#include "misc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
//#include <event2/event.h>
#include <event2/event-config.h>
#include <event.h>
#include "linux_list.h"
#include "p2plog.h"

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


#define RELAY_BUFF_SIZE	1600
struct media_sock {
	int sock;
	struct event *e;
	char *buff;	//size = RELAY_BUFF_SIZE
	int len;
};

struct RELAYSESSION {
	struct list_head list;
	unsigned char sess_id[16];
	uint32_t trans_id;

	struct media_sock ms_cam, ms_clt;

	time_t t_start;
	unsigned long n_bytes;	//statistics

	int sock_p2psvr;	//connection received notication
	char *dp;           //punch from one side
};

/* noti socket state machine */
struct noti_sock {
	int sock;
	int state;	//0-accepted; 1-handshaked
	struct event *_e;
};

/* In dcssvr:
 * 	1. Client request a relay session
 * 	2. Server checks the Ipcam is online
 * 	3. Generate a session ID, notify the ipcam and client with relay-server's address and port. 
 * 	   notify the relay-server with this session id.
 *
 * In relaysvr:
 * 	1. Allocate a RELAYSESSION structure
 * 	2. Wait for connection of the specific session ID from ipcam and client
 * 	3. Start a thread to relay data.
 *
 */

int max_sess = 30;
int relay_time = 300;	//
static LIST_HEAD(s_PendingSessionList);
static struct event_base *base0;

#define max(x,y) ((x)>(y)?(x):(y))
int media_conn_handler(int sk, struct event *e);
int noti_conn_handler(int sk);
static void parseOpts(int argc, char *argv[]);

void sig_handler(int sig)
{
	if(sig == SIGINT || sig == SIGTERM)
	{
		event_base_loopbreak(base0);
	}
}

struct event * event_new_selfptr(struct event_base *base, int fd, int what, event_callback_fn cb) 
{
	struct event *e = event_new(base, 0, 0, cb, NULL); 
	event_assign(e, base0, fd, what, cb, e);
	return e;
}

int noti_conn_handler(int sk)
{
	struct p2pcore_relay_notification drn;
	struct p2pcore_header dh;
	int i, len;
   
	len = PA_Recv(sk, &drn, sizeof(drn), 0);
	if(len != sizeof(drn) || !check_p2pcore_header(&drn.dh) || P2PCORE_OP(&drn.dh) != OP_RELAY_NOTIFY)
	{
		return -1;
	}

	//dbg_bin("sess noti: ", drn.sess_id, LENGTH_OF_SESSION_ID);
	RELAYSESSION *pRS = (RELAYSESSION*)calloc(sizeof(RELAYSESSION), 1);
	dbg_p2p("session %p allocated", pRS);
	pRS->t_start = time(NULL);
	memcpy(pRS->sess_id, drn.sess_id, LENGTH_OF_SESSION_ID);
	pRS->trans_id = drn.dh.trans_id;
	pRS->ms_cam.sock = pRS->ms_clt.sock = INVALID_SOCKET;
	pRS->ms_cam.e = pRS->ms_clt.e = NULL;
	pRS->sock_p2psvr = sk;

	INIT_LIST_HEAD(&pRS->list);
	list_add_tail(&pRS->list, &s_PendingSessionList);

	init_p2pcore_header(&dh, ST_RELAYER, OP_RELAY_NOTIFY, CLS_RESPONSE, 0, 0, 0);
	PA_Send(sk, &dh, sizeof(dh), 0);

	return 0;
}

int noti_conn_handshake(int sk)
{
	union { struct p2pcore_header dh; char buff[256]; };
	if(PA_Recv(sk, buff, sizeof(buff), 0) <= 0)
		return -1;
	return 0;
}

void cb_noti_data(PA_SOCKET fd, short what, void *arg)
{
	struct noti_sock *pns = (struct noti_sock*)arg;
	switch(pns->state)
	{
	case 0:
		if(what == EV_TIMEOUT || noti_conn_handshake(fd) != 0)
		{
			PA_SocketClose(fd);
			event_free(pns->_e);
			free(pns);
		}
		else
		{
			event_assign(pns->_e, base0, fd, EV_READ|EV_PERSIST, cb_noti_data, arg);
			event_add(pns->_e, NULL);
			pns->state = 1;
		}
		break;
	case 1:
		if(noti_conn_handler(fd) != 0)
		{
			PA_SocketClose(fd);
			event_del(pns->_e);
			event_free(pns->_e);
			free(pns);
		}
		break;
	}
}

void cb_media_data(PA_SOCKET fd, short what, void *arg)
{
	RELAYSESSION *pRS = (RELAYSESSION*)arg;
	int len;
	struct media_sock *src, *dst;

	//dbg_p2p("event: %s", what==EV_READ?"read":((what==EV_WRITE)?"write":"timeout"));
	if(__LIKELY(what == EV_READ))
	{
		if(fd == pRS->ms_cam.sock) {
			src = &pRS->ms_cam;
			dst = &pRS->ms_clt;
		}
		else {
			src = &pRS->ms_clt;
			dst = &pRS->ms_cam;
		}
		src->len = PA_Recv(fd, src->buff, RELAY_BUFF_SIZE, 0);
		if(src->len <= 0)
		{
			goto clean;
		}
		len = PA_Send(dst->sock, src->buff, src->len, 0);
		if(len < 0)
		{
			if(PA_SocketGetError() == EWOULDBLOCK)
			{
				struct timeval tv = { 6, 0 };
				event_del(src->e);
				event_del(dst->e);
				event_assign(dst->e, base0, dst->sock, EV_WRITE|EV_TIMEOUT, cb_media_data, arg);
				event_add(dst->e, &tv);
				dbg_p2p("sess %p would block", pRS);
			}
			else
				goto clean;
		}
		else
			src->len = 0;
	}
	else if(what == EV_WRITE)
	{
		if(fd == pRS->ms_cam.sock) {
			src = &pRS->ms_clt;
			dst = &pRS->ms_cam;
		}
		else {
			src = &pRS->ms_cam;
			dst = &pRS->ms_clt;
		}
		if(__LIKELY(PA_Send(dst->sock, src->buff, src->len, 0) > 0))
		{
			struct timeval tv = { 20, 0 };
			event_assign(src->e, base0, src->sock, EV_TIMEOUT|EV_READ|EV_PERSIST, cb_media_data, arg);
			event_add(src->e, &tv);
			event_assign(dst->e, base0, dst->sock, EV_TIMEOUT|EV_READ|EV_PERSIST, cb_media_data, arg);
			event_add(dst->e, &tv);
			src->len = 0;
			dbg_p2p("sess %p re-opened", pRS);
		}
		else
			goto clean;
	}
	else if(what == EV_TIMEOUT)
	{
		src = &pRS->ms_cam;
		dst = &pRS->ms_clt;
		goto clean;
	}
	return;

clean:
	event_del(src->e);
	event_del(dst->e);
	event_free(src->e);
	event_free(dst->e);
	PA_SocketClose(src->sock);
	PA_SocketClose(dst->sock);
	free(src->buff);
	free(dst->buff);
	free(arg);
	dbg_p2p("session %p cleaned", arg);
}

int media_punch_handler(int sk, struct event *e)
{
	union {
		struct p2pcore_session_init bsa;
		struct p2pcore_punch dp;
		char buff[320];
	};
	uint8_t *sess_id = NULL;
	int len;

	if( (len=PA_Recv(sk, &dp, sizeof(buff), 0)) < sizeof(struct p2pcore_header) || !check_p2pcore_header(&dp.dh))
	{
		return -1;
	}

	dbg_p2p("op = %d(%s), st=%s", 
			P2PCORE_OP(&dp.dh), P2PCORE_OP(&dp.dh)==OP_PUNCH?"punch":"begin session resp",
			dp.dh.st==ST_CALLER?"clt":"cam");
	//dbg_bin("session id: ", dp.sess_id, LENGTH_OF_SESSION_ID);
#if 0
	if( bsa.dh.st == ST_CALLEE && bsa.dh.op == OP_SD_SESSION_INIT && bsa.dh.cls == CLS_RESPONSE)
	{
		sess_id = bsa.sess_id;
	}
	else
#endif
	if( P2PCORE_OP(&dp.dh) == OP_PUNCH )
	{
		sess_id = dp.sess_id;
	}
	else
	{
		return -1;
	}


	struct list_head *p, *q;
	list_for_each_safe(p, q, &s_PendingSessionList)
	{
		RELAYSESSION *pRS = list_entry(p, RELAYSESSION, list);
		if(memcmp(sess_id, pRS->sess_id, LENGTH_OF_SESSION_ID) == 0)
		{
#if 0
			if(bsa.dh.op == OP_SD_SESSION_INIT)
			{
				dbg_msg("Relay server: ack of OP_SD_BEGIN_SESSION received.\n");
				if(PA_Send(pRS->sock_p2psvr, &bsa, len, 0) < 0)
					perror("PA_Send(sock_p2psvr...)");
				//PA_SocketClose(pRS->sock_p2psvr);
				//pRS->sock_p2psvr = -1;
				pRS->ms_cam.sock = sk;
			}
			else
#endif
			if(dp.dh.st == ST_CALLEE)
			{
				pRS->ms_cam.sock = sk;
				pRS->ms_cam.e = e;

				if(pRS->ms_clt.sock == INVALID_SOCKET)
				{
					dbg_p2p("caller not ready");
					pRS->dp = (char*)malloc(len);
					memcpy(pRS->dp, &dp, len);
				}
				else
				{
					dbg_p2p("relay punch to caller");
					PA_Send(pRS->ms_clt.sock, &dp, len, 0);
					if(pRS->dp) 
					{
						dbg_p2p("relay punch to camera");
						PA_Send(pRS->ms_cam.sock, pRS->dp, P2PCORE_PACKET_LEN((struct p2pcore_header*)pRS->dp), 0);
						free(pRS->dp);
						pRS->dp = NULL;
					}
				}
			}
			else if(dp.dh.st == ST_CALLER)
			{
				pRS->ms_clt.sock = sk;
				pRS->ms_clt.e = e;

				if(pRS->ms_cam.sock == INVALID_SOCKET)
				{
					dbg_p2p("callee not ready");
					pRS->dp = (char*)malloc(len);
					memcpy(pRS->dp, &dp, len);
				}
				else
				{
					dbg_p2p("relay punch to camera");
					PA_Send(pRS->ms_cam.sock, &dp, len, 0);
					if(pRS->dp) 
					{
						dbg_p2p("relay punch  to caller");
						PA_Send(pRS->ms_clt.sock, pRS->dp, P2PCORE_PACKET_LEN((struct p2pcore_header*)pRS->dp), 0);
						free(pRS->dp);
						pRS->dp = NULL;
					}
				}
			}

			if(pRS->ms_clt.sock != INVALID_SOCKET && pRS->ms_cam.sock != INVALID_SOCKET)
			{
#if 0
				if(pRS->sock_p2psvr != INVALID_SOCKET)
				{
					//PA_SocketClose(pRS->sock_p2psvr);
					//pRS->sock_p2psvr = INVALID_SOCKET;
				}
#endif
				list_del(p);

				dbg_p2p("session ready");
				struct timeval tv = { 20, 0 };
				pRS->ms_clt.buff = (char*)malloc(RELAY_BUFF_SIZE);
				pRS->ms_cam.buff = (char*)malloc(RELAY_BUFF_SIZE);
				event_assign(pRS->ms_clt.e, base0, pRS->ms_clt.sock, EV_TIMEOUT|EV_READ|EV_PERSIST, cb_media_data, pRS);
				event_assign(pRS->ms_cam.e, base0, pRS->ms_cam.sock, EV_TIMEOUT|EV_READ|EV_PERSIST, cb_media_data, pRS);
				event_add(pRS->ms_clt.e, NULL);
				event_add(pRS->ms_cam.e, NULL);
				pRS->ms_clt.len = pRS->ms_cam.len = 0;
			}
			return 0;
		}
	}

	return 0;
}

void cb_media_punch(PA_SOCKET fd, short what, void *arg)
{
	struct event *me = (struct event*)arg;
	if(what == EV_READ)
	{
		dbg_p2p("media connection punch");
		if(media_punch_handler(fd, me) < 0)
		{
			PA_SocketClose(fd);
			event_free(me);
		}
		else
		{
		}
	}
	else if(what == EV_TIMEOUT)
	{
		dbg_p2p("media connection timeouted");
		PA_SocketClose(fd);
		event_free(me);
	}
}

void cb_noti_accept(PA_SOCKET fd, short what, void *arg)
{
	struct sockaddr sa;
	socklen_t sa_len;

	sa_len = sizeof(sa);
	int sk = accept(fd, &sa, &sa_len);
	if(sk > 0)
	{
		dbg_p2p("accepted");
		struct timeval tv = { 1, 0 }; //刚开始连接上来应很快发通知，这样可以验证连接是否有效，之后可以保存长连接
		struct noti_sock *pns = (struct noti_sock*)malloc(sizeof(struct noti_sock));
		pns->state = 0;
		pns->sock = sk;
		//struct event *ev = event_new_selfptr(base0, sk, EV_TIMEOUT|EV_READ, cb_noti_handshake);
		struct event *ev = event_new(base0, sk, EV_TIMEOUT|EV_READ, cb_noti_data, (void*)pns);
		pns->_e = ev;
		event_add(ev, &tv);
	}
}

void cb_media_accept(PA_SOCKET fd, short what, void *arg)
{
	struct sockaddr sa;
	socklen_t sa_len;

	sa_len = sizeof(sa);
	int sk = accept(fd, &sa, &sa_len);
	if(sk > 0)
	{
		dbg_p2p("accepted");
		struct timeval tv = { 2, 0 };
		struct event *ev = event_new_selfptr(base0, sk, EV_TIMEOUT|EV_READ, cb_media_punch);
		event_add(ev, &tv);
	}
}

void cb_check_pending_sess(PA_SOCKET fd, short what, void *arg)
{
	struct list_head *p, *q;
	time_t now = time(NULL);
	list_for_each_safe(p, q, &s_PendingSessionList)
	{
		RELAYSESSION *prs = list_entry(p, RELAYSESSION, list);
		if(now - prs->t_start > 10)
		{
			list_del(p);
			free(prs);
		}
	}
}

#if 0
if(check_p2pcore_header(pdh))
{
	if(pdch->op == CMD_START_VIDEO)
	{
		struct { struct p2pcore_command_header dch; struct p2pcore_video_param vp; } vp;
		//Only request sub-stream
		pdch->param.param_b[1] = 1;

		//set the sub-stream to CIF/200Kbps
		memcpy(&vp.dch, pdch, sizeof(struct p2pcore_command_header));
		vp.dch.dh.length = htonl(sizeof(vp) - sizeof(vp.dch.dh));
		vp.dch.param.cmd = CMD_SET_VIDEO_PARAMETER;
		vp.vp.brmode = 0;
		vp.vp.quality = 3;
		vp.vp.res = 1;	//cif
		vp.vp.fps = 5;
		vp.vp.kbps = 150;
		vp.vp.gop = 100;
		vp.vp.save = 0;
		vp.vp.reserved = 0;
		PA_Send(pRS->ms_cam.sock, &vp, sizeof(vp), 0);
		timed_recv(pRS->ms_cam.sock, &vp, sizeof(vp), 1000);
	}
	else if(pdch->op == CMD_SET_VIDEO_PARAMETER)
	{
		struct p2pcore_video_param *pdvp = (struct p2pcore_video_param*)(pdch+1);
		pdch->param.param_b[1] = 1;
		pdvp->res = 1;
		if(pdvp->kbps > 200) pdvp->kbps = 200;
		if(pdvp->fps > 6) pdvp->fps = 6;
	}
}
#endif

void printUsage()
{
	printf("relayd -?\t\t--\tthis message\n");
	printf("relayd [-n max_session_count][-t seconds_to_server]\n");
}
void parseOpts(int argc, char *argv[])
{
	int c;
	while((c = getopt(argc, argv, "?n:t:")) != -1)
	{
		switch(c)
		{
		case '?':
		default:
			printUsage();
			exit(0);
			break;
		case 'n':
			max_sess = atoi(optarg);
			break;
		case 't':
			relay_time = atoi(optarg);
			break;
		}
	}

	if(max_sess == 0 || relay_time == 0)
	{
		printUsage();
		exit(0);
	}
}

int main(int argc, char *argv[])
{
	int sock_n;	//Accept notifications from p2p server
	int sock_m;	//Accept media connection from cameras and clients
	//struct event *ev;
	struct event evt_noti, evt_media, evt_chk_pending_sess;

	parseOpts(argc, argv);

#ifndef _DEBUG
	if(fork() != 0) return 0;
	setsid();
	chdir("/");
	umask(0);
	if(fork() != 0) return 0;
#endif

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGPIPE, SIG_IGN);

	base0 = event_base_new();

	sock_n = CreateServiceSocket(SOCK_STREAM, 0, RELAYSERVER_NOTIFY_PORT);
	sock_m = CreateServiceSocket(SOCK_STREAM, 0, RELAYSERVER_MEDIA_PORT);
	if(sock_n == INVALID_SOCKET || sock_m == INVALID_SOCKET)
	{
		fprintf(stderr, "relayd: create service sockets falied");
		return -1;
	}
	evutil_make_socket_nonblocking(sock_n);
	event_assign(&evt_noti, base0, sock_n, EV_READ|EV_PERSIST, cb_noti_accept, (void*)1);
	//ev = event_new(base0, sock_n, EV_READ|EV_PERSIST, cb_noti_accept, (void*)1);
	event_add(&evt_noti, NULL);

	evutil_make_socket_nonblocking(sock_m);
	event_assign(&evt_media, base0, sock_m, EV_READ|EV_PERSIST, cb_media_accept, (void*)2);
	//ev = event_new(base0, sock_m, EV_READ|EV_PERSIST, cb_media_accept, (void*)2);
	event_add(&evt_media, NULL);

	struct timeval tv = { 10, 0 };
	event_assign(&evt_chk_pending_sess, base0, 0, EV_TIMEOUT|EV_PERSIST, cb_check_pending_sess, NULL);
	event_add(&evt_chk_pending_sess, &tv);


	event_base_dispatch(base0);

	PA_SocketClose(sock_n);
	PA_SocketClose(sock_m);

	struct list_head *p, *q;
	list_for_each_safe(p, q, &s_PendingSessionList)
	{
		RELAYSESSION *prs = list_entry(p, RELAYSESSION, list);
		list_del(p);
		free(prs);
	}

	printf("relayd terminated.\n");
	return 0;
}

