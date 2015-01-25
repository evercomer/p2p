#ifndef __p2pconst_h__
#define __p2pconst_h__


/// \name Status codes
///@{
#define P2PS_OK                 0
#define P2PS_AUTH_FAILED        1
#define P2PS_ERROR_TRANSID      2
#define P2PS_ERROR_PACKET       3//??
#define P2PS_SESSION_CLOSED     4
#define P2PS_ADDRESS_CHANGED    5 //for OP_DS_IAMHERE, server return a new address, then ipcam should report itself to the new server 
#define P2PS_INACTIVE           6
#define P2PS_CALLEE_OFFLINE     7 //Offline
#define P2PS_UNEXPECTED         8
#define P2PS_LACK_OF_RESOURCE   9
#define P2PS_NOT_ALLOWED        10
#define P2PS_RELAY_SERVER_UNVAILABLE     11
#define P2PS_UNRECOGNIZED_CMD   12
#define P2PS_CHANGE_CONN_TYPE   13
#define P2PS_CONTINUE           14 //通知发送端请求已经收到
#define P2PS_CHANGE_MAIN_PORT   15 //with OP_SD_SESSION_NOTIFY
///@}

/// \brief Error codes
///@{
#define P2PE_OK                     0
#define P2PE_CANNOT_RESOLVE_HOST    -20001
#define P2PE_INVALID_CONN_OBJECT    -20002
#define P2PE_NO_RESPONSE            -20003
#define P2PE_SERVER_NO_RESPONSE     -20004
#define P2PE_ERROR_RESPONSE         -20005
#define P2PE_SOCKET_ERROR           -20006
#define P2PE_CONN_FAILED            -20007
#define P2PE_TIMEOUTED              -20008
#define P2PE_NO_DEV                 -20009 //cann't be searched
#define P2PE_CONN_ABORTED           -20010 //connection aborted(peer closed)
#define P2PE_CONN_TIMEOUTED         -20011 //connection is timeouted, not usable any longer
#define P2PE_AGAIN                  -20012
#define P2PE_INVALID_PARAM          -20013
#define P2PE_FAILED                 -20014
#define P2PE_NOT_ALLOWED            -20015
///@}


/// \name Connection Type
///@{
#define P2P_CONNTYPE_AUTO     0
#define P2P_CONNTYPE_LOCAL    1
#define P2P_CONNTYPE_P2P      2
#define P2P_CONNTYPE_RELAY    3
#define P2P_CONNTYPE_AS_PROXY 4 //act as a relayer between two objects
///@}

//! \brief Enumeration of NAT type
typedef enum 
{
	StunTypeUnknown=0,
	StunTypeBlocked=0,
	StunTypeOpen,

#ifndef RFC_STUN
	StunTypeIndependentFilter,
	StunTypeDependentFilter,
	StunTypePortDependedFilter,
	StunTypeDependentMapping,
#else
	StunTypeConeNat,
	StunTypeRestrictedNat,
	StunTypePortRestrictedNat,
	StunTypeSymNat,
#endif
	StunTypeFirewall,
	StunTypeFailure,
} NatType;


#endif

