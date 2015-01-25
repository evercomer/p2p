#include <stdio.h>
#include <stdlib.h>

#include "p2pbase.h"
#include "netbase.h"
#ifdef WIN32
#include <iphlpapi.h>
#pragma warning( disable : 4200 )
#pragma comment(lib, "iphlpapi.lib")
#endif

//--------------------------------------------------------------------------------------
void init_p2pcore_header(struct p2pcore_header* p, int st, int op, int cls, int status, int len, p2pcore_transid_t trans_id)
{
	p->tag = P2PCORE_TAG | P2PCORE_VERSION;
	P2PCORE_SET_STATUS(p, status);
	P2PCORE_SET_OP(p, op);
	P2PCORE_SET_DATA_LEN(p, len);
	p->cls = cls;
	p->st = st;
	P2PCORE_SET_TID(p, trans_id);
}
//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------

//Return number of valid local IPs(exclude loopback)
//	< 0 means something wrong
int get_local_ips(uint32_t* pIps, unsigned int size)
{
	int i, n;
	ETHERNIC eths[16];

	n = GetIpAddresses(eths, 16);
	for(i=0; i<n&&i<size; i++)
		pIps[i] = eths[i].addr.s_addr;
	return i;
}
//------------------------------------------------------------------------------------

//Return:
//	>0, length of received response
//	<0, nagatived error code
//Remark:
//	The size of the buffer must be big enough
int send_recv_over_udp(int sock, const struct sockaddr* addr, const struct p2pcore_header* p, void* buf, int size)
{
	int i;
	struct p2pcore_header *pdh = (struct p2pcore_header*)buf;
	for(i=0; i<3; i++)
	{
		struct sockaddr sa;
		int sa_len, len;

		sa_len = sizeof(sa);
		PA_SendTo(sock, p, P2PCORE_PACKET_LEN(p), 0, addr, sizeof(struct sockaddr));
		len = timed_recv_from(sock, buf, size, &sa, (unsigned int*)&sa_len, 1000);
		if(len <= 0) { continue; }

		if( check_p2pcore_header(pdh) && p->op == pdh->op && len == P2PCORE_PACKET_LEN(pdh) )
		{
			return len;
		}
		else
			return P2PE_ERROR_RESPONSE;
	}

	return P2PE_NO_RESPONSE;
}

//--------------------------------------------------------------------------------------

//Parameter:
//	local_port -- the binding port used to query, can be 0
//Return: 
//	success -- the socket created to query address
//	failed  -- -1
int query_external_address(const struct sockaddr* svr, struct p2pcore_addr* pda, unsigned short local_port)
{
	struct p2pcore_header dh;
	struct p2pcore_query_address_response dq;

	int sock = CreateServiceSocket(SOCK_DGRAM, 0, local_port);
	if(sock <= 0) return -1;

	init_p2pcore_header(&dh, ST_CALLER, OP_QUERY_ADDRESS, CLS_REQUEST, 0, 0, 0);
	memset(&dq, 0, 10);
	if(send_recv_over_udp(sock, svr, &dh, &dq, sizeof(dq)) == sizeof(dq))
	{
		if(check_p2pcore_header(&dq.dh) && dq.dh.op == OP_QUERY_ADDRESS && dq.dh.st == ST_SERVER && dq.dh.status == 0)
		{
			pda->ip = dq.ext_addr.ip;
			pda->port = dq.ext_addr.port;
			return sock;
		}
		else
		{
			CloseSocket(sock);
			return -1;
		}
	}
	return -1;
}


//--------------------------------------------------------------------------------------
#if 0
int p2pcore_get_challenge(int sk, int st, uint8_t sn_user[16], uint8_t rval[LENGTH_OF_CHALLENGE])
{
	struct p2pcore_challenge dc;
	int r;

	init_p2pcore_header(&dc.dh, st, OP_GET_CHALLENGE, 0, 0, LENGTH_OF_AUTH, 0);
	memset(dc.sn_user, 0, 16);
	if(st == ST_CALLER) strncpy((char*)dc.sn_user, (const char*)sn_user, 16);
	else memcpy(dc.sn_user, sn_user, 16);

	r = PA_Send(sk, &dc, sizeof(dc), 0);
	if(r < 0)
	{
		return -1;
	}
	r = timed_recv(sk, &dc, sizeof(dc), 3000);
	if(r < 0) return P2PE_TIMEOUTED;
	if(r < sizeof(struct p2pcore_header))
		return P2PE_ERROR_RESPONSE;

	if(dc.dh.status == 0)
	{
		if(r != sizeof(dc))
			return P2PE_ERROR_RESPONSE;
		memcpy(rval, dc.challenge, LENGTH_OF_CHALLENGE);
		return 0;
	}

	return dc.dh.status;
}
#endif


