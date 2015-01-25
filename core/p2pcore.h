/** 
  	\file p2pcore.h
  	\author SongZC
 
 	\brief P2P core.
 	which provides functions for connection establishment, and data transfermission.

  会话的请求与建立

  	1. 主叫调用P2pConnInit来初始化一个连接请求，该函数在发出请求后立即返回。真正的连接过程在
	   与调用者不同的线程内执行。连接最终成功或失败的结果通过回调函数来通知调用者。

	   P2pConnInit的最后一个参数是一个void *型的用户自定义指针，这个指针用来区分不同的连接请求。

	   当连接成功建立，一个连接对象HP2PCONN被创建，ConnCreated(hconn, ...) 被调用。用户通过
	   P2pConnGetUserData(hconn, &pUser)来取回在初始化操作中传入的指针，从而可以知道本连接对象
	   对应的请求。在此回调中，用户分配连接相关数据，并调用P2pConnSetUserData(..) 把指针保存于
	   连接对象中。
	   
	   连接失败时 ConnFailed(void *pUser, int err) 会被调用，前述的用户指针被作为第一个参数传入。

	2. 被叫在初始化时向系统注册一个ID，当一个连接到来时，回调函数VerifyAuthString被调用用于验
	   证主叫身份。如果主叫有效，则 ConnCreated 被调用


  安全验证

 	发起的会话请求要在p2p服务器端 或/和 被叫端验证。

	在主叫端使用 sident(security-identier)。这个标识由登录服务器返回并携带在用户对p2p服务器发起
	的连接请求中。

 	在被叫端使用 auth-str(authentication-string)。这个参数在主叫调用P2pConnInit时传入，被叫通过
	VerifyAuthString回调函数获得此认证串。
 */

#ifndef __p2pcore_h__
#define __p2pcore_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "platform_adpt.h"
#include "p2pconst.h"
#include "mediatyp.h"


struct P2PCONN;
typedef struct P2PCONN* HP2PCONN;

#define NETSTATE_OK				0
#define NETSTATE_SERVER_DOES_NOT_RESPONSE	1
typedef struct P2pCoreCbFuncs {
	/** \brief Connection verification. */
	/** Called when a new connection comes in and the authentication is performing.
	 * Return TRUE if verified.
	 * \param auth_str An implementation defined string contains authentication information.
	 */
	BOOL (*VerifyAuthString)(const char *auth_str);

	/** \brief Called when a new session is established */
	int (*ConnCreated)(HP2PCONN hconn);

	/** \brief Connection aborted */
	/** Called when the session is closed by peer, or timeouted, or has problems in kernel.
	 * User should call P2pConnClose later to release resource allocated by hconn. */
	void (*ConnAbortionNotify)(HP2PCONN hconn, int err);

	/** \brief Connection failed */
	/** This callback indicates a connection request started be P2pConnInit(...) failed
	 * \param pUser user's data inputed in P2pConnInit
	 * \param err error code
	 */
	void (*ConnFailed)(int err, void *pUser);

	/** \brief Data arrived.
	 *
	 * Called when data is recevied. */
	void (*OnData)(HP2PCONN, BYTE *pData, int len);

	/** \brief Be called to send hearbeat
	 */
	void (*OnSendHb)(HP2PCONN);

	/// \brief Network state changed
	/**  Called when network state changed.
	 *  state: NETTATE_xxx */
	void (*NetworkStateChanged)(int state);
} P2PCORECBFUNCS;

typedef struct P2PCORENATINFO {
	NatType stunType, stuntType;
	USHORT delta_u, delta_t;
	BOOL	bUpnpIgd;
	BOOL	bMultiNat;	//when bUpnpIgd = TRUE
} P2PCORENATINFO;


/** \brief Get version of this library */
DWORD P2pCoreGetVersion();

/** \brief Initialize p2p core.
 * \param sn Serial-number(ID) of callee, or NULL if application only behaves as a caller
 * \param cbs Callback functions
 * \return 0 if successful; error code is something wrong
 */
int P2pCoreInitialize(const char *svr[], int n_svr, const char *sn, P2PCORECBFUNCS *cbs);

/// \brief Fire a termination signal.
/**  This function fires a termination signal and returns immediately. */
void P2pCoreTerminate();

/// \brief Wait p2p system to be terminated and cleanup resources
/**  Call P2pCoreTerminate() first */
void P2pCoreCleanup();

/// \brief Get NAT information
void P2pCoreGetNatInfo(P2PCORENATINFO* pNi);

#define TPCGSF_LOCAL_READY	0x0001
#define TPCGSF_REMOTE_READY	0x0002
#define TPCGS_EXPIRED		1
#define TPCGS_INVALID_SN	2

typedef struct P2pCoreState {
	int readyStates; ///< combination of TPCGSF_xxx
	int state;		 ///< TPCGS_xxx (invalid sn, expired service)
} P2PCORESTATE;

/// \brief Get state of p2p core
/**  Only used when the app also act as a callee */
int P2pCoreGetState(P2PCORESTATE *tpcState);

typedef struct EnumCallee {
	char ip[16];
	char netmask[16];
	char sn[48];
	int p2pport;
} ENUMCALLEE, *LPENUMCALLEE;

/** \brief Search callees
 *
 * Search callees in LAN
 *
 * \param ppEnumDev the address of a pointer, which will be allocate a buffer holding the search results
 * \param pNDev To receive the amount of records found
 * \param pszSN if not NULL, only search callee with this id.
 * \return 
 * 	\retval TRUE Someone is found, and ppEnumDev/pNDev receive the results and amount
 * 	\retval FALSE None is found
 */
