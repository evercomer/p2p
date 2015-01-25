#ifndef __p2pcore_ex_h__
#define __p2pcore_ex_h__

#include "p2pbase.h"

/**
 *  摄像机或客户端启动时，先向 kernel 服务器查询 stun 服务和 p2p 服务器地址
 *
 *  两种地址的查询使用一个命令来完成
 *
 *  请求和应答使用UDP通信，客户端可以一次查询多台设备的p2p地址，前提是请求和应答
 *  都可以封闭装一个UDP包里发送
 */

#define OP_QUERY_SERVER		17
#define QSF_STUN_SERVER		0x0001	//return stun server
struct p2pcore_query_server_req {
	struct p2pcore_header dh;
	uint8_t flags;		//qsf_xxx
	uint8_t n_q;		//<= 12
	uint8_t reserved[2];
	char   sn[0][LENGTH_OF_SN];
};

#define LENGTH_OF_SERVER	48
struct sn_server_pair {
	char sn[LENGTH_OF_SN];
	char server[LENGTH_OF_SERVER];	//"xxx.xxx.xxx:port"
};

struct p2pcore_query_server_resp {
	struct p2pcore_header dh;
	uint8_t n_stun_svr, n_resp;
	uint8_t reserved[2];
	/*
	char   stun_sverver[n_stun_svr][LENGTH_OF_SERVER];
	struct sn_server_pair[n_resp];
	*/
};

#endif