//功能:
//	开始TCP打洞过程，直到超时或建立了有效连接
//	调用cb以确认连接的有效性
//
//返回：
//	0: *sock is the socket connected and verified
//	none zero: error code
//
//说明:
//	连接建立时，以 CONNECTED(连接为对方接受) 或 ACCEPTED(接受对方连接) 为 status 参数调用cb。这两个参数没有本质区别。
//	cb返回OK，则连接得到确认，函数返回该连接；返回CONTINUE，则继续确认过程；返回其他值该连接并关闭。
//	当连双方需要交换一些信息确认有效性时，cb向对端发送一些数据并返回 CHECKCONNECTION_CONTINUE。当对端应答到来时，
//	用status=SOCK_STATUS_READABLE再次调用cb
//
int p2pcore_tcp_punch(uint16_t local_port, int mapped_sock, struct conn_bit_fields bits, const struct p2pcore_addr *peer_addr, 
		/*OUT*/int *sock, CHECKCONNECTIONCB cb, void* data)
{
	struct sockaddr_in sai;
	socklen_t sa_len;
	int i, try_cnt,  n_addr;
	int sk0, out_sk[10], out_refused_cnt[10];
	fd_set rfds, wfds, efds;
	time_t t0;
	int in_sk[10], n_in_sk = 0;
	int sock_conn = INVALID_SOCKET;
	int rlt = P2PE_TIMEOUTED;

	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;

	n_addr = bits.n_addr;
	if(n_addr > 10) n_addr = 10;

	// 
	// Create n_addr sockes for outgoing connection(put the upnp target at last) 
	// and one socket to listen.
	// In linux, socket for listening must be create at last.
	//
	//应先访问客户端的外网地址。否则当访问客户端的内网地址时，
	//有可能要通过路由，从而在NAT上建立端口映射。当本端NAT为
	//Symmetric时，访问外网地址时端口预测失败。
	for(i=n_addr-1; i>=0; i--)
	{
		out_sk[i] = NewSocketAndBind(SOCK_STREAM, 0, local_port);
		out_refused_cnt[i] = 0;
		setblk(out_sk[i], 0);

		sai.sin_port = peer_addr[i].port;
		sai.sin_addr.s_addr = peer_addr[i].ip;

		connect(out_sk[i], (struct sockaddr*)&sai, sizeof(sai));
		//LOG("conntect %d to: %s:%d\n", out_sk[i], inet_ntoa(sai.sin_addr), ntohs(sai.sin_port));
	}
	sk0 = CreateServiceSocket(SOCK_STREAM, 0, local_port);
	//sk0 = NewSocketAndBind(SOCK_STREAM, 0, local_port);
	//LOG("socket %d bind to local port %d to listen.\n", sk0, local_port);


	//
	// initialize fd_set
	//
	FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);
	FD_SET(sk0, &rfds); FD_SET(sk0, &efds);
	if(mapped_sock > 0) FD_SET(mapped_sock, &rfds);
	for(i=0; i < n_addr; i++)
	{
		if(out_sk[i] > 0) 
		{
			FD_SET(out_sk[i], &wfds); 
			FD_SET(out_sk[i], &efds);
		}
	}


	//
	// try 10 times(seconds)
	//
	time(NULL);
	for(t0 = time(NULL), try_cnt=0; time(NULL) - t0 < 10/* && try_cnt < 20*/; try_cnt++)
	{
		fd_set t_rfds, t_wfds, t_efds;
		struct timeval tv;
		int sel, sk_max;
		int cb_rlt;

		//LOG("tcp punching, circle %d\n", try_cnt);
		// re-calculate the max sock
		sk_max = sk0>mapped_sock?sk0:mapped_sock;
		for(i=0; i<n_addr; i++) if(sk_max < out_sk[i]) sk_max = out_sk[i];
		for(i=0; i<n_in_sk; i++) if(sk_max < in_sk[i]) sk_max = in_sk[i];

		tv.tv_sec = 1;
		tv.tv_usec = 0;
		t_rfds = rfds; t_wfds = wfds; t_efds = efds;


		sel = select(sk_max+1, &t_rfds, &t_wfds, &t_efds, &tv);
		if(sel == 0) continue;
		if(sel < 0)
		{
#ifndef WIN32
			if(errno == EINTR) continue; 
			perror("select");
#endif
			break; 
		}

		for(i=0; i<2; i++)
		{
			int sl; //socket listening
			sl = i==0?sk0:mapped_sock;
			if(sl > 0 && FD_ISSET(sl, &t_rfds))
			{
				int s;
				sa_len = sizeof(sai);
				s = accept(sl, (struct sockaddr*)&sai, &sa_len);
				if(s > 0)
				{
					cb_rlt = cb(s, SOCK_STATUS_ACCEPTED, data);
					//LOG("accepted %d from %s:%d cb: %d\n", s, inet_ntoa(sai.sin_addr), ntohs(sai.sin_port), cb_rlt);
					switch(cb_rlt)
					{
						case CHECKCONNECTION_CONTINUE:
							FD_SET(s, &rfds);
							in_sk[n_in_sk++] = s;
							break;
						case CHECKCONNECTION_OK:
							sock_conn = s;
							goto out;
							break;
						default:
							CloseSocket(s);
					}
				}
			}
		}

		for(i=0; i < n_addr; i++)
		{
			int optlen, err = 0;
			if(out_sk[i] == INVALID_SOCKET) continue;

			if(FD_ISSET(out_sk[i], &t_wfds))
			{
				optlen = sizeof(err);
				if(PA_GetSockOpt(out_sk[i], SOL_SOCKET, SO_ERROR, &err, &optlen) < 0)
				{
					//LOG("getsockopt");
					//should not happen
					continue;
				}

				if(err == 0)
				{
					cb_rlt = cb(out_sk[i], SOCK_STATUS_CONNECTED, data);
					//LOG("%d connected cb: %d\n", out_sk[i], cb_rlt);
					switch(cb_rlt)
					{
					case CHECKCONNECTION_CONTINUE:
						FD_CLR(out_sk[i], &wfds);
						FD_SET(out_sk[i], &rfds);
						break;
					case CHECKCONNECTION_OK:
						sock_conn = out_sk[i];
						out_sk[i] = INVALID_SOCKET;
						goto out;
						break;
					default:
						err = -1; //close it at following
						break;
					}
					if(!err) continue;
				}
			}
			else if(FD_ISSET(out_sk[i], &t_efds))
			{
				optlen = sizeof(err);
				PA_GetSockOpt(out_sk[i], SOL_SOCKET, SO_ERROR, &err, &optlen);
			}
			else if(FD_ISSET(out_sk[i], &t_rfds))
			{
				cb_rlt = cb(out_sk[i], SOCK_STATUS_READABLE, data);
				//LOG("%d connected readable cb: %d\n", out_sk[i], cb_rlt);
				switch(cb_rlt)
				{
				case CHECKCONNECTION_CONTINUE:
					break;
				case CHECKCONNECTION_OK:
					sock_conn = out_sk[i];
					out_sk[i] = INVALID_SOCKET;
					goto out;
					break;
				default:
					CloseSocket(out_sk[i]);
					FD_CLR(out_sk[i], &rfds);
					FD_CLR(out_sk[i], &efds);
					out_sk[i] = INVALID_SOCKET;
					if(cb_rlt == -P2PS_AUTH_FAILED)
					{
						rlt = P2PS_AUTH_FAILED;
						goto out;
					}
					break;
				}
			}

			if(err)
			{
				//LOG("sock %d error(%d): %s\n", out_sk[i], err, strerror(err));
				if(err == ECONNREFUSED) 
				{
					out_refused_cnt[i]++;
					//if(out_refused_cnt[i] <= 2)
					if(1)	//(Linux)client might not be in listening
					{
						sai.sin_port = peer_addr[i].port;
						sai.sin_addr.s_addr = peer_addr[i].ip;

						//LOG("reconnect %d to %s:%d\n", out_sk[i], inet_ntoa(sai.sin_addr), ntohs(sai.sin_port));
						if(connect(out_sk[i], (struct sockaddr*)&sai, sizeof(sai)) < 0)
						{
							err = PA_SocketGetError();
							if(err == ECONNABORTED)
								if(connect(out_sk[i], (struct sockaddr*)&sai, sizeof(sai)) < 0)
									err = PA_SocketGetError();
							if(err != EINPROGRESS)
							{
								//LOG("reconnect err %d: %s\n", err, strerror(err));
							}
						}
					}
					else
					{
						PA_SocketClose(out_sk[i]);
						FD_CLR(out_sk[i], &wfds);
						FD_CLR(out_sk[i], &efds);
						out_sk[i] = INVALID_SOCKET;
					}
				}
				else if(err != EINPROGRESS)
				{
					CloseSocket(out_sk[i]);
					FD_CLR(out_sk[i], &wfds);
					FD_CLR(out_sk[i], &efds);
					out_sk[i] = INVALID_SOCKET;
				}
			}
		}

		for(i=0; i<n_in_sk; i++)
		{
			if(in_sk[i] != INVALID_SOCKET && FD_ISSET(in_sk[i], &t_rfds))
			{
				cb_rlt = cb(in_sk[i], SOCK_STATUS_READABLE, data);
				//LOG("accepted readable cb: %d\n", cb_rlt);
				switch(cb_rlt)
				{
				case CHECKCONNECTION_CONTINUE:
					break;
				case CHECKCONNECTION_OK:
					sock_conn = in_sk[i];
					in_sk[i] = INVALID_SOCKET;
					goto out;
					break;
				default:
					FD_CLR(in_sk[i], &rfds);
					CloseSocket(in_sk[i]);	
					in_sk[i] = INVALID_SOCKET;
					if(cb_rlt == -P2PS_AUTH_FAILED)
					{
						rlt = P2PS_AUTH_FAILED;
						goto out;
					}
					break;
				}
			}
		}

		if(sock_conn != INVALID_SOCKET) break;
	}