BOOL P2pCoreEnumCallee(LPENUMCALLEE *ppEnumDev, UINT *pNDev, const char *pszSN);

typedef struct _tagTpcConnInfo {
	int ct;	    ///< P2P_CONNTYPE_xxx
	struct sockaddr_in peer;
} P2PCONNINFO;
/** \brief Get connection information
 *  \return 0 - ok; none-zeor - error code
 */
int P2pConnGetInfo(HP2PCONN hconn, P2PCONNINFO *info);

/** \brief Initialize and start a p2p connection request
 *
 *  This operation is asynchronous. Success or failure of the connection is notified
 *  by ConnCreated/ConnFailed callback functions.
 *
 *  \param p2psvr p2p server
 *  \param pcsSN callee's ID
 *  \param sident security-identifier verified on server
 *  \param auth_str an implementation defined string used for authentication on callee
 *  \param pIdent user's data to identify this request.
 *  	连接成功时，这个值可以在 ConnCreated 回调函数中通过 P2pConnGetUserData 获得,
 *  	失败时，则在 ConnFailed 的 pUser 参数中传入
 */
int P2pConnInit(const char *p2psvr, const char *pcsSN, const uint8_t *sident/*LENGTH_OF_SIDENT*/, 
		const char *auth_str, int auth_len, void *pIdent);

/** \brief Intialize and start a p2p connection request
 *
 *  This operation is asynchronous. Success or failure of the connection is notified
 *  by ConnCreated/ConnFailed callback functions.
 *
 *  \param p2psvr p2p server
 *  \param pcsSN callee's ID
 *  \param sident security-identifier verified on server
 *  \param auth_str an implementation defined string used for authentication on callee
 *  \param pIdent user's data to identify this request.
 *  	连接成功时，这个值可以在 ConnCreated 回调函数中通过 P2pConnGetUserData 获得,
 *  	失败时，则在 ConnFailed 的 pUser 参数中传入
 *  \param ct connection type(P2P_CONNTYPE_xxx). If set to P2P_CONNTYPE_AUTO, will try
 *  	p2p first, then relay
 *  \sa P2pConnInit
 */
int P2pConnInitEx(const char *p2psvr, const char *pcsSN, const uint8_t *sident/*LENGTH_OF_SIDENT*/, 
		const char *auth_str, int auth_len, void *pIdent, int ct);

#define P2PCONN_MODE_PUSH     0 ///< default, data is pushed by OnData callback
#define P2PCONN_MODE_PULL     1 ///< data is fetched by call P2pConnRecv(...)

/** \brief Set connection's work mode
 * \param mode P2PCONN_MODE_PUSH or P2PCONN_MODE_PULL */
int P2pConnSetMode(HP2PCONN hconn, int mode);

/** \brief Get connections's work mode
 *  \return the mode */
int P2pConnGetMode(HP2PCONN hconn);

/** \brief Wait for connection to be readable. 
 
 *  Should only be called when connection is in PULL mode
 *
 *  \param phy_chno 0 | 1
 *  \retval >0 readable
 *  \retval =0 timeouted
 *  \retval <0 error encountered
 */
int P2pConnWaitR(HP2PCONN hconn, int wait_ms);

/// \brief Receive data from connection
/** Should only be called when connection is in PULL mode */
int P2pConnRecv(HP2PCONN hconn, BYTE *pBuff, int size);

/** \brief Wait for connection to be writable
 *  \param phy_chno 0 | 1
 *  \retval >0 writable
 *  \retval =0 timeouted
 *  \retval <0 error encountered
 */
int P2pConnWaitW(HP2PCONN hconn, int phy_chno, int wait_ms);

/** \brief Send data in multiparts, to peer.
 *  \param wait_ms time(in milliseconds) to wait
 *  \retval >0            bytes sent
 *  \retval P2PE_TIMEOUT  timeouted (wait_ms is non-zero)
 *  \retval P2PE_AGAIN    connection is busy(underlying sending buffer is full)
 *  \retval <0            other error codes
 */
int P2pConnSendV(HP2PCONN hconn, int phy_chno, PA_IOVEC *v, int size_v, int wait_ms);

/** \brief Send data on connection */
int P2pConnSend(HP2PCONN hconn, int phy_chno, void *pData, int len, int wait_ms);

/** \brief Set timeout value for the connection
 *  \param sec  timeout value, in seconds. must less than 25
 *  \return old timeout value
 */
int P2pConnSetTimeout(HP2PCONN hconn, int sec/*default 15*/);

/// \brief Close connection
int P2pConnClose(HP2PCONN hconn);

//! \brief Close connection asynchronously, used in callback
int P2pConnCloseAsync(HP2PCONN hconn);

/// \brief Associate a user's pointer to session
int P2pConnSetUserData(HP2PCONN hconn, void *pUser);

/// \brief Get user's pointer associated with session
int P2pConnGetUserData(HP2PCONN hconn, void **ppUser);

/** \brief Set user's receive buffer
 *
 *  Use user's rather than internal receiver-buffer, to eliminate copy operation 
 */
int P2pConnSetUserBuffer(HP2PCONN hconn, void *pBuff, int size);

/** \brief Set offset to user's receive buffer.
 *
 *  Next read operation will save data begin from this offset 
 */
int P2pConnSetUserBufferOffset(HP2PCONN hconn, int offset);


typedef BOOL (*ENUMCONNCB)(HP2PCONN hconn, void *pUser);
/** \brief Enumerate connections.
 *
 * Call callback for each connection, break the enumeration if the callback return FALSE
 */
void P2pCoreEnumConn(ENUMCONNCB cb, void *pUser);

#ifdef __cplusplus
}
#endif

#endif

