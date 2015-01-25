#ifndef __netbase_h__
#define __netbase_h__

#include "platform_adpt.h"

#ifdef __cplusplus
extern "C" {
#endif

extern BOOL ResolveHost(const char* host, uint32_t* pIP);
char* IP2STR(unsigned int ip, char ips[16]);
//* shost can be like:  xx.xx.xxx:port
extern int init_sai(struct sockaddr_in* sai, const char* shost, unsigned short def_port);

//bind_ip: ip to bind to. network byte order
//port: port to bind. host byte order
int NewSocketAndBind(int sock_type, unsigned long bind_ip, unsigned short port);
//call listen() if sock_type is SOCK_STREAM
int CreateServiceSocket(int sock_type, unsigned long bind_ip, unsigned short port);

BOOL GetDefaultRoute(unsigned int *ip_gw, unsigned int *ip_local);

BOOL IsInternet(struct in_addr addr);

typedef struct ether_nic {
#ifdef WIN32
	int ifIdx;
#elif defined(__LINUX__)
	char name[16];//name[IFNAMSIZ];
	int flags;
#endif
	struct in_addr addr;
	struct in_addr netmask;
} ETHERNIC;
int GetIpAddresses(ETHERNIC *pAN, int size);

//wait to be readable
int timed_wait_fd(int fd, unsigned int ms);
//wait to be writable
int timed_wait_fd_w(int fd, unsigned int ms);
int timed_recv(int sk, void* ptr, int size, unsigned int ms);
int timed_recv_from(int sk, void* ptr, int size, struct sockaddr* addr, unsigned int *sock_len, unsigned int ms);
int setblk(int s, int b);
int setlinger(int s, int onoff, int linger);


#ifdef __cplusplus
}
#endif

#endif