out:
	CloseSocket(sk0);
	for(i=0; i<n_addr; i++) if(out_sk[i] != INVALID_SOCKET) CloseSocket(out_sk[i]);
	for(i=0; i<n_in_sk; i++) if(in_sk[i] != INVALID_SOCKET) CloseSocket(in_sk[i]);
	if(sock_conn != INVALID_SOCKET) 
	{ 
		setblk(sock_conn, 1); 
		*sock = sock_conn;
		
		sa_len = sizeof(sai);
		getsockname(sock_conn, (struct sockaddr*)&sai, &sa_len);
		//LOG("tcp punch connected: %s:%d <-> ", inet_ntoa(sai.sin_addr), ntohs(sai.sin_port));
		sa_len = sizeof(sai);
		getpeername(sock_conn, (struct sockaddr*)&sai, &sa_len);
	    //LOG("%s:%d\n", inet_ntoa(sai.sin_addr), ntohs(sai.sin_port));
		
		return 0; 
	}
	return rlt;
}


NatType simple_stun_type(DWORD ip, int *preserve_port, int *hairpin, unsigned short *delta)
{
	int sock1;
	unsigned short port1;
	struct sockaddr_in sai;
	socklen_t sa_len;
	struct p2pcore_query_address_response dqr1, dqr2;
	struct p2pcore_header dh;
	NatType type;
       
	type = StunTypeBlocked;
	*preserve_port = 0;
	*delta = 0;
	*hairpin = 0;
	init_p2pcore_header(&dh, ST_CALLEE, OP_QUERY_ADDRESS, CLS_REQUEST, 0, 0, 0);

	sock1 = NewSocketAndBind(SOCK_DGRAM, 0, 0);
	sa_len = sizeof(sai);
	getsockname(sock1, (struct sockaddr*)&sai, &sa_len);
	port1 = ntohs(sai.sin_port);


	//Send to S1:P1
	sai.sin_addr.s_addr = ip;
	sai.sin_port = ntohs(STUN_SVR_PORT);
	if(send_recv_over_udp(sock1, (const struct sockaddr*)&sai, &dh, &dqr1, sizeof(dqr1)) == sizeof(dqr1))
	{
		int i;
		uint32_t ips[10];
		int n_local;
		
		n_local = get_local_ips(ips, 10);
		type = StunTypePortDependedFilter;
		for(i=0; i<n_local; i++)
		{
			if(ips[i] == dqr1.ext_addr.ip)
			{
				PA_SocketClose(sock1);
				return StunTypeOpen;
			}
		}

		*preserve_port = (port1 == ntohs(dqr1.ext_addr.port))?1:0;


		//Send to S2:P2
		memset(&sai, 0, sizeof(sai));
		sai.sin_family = AF_INET;
		sai.sin_addr.s_addr = dqr1.server2.ip;
		sai.sin_port = dqr1.server2.port;
		if(send_recv_over_udp(sock1, (const struct sockaddr*)&sai, &dh, &dqr2, sizeof(dqr2)) == sizeof(dqr2))
		{
			//Depended mapping
			//Check delta of external ports
			if(dqr2.ext_addr.port != dqr1.ext_addr.port)
			{
				type = StunTypeDependentMapping;
				*delta = ntohs(dqr2.ext_addr.port) - ntohs(dqr1.ext_addr.port);
			}
		}
	}

	if(sock1 > 0) PA_SocketClose(sock1);
	return type;
}

