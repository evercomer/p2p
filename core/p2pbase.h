/**
 * 	\brief p2pcore.h
 * 	\author SongZC
 *
 * sident 
 * 	Client gets a security string [sident] after authenticated on a Login-Server,
 *  this [sident] is used when request a new session to p2p-server.
 *
 *  This logic is reserved for security uses.
 *
 */
#ifndef __p2pbase_h__
#define __p2pbase_h__

#include "platform_adpt.h"
#include "p2pconst.h"

#ifdef WIN32
#pragma warning(disable: 4200)
#endif

#ifdef __cplusplus
extern "C" {
#endif

//for open clients
#define P2PCORE_SERVICE_PORT		8126
#define P2PCORE_SERVICE_PORT2		8127	//for TCP nat traversal
#define STUN_SVR_PORT		3478

#ifndef __BIG_ENDIAN__
#define P2PCORE_TAG		0xD0EAC10D	//0x0dc1ead0, highest bit is 0 to distinguish with rudp packet
#define P2PCORE_VERSION	0x01000000
#else
#define P2PCORE_TAG		0x0DC1EAD0	//
#define P2PCORE_VERSION	0x00000001
#endif

//
// Sender Type
//
#define ST_CALLEE   0
#define ST_CALLER   1
#define ST_SERVER   2

// Class of command packet
#define CLS_REQUEST     0
#define CLS_RESPONSE    1

//
// Internal Operations
//
/* basic udp operations */
#define OP_NONE             0
#define OP_SEARCH           1	//broadcast by client
#define OP_QUERY_ADDRESS    2	//UDP: C->S, D->S
#define OP_CS_SESSION_INIT  3
#define OP_SD_SESSION_NOTIFY    4
#define OP_SC_SESSION_BEGIN	5
#define OP_PUNCH            6
#define OP_HEART_BEAT       7	//UDP: C->D
#define OP_DS_IAMHERE       8   //UDP
#define OP_DECLARE          10	//p2p server send to relayer to decalre itself

//
// Header of package. 16 bytes
//
//
struct p2pcore_header {
	uint32_t tag;	/* 同步标志, 最高位为0以与rudp包区别 */
	uint32_t trans_id; //request sent in udp, or want a response, should have a unique trans_id
#ifndef __BIG_ENDIAN__
	uint8_t op:5;
	uint8_t cls:1;
	uint8_t st:2; 	//type of sender. necessary when punching
#else
	uint8_t st:2; 	//type of sender. necessary when punching
	uint8_t cls:1;
	uint8_t op:5;
#endif
	uint8_t  status;
	uint16_t length;
};

typedef uint32_t p2pcore_transid_t;
typedef uint16_t p2pcore_datalen_t;

#define P2PCORE_STATUS(ph) ((ph)->status)
#define P2PCORE_SET_STATUS(ph, status) (ph)->status = (status)
#define P2PCORE_OP(ph) ((ph)->op)
#define P2PCORE_SET_OP(ph, _op) (ph)->op = (_op)
#define P2PCORE_TID(ph) ntohl((ph)->trans_id)
#define P2PCORE_SET_TID(ph, _trans_id) ((ph)->trans_id = htonl(_trans_id))
#define P2PCORE_DATA_LEN(ph) ntohs((ph)->length)
#define P2PCORE_SET_DATA_LEN(ph, len) (ph)->length = htons(len)
#define P2PCORE_PACKET_LEN(ph) (sizeof(struct p2pcore_header) + ntohs((ph)->length))
//---------------------------------------------

#define LENGTH_OF_CHALLENGE	16
#define LENGTH_OF_SESSION_ID	16
#define LENGTH_OF_AUTH		16
#define LENGTH_OF_SN		32
#define LENGTH_OF_SIDENT		64

struct p2pcore_addr {
	uint32_t 	ip;	//network byte-order
	uint16_t  port;
	uint16_t  flags; //1 - Upnp nat maaped
};

//query address response
struct p2pcore_query_address_response {
	struct p2pcore_header dh;
	struct p2pcore_addr	ext_addr;
	struct p2pcore_addr server2;
};

struct p2pcore_addr_pair {
	struct p2pcore_addr local;
	struct p2pcore_addr external;
};

struct nat_info {
	uint8_t delta;
	uint8_t upnp:1;
	uint8_t hairpin:1;
	uint8_t preserve_port:1;
	uint8_t nat:3;
} __PACKED__;
//D->S to report itself(UDP)
//response is a p2pcore_header or a p2pcore_header with new server
struct p2pcore_i_am_here {
	struct p2pcore_header dh;
	char  sn[LENGTH_OF_SN];

	uint32_t version;
	//stun information
	struct nat_info stun;
	uint8_t n_client;   //number of connections
	uint8_t reserved2;
} __PACKED__;

//Information returned by server when heart-beat,
//depended on the status
struct p2pcore_i_am_here_ack {
	struct p2pcore_header dh;
	struct p2pcore_addr ext_addr;
	char data[0];	//DCSS_ADDRESS_CHANGED
};


//C->D, D->C
struct p2pcore_punch {
	struct p2pcore_header dh;
	char  sn[LENGTH_OF_SN];
	uint8_t  sess_id[LENGTH_OF_SESSION_ID];
	char  auth_str[0];	///< zero terminated authentication string, 
				///< provided by caller, verified by callee in a callback
};

//struct p2pcore_heart_beat 
#define p2pcore_heart_beat p2pcore_header


//连接信息
struct conn_bit_fields {
	//common bit fields
	uint32_t reserved:11;
	uint32_t sibling_sess:1;  //when set
	uint32_t auth:1;		//will not be used
		
	//Candidate addresses
	uint32_t n_addr:4;
		
	//NAT info
	uint32_t delta:6;
	uint32_t hairpin:1;
	uint32_t preserve_port:1;
	uint32_t nat:3;
		
	//Connection method. 
	uint32_t tcp:1;	//1
	uint32_t ct:3;	//connection type: 0-auto; 1-local; 2-p2p; 3-relay; 4-as proxy
};

/**
  1) Caller send <p2pcore_session_init>(with op=OP_CS_SESSION_INIT, dh.trans_id set to a unique value for each request, and,
  bits.ct to CONNTYPE_P2P) to server.
  If caller does not receive the ack in a short time(say, 300ms), it re-send request with trans_id unchanged.
  
  
  Server checsk the callee's state and the NATs type of both sides. 
  2) If the connection cannot be created, it send back a p2pore_header with error status indication.

  3) When the connection is possible, 

     - case A) RELAY
	    If the connection should be created with relay, it send back a <p2pcore_session_init> immediately, 
     	with 
	 	    bits.ct=CONNTYPE_RELAY
	    	 bits.tcp=1
		     op = OP_CS_SESSION_INIT
		     cls = CLS_RESPONSE
	    	 status=P2PS_CHANGE_CONN_TYPE
		     sess_id is set
    	     addrs[0] = the relay-server's address

     - case B) FAST P2P
	     If callee is possiblely accessed by fast(direct) p2p(INDEPENDENT FILTER), it send back a <p2pcore_session_init> immediately, with:
		     bits.ct=CONNTYPE_P2P
		     bits.tcp=0
	    	 op = OP_CS_SESSION_INIT
		     cls = CLS_RESPONSE
		     status=0
	    	 sess_id is set
	         addrs[0] = callee's address where the heart-beat comes from.
		 then:
		     a) Server sends OP_SD_SESSION_NOTIFY to callee[, with status=PSS_CHANGE_MAIN_PORT]. 
			 b) Callee responses and punchs on its main port
			 c) Callee binds a new port to send heart-beat

     - case C) P2P
	     If callee cann't be reached by fast p2p, it sends back a <p2pcore_header>, with 
		 	op = OP_CS_SESSION_INIT
	 		cls = CLS_RESPONSE
	 		status=P2PS_CONTINUE.
	     then:
    	     a) Server sends the <p2pcore_session_init> with op=OP_SD_SESSION_NOTIFY to callee
			 b) Callee response on a different port() with it's connection info.
			 c) Server handle off the response to caller with:
		    	 op = OP_SC_SESSION_BEGIN
			     cls = CLS_REQUEST
		    	 status=0
		    	 sess_id is set
		         addrs 
			 d) Caller send <p2pcore_header> as response
		     e)	Re-send request every 1(?) seconds if caller doesn't send a response to OP_SC_SESSION_BEGIN.

     - case D) Relay via SUPPER NODE
	    Server selects a supper node, treat this node as callee like case C, except:
		  (D.1)    in step a), fill sess_id2 and set bits.sibling_sess to 1.

	    The supper node sends response as normal. meanwhile, initialize a connection to real callee like 1), 
		but copy sess_id2 to sess_id, set bits.sibling_sess to 1, then the server will use the sess_id to
		notify the real callee.
 */

struct p2pcore_session_init {
	struct p2pcore_header dh;	

	char sn[LENGTH_OF_SN];
	uint8_t sess_id[LENGTH_OF_SESSION_ID];
	uint8_t sess_id2[LENGTH_OF_SESSION_ID]; //accompanied session
	uint8_t sident[LENGTH_OF_SIDENT]; ///< security-identifier used to identify the client or sender
	union {
		struct conn_bit_fields bits;
		uint32_t bitf;
	};

	struct p2pcore_addr addrs[0];	//local addresses; external address will be filled by server
};


//Caller->Server(UDP)
// Should set trans_id, and it'll be used by server to identify request

//Server->Callee(UDP)
//Callee->Server(TCP or UDP)

//Server->Caller(UDP)
//if bits.ct == CONNTYPE_P2P



//------------------------------------------
#ifdef WIN32
//#pragma pack pop()
#endif


void init_p2pcore_header(struct p2pcore_header* p, int st, int op, int cls, int status, int len, p2pcore_transid_t trans_id);
static INLINE BOOL check_p2pcore_header(const struct p2pcore_header *pdc)
{
	return ((pdc->tag & 0xF0FFFFFF) == P2PCORE_TAG);
}


//Called by IPCAM/CLIENT
int p2pcore_get_challenge(int sk, int st, uint8_t sn_user[16], uint8_t rval[LENGTH_OF_CHALLENGE]);

//Parameter:
//	local_port -- the binding port used to query, can be 0
//Return: 
//	success -- the socket created to query address
//	failed  -- -1
int query_external_address(const struct sockaddr* svr, struct p2pcore_addr* pda, unsigned short local_port);

//Return:
//	0 - Success, return external ip and port delta in ext_ip and delta
//  -1 - Failed
int query_nat_delta(const struct sockaddr* svr, unsigned short local_port, unsigned long *ext_ip, int *delta);



NatType simple_stunt_type(DWORD ip, int *preserve_port, int *hairpin, unsigned short *delta);
NatType simple_stun_type(DWORD ip, int *preserve_port, int *hairpin, unsigned short *delta);

/// Get Local IPs
// Return number of valid local IPs(exclude loopback)
//	< 0 means something wrong
int get_local_ips(uint32_t* pIps, unsigned int size);


//Return:
//	>0, length of received response
//	<0, nagatived error code
//Remark:
//	The size of the buffer must be big enough
int send_recv_over_udp(int sock, const struct sockaddr* addr, const struct p2pcore_header* p, void* buf, int size);

//
void calc_auth(const char* password, const uint8_t* challenge, uint8_t* auth);


#define SOCK_STATUS_CONNECTED	1
#define SOCK_STATUS_ACCEPTED	2
#define SOCK_STATUS_READABLE	3

#define CHECKCONNECTION_OK		0
#define CHECKCONNECTION_CONTINUE	1
#define CHECKCONNECTION_FAKE		2
#define CHECKCONNECTION_RESETED		3	//happened when status is SOCK_STATUS_READABLE
//return: 0 - OK
//	  1 - Continue
//	  <0 - DCSE_XXX/DCSS_XXX error code
typedef int (*CHECKCONNECTIONCB)(int sock, int status, void* data);

//功能:
//	开始TCP打洞过程，直到超时或建立了有效连接
//	调用cb以确认连接的有效性
//
//返回：
//	0: sock points to the socket connected and verified
//	none zero: error code
//
//说明:
//	连接建立时，以 CONNECTED(连接为对方接受) 或 ACCEPTED(接受对方连接) 为 status 参数调用cb。这两个参数没有本质区别。
//	cb返回OK，则连接得到确认，函数返回该连接；返回CONTINUE，则继续确认过程；返回其他值该连接并关闭。
//	当连双方需要交换一些信息确认有效性时，cb向对端发送一些数据并返回 CHECKCONNECTION_CONTINUE。当对端应答到来时，
//	用status=SOCK_STATUS_READABLE再次调用cb
//
int p2pcore_tcp_punch(uint16_t local_port, int another_listening_sock, 
		struct conn_bit_fields bits, const struct p2pcore_addr *peer_addr, 
		/**/int *sock,
		CHECKCONNECTIONCB cb, void* data);


//====================================================================
//--------------------------------------------------------------------
#ifdef __cplusplus
}
#endif
#endif	//#ifndef __p2pcore_h__
