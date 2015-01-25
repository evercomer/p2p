#ifndef __p2pcore_imp_h__
#define __p2pcore_imp_h__

#include "platform_adpt.h"
#include "linux_list.h"

typedef enum {
	OSS_CONNECT = 1,
	OSS_CONNECTED
} OUTSOCKSTATE;

//temporary data used when punching
struct punch_tmp {
	char			*auth_str;
	int			auth_len;

	struct p2pcore_addr 	addrs[8];	//peer's candidate-addresses
	uint16_t          local_port;	//locally bound port
	int             sock2;      //udp socket 2, for symmetric nat, used for punching, if connected 
                                //on this socket, assigned it to sock

	RUDPSOCKET      out_sock;	//connect to peer's listening socket
	OUTSOCKSTATE    oss;        //state for out_sock

	int 			n_try;	//counter for sending of punching or initial request

	//for tcp
//tcp punching(to peer or relayer) stage
#define TPS_CONNECT     1
#define TPS_CONNECTED   2
#define TPS_CONN_FAILED 3
	struct sockaddr	p2p_serv;
	int			tps_state;
	int			bsa_len;
	union {
		char            bsa[200];               //callee: ack sent to server for session_noti when state = PSS_PUNCHING
		struct p2pcore_session_init sess_req;   //caller: session_init request when state = PSS_WAIT_FOR_SERVER_ACK
	};
};

#define HP2PCONN_TAG	0x53435054
typedef struct P2PCONN {
	uint32_t        dwTag;	
	struct list_head list;

	int             err;

	uint8_t         sess_id[LENGTH_OF_SESSION_ID];
	struct conn_bit_fields	bits;
	struct punch_tmp	*tmp;
	int             is_caller;

	struct sockaddr_in	peer_addr;
	int             state;

	int             mode;   //push or pull. P2PCONN_MODE_xxx
	int 			sock;	//udp socket, or connected tcp socket
	RUDPSOCKET		rudp_sock;


	unsigned int    last_access;    //real-time, counted in milliseconds
	int             timeout_val;    //in milliseconds
	int             is_hb_sent;     //hearbeat is sent

	//----------------------------------
	BYTE            *rbuff;         //user's recv buff
	int             rbuff_size, rbuff_off;
	//-----------------------------------
	
	//
	void            *pUserData;


	struct P2PCONN  *sibling;
	int             sb_size;         //size of send buffer
	int             sb_data_len;     //data length in snd buffer
	int             sb_chno;         //rudp chno for buffered data
	char            *sbuff;          //send buffer for sibling session

} P2PCONN;


#endif