// *  The same local port connect to different peer
NatType simple_stunt_type(DWORD ip, int *preserve_port, int *hairpin, unsigned short *delta)
{
	int sock1;
	unsigned short port1;
	struct sockaddr_in sai;
	socklen_t sa_len;
	struct p2pcore_query_address_response dqr1, dqr2;
	NatType type;
	int opt;
       
	type = StunTypeBlocked;
	*preserve_port = 0;
	*delta = 0;
	*hairpin = 0;

	sock1 = NewSocketAndBind(SOCK_STREAM, 0, 0);
	opt = 1;
	setsockopt(sock1, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(int));
	sa_len = sizeof(sai);
	getsockname(sock1, (struct sockaddr*)&sai, &sa_len);
	port1 = ntohs(sai.sin_port);


	//connect S1:P1
	sai.sin_addr.s_addr = ip;
	sai.sin_port = ntohs(STUN_SVR_PORT);
	if(connect(sock1, (const struct sockaddr*)&sai, sizeof(struct sockaddr)) == 0 &&
			recv(sock1, (char*)&dqr1, sizeof(dqr1), 0) == sizeof(dqr1) )
	{
		int i;
		uint32_t ips[10];
		int n_local;
		
		n_local = get_local_ips(ips, 10);
		type = StunTypePortDependedFilter;
		for(i=0; i<n_local; i++)
		{
			if(ips[i] == dqr1.ext_addr.ip)
			{
				PA_SocketClose(sock1);
				return StunTypeOpen;
			}
		}

		*preserve_port = (port1 == ntohs(dqr1.ext_addr.port))?1:0;

		PA_SocketClose(sock1);
		sock1 = NewSocketAndBind(SOCK_STREAM, 0, port1);
		setsockopt(sock1, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(int));

		//Connect S2:P2
		memset(&sai, 0, sizeof(sai));
		sai.sin_family = AF_INET;
		sai.sin_addr.s_addr = dqr1.server2.ip;
		sai.sin_port = dqr1.server2.port;
		if(connect(sock1, (const struct sockaddr*)&sai, sizeof(sai)) == 0 &&
				recv(sock1, (char*)&dqr2, sizeof(dqr2), 0) == sizeof(dqr2))
		{
			//Depended mapping
			//Check delta of external ports
			if(dqr2.ext_addr.port != dqr1.ext_addr.port)
			{
				type = StunTypeDependentMapping;
				*delta = ntohs(dqr2.ext_addr.port) - ntohs(dqr1.ext_addr.port);
			}
		}
	}

	if(sock1 > 0) PA_SocketClose(sock1);
	return type;
}

#ifdef TEST_NAT_TYPE

int main(int argc, char *argv[])
{
	//NatType simple_stun_type(DWORD ip, int *preserve_port, int *hairpin, unsigned short *delta)
	int preserve_port, hairpin;
	unsigned short delta;
	if(argc == 1)
	{
		printf("%s stun_server\n", argv[0]);
		return 0;
	}
	printf("stun type = %d\n", simple_stun_type(inet_addr(argv[1]), &preserve_port, &hairpin, &delta));
	printf("preserve_port = %d, hairpin = %d, delta = %d\n", preserve_port, hairpin, delta);
	return 0;
}

#endif


