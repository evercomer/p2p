#include "platform_adpt.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#ifdef WIN32
#include <errno.h>
#include <IphlpApi.h>
#elif defined(__LINUX__)
#include <net/if.h>
#include <sys/ioctl.h>
#endif
#include "netbase.h"

BOOL ResolveHost(const char* host, uint32_t* pIP)
{
	struct hostent *h = gethostbyname(host);
	if(h)
	{
		*pIP = *(uint32_t*)(h->h_addr_list[0]);
		return TRUE;
	}
	return FALSE;
}

char* IP2STR(unsigned int ip, char ips[16])
{
	int len = sprintf(ips, "%d.", ip&0xFF);
	len += sprintf(ips+len, "%d.", (ip>>8)&0xFF);
	len += sprintf(ips+len, "%d.", (ip>>16)&0xFF);
	len += sprintf(ips+len, "%d", (ip>>24)&0xFF);
	return ips;
}

/*
 * Create socket and bind to the given port
 */
int NewSocketAndBind(int sock_type, unsigned long bind_ip, unsigned short port)
{
	struct sockaddr_in sa;
	PA_SOCKET fd;
	int opt;

	fd = socket(AF_INET, sock_type, 0);
	if(fd == INVALID_SOCKET) return INVALID_SOCKET;
	opt = 1;
	PA_SetSockOpt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)); 
	
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = bind_ip;//htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	if(bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) 
	{
#ifdef WIN32
		fd = WSAGetLastError();
#else
		perror("bind");
#endif
		PA_SocketClose(fd);
		return INVALID_SOCKET;
	}
	return fd;
}

int CreateServiceSocket(int sock_type, unsigned long bind_ip, unsigned short port)
{
	int fd = NewSocketAndBind(sock_type, bind_ip, port);
	if(fd != INVALID_SOCKET && sock_type == SOCK_STREAM) listen(fd, 5);
	return fd;
}

#ifdef __LINUX__
unsigned int GetIfAddr(const char* ifname, unsigned int *netmask)
{
	int sock;
	struct ifreq req;
	strcpy(req.ifr_name, ifname);
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(ioctl(sock, SIOCGIFADDR, &req) == 0)
	{
		unsigned int ip = ((struct sockaddr_in*)&req.ifr_addr)->sin_addr.s_addr;
		if(netmask)
		{
			ioctl(sock, SIOCGIFNETMASK, &req);
			*netmask = ((struct sockaddr_in*)&req.ifr_addr)->sin_addr.s_addr;
		}
		close(sock);
		return ip;
	}
	close(sock);
	return 0;
}

BOOL GetDefaultRoute(unsigned int *ip_gw, unsigned int *ip_local)
{
	char line[400];
	*ip_gw = *ip_local = 0;
	FILE *fp = fopen("/proc/net/route", "r");
	if(fp)
	{
		while(fgets(line, 400, fp))
		{
			unsigned int dest;
			char intrf[64];
			if(sscanf(line, "%s %x %x", intrf, &dest, ip_gw) == 3 && dest == 0)
			{
				*ip_local = GetIfAddr(intrf, NULL);
				fclose(fp);
				return TRUE;
			}
		}
		fclose(fp);
	}
	return FALSE;
}
#elif defined(WIN32)
BOOL GetDefaultRoute(unsigned int *ip_gw, unsigned int *ip_local)
{
	UINT i;
	int ifIndex = -1;
	ULONG len;
	DWORD dwGW = 0;
	MIB_IPFORWARDTABLE *pift;	//gateway
	MIB_IPADDRTABLE *piat;		//ip

	//
	// Gateway
	//
	len = 0;
	GetIpForwardTable(NULL, &len, FALSE);
	pift = (MIB_IPFORWARDTABLE*)malloc(len);
	GetIpForwardTable(pift, &len, FALSE);
	for(i=0; i < pift->dwNumEntries; i++)
	{
		if(pift->table[i].dwForwardDest == 0 && pift->table[i].dwForwardMask == 0)
		{
			*ip_gw = pift->table[i].dwForwardNextHop;
			ifIndex = pift->table[i].dwForwardIfIndex;
			//pift->table[i].dwForwardIfIndex;
			break;
		}
	}
	free(pift);
	if(ifIndex < 0) return FALSE;

	//
	// IP Address
	//
	len = 0;
	GetIpAddrTable(NULL, &len, FALSE);
	piat = (MIB_IPADDRTABLE*)malloc(len);
	GetIpAddrTable(piat, &len, FALSE);
	for(i=0; i<piat->dwNumEntries; i++)
	{
		if(piat->table[i].dwIndex == ifIndex && (dwGW & piat->table[i].dwMask) == (piat->table[i].dwAddr & piat->table[i].dwMask))
		{
			*ip_local = piat->table[i].dwAddr;
			break;
		}
	}
	free(piat);

	return TRUE;
}
#else
#error "Platform is not specified !"
#endif


BOOL IsInternet(struct in_addr addr)
{
#if 1
	unsigned int netmask = ntohl(addr.s_addr);
	return !((netmask & 0xFF000000) == 0x0A000000 ||  //10.x.x.x
		(netmask & 0xFFFF0000) == 0xC0A80000 || //192.168.x.x
		((netmask & 0xFFFF0000) >= 0xAC100000 && (netmask & 0xFFFF0000) <= 0xAC1F0000));	//172.16.x.x ~ 172.31.x.x
#else
	unsigned int netmask = addr.s_addr & 0xFFFF;
	return !((addr.s_addr&0xFF)==10 || netmask==0xA8C0 || (netmask&0xFF)==0xAC&&(netmask&0xFF00)>=0x1000 && (netmask&0xFF00)<=0x1F00);
#endif
}

#ifdef __LINUX__
int GetIpAddresses(ETHERNIC *pAN, int size)
{
	/* Retrieve the interfaces information */
	int sock;
	int i, n = 0;
	struct ifconf ifcf;
	struct ifreq *pifreqs;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock < 0)
	{
		perror("socket");
		return 0;
	}

	ifcf.ifc_len = sizeof(struct ifreq) * 16;
	pifreqs	= (struct ifreq*)malloc(sizeof(struct ifreq) * 16);

	//ifcf.ifc_buf =pifreqs;
	//
	ifcf.ifc_req = pifreqs;
	//

	if(ioctl(sock, SIOCGIFCONF, &ifcf) == 0)
	{
		for(i=0; i<(ifcf.ifc_len/sizeof(struct ifreq)) && (n < size); i++)
		{
			if( pifreqs[i].ifr_addr.sa_family == AF_INET && strncmp(pifreqs[i].ifr_name, "lo", 2) )
			{
				struct ifreq ifrq;
				strcpy(ifrq.ifr_name, pifreqs[i].ifr_name);
				if(ioctl(sock, SIOCGIFFLAGS, &ifrq) == 0)
				{
					pAN[n].flags = ifrq.ifr_flags;
					strcpy(pAN[n].name, pifreqs[i].ifr_name);
					if(ioctl(sock, SIOCGIFADDR, &pifreqs[i]) == 0)
						pAN[n].addr = ((struct sockaddr_in*)&pifreqs[i].ifr_addr)->sin_addr;
					if(ioctl(sock, SIOCGIFNETMASK, &pifreqs[i]) == 0)
						pAN[n].netmask = ((struct sockaddr_in*)&pifreqs[i].ifr_addr)->sin_addr;
					n++;
				}
				else
					perror("ioctl(, SIOCGIFFLAGS, )");
			}
		}
	}
	else
		perror("ioctl(, SIOCGIFCONF, )");
	close(sock);
	free(pifreqs);
	return n;
}

BOOL IntfIsUp(const char *sIntf)
{
	struct ifreq req;
	int sock;
	BOOL isUp;
       
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	strcpy(req.ifr_name, sIntf);
	isUp = (ioctl(sock, SIOCGIFFLAGS, &req) == 0) && (req.ifr_flags & IFF_UP);
	close(sock);
	return isUp;
}
#elif defined(WIN32)
int GetIpAddresses(ETHERNIC *pAn, int size)
{
	//
	// IP Address
	//
	MIB_IPADDRTABLE *piat;
	ULONG len = 0;
	UINT i, n;
	GetIpAddrTable(NULL, &len, FALSE);
	piat = (MIB_IPADDRTABLE*)malloc(len);
	GetIpAddrTable(piat, &len, FALSE);
	for(i=n=0; i<piat->dwNumEntries && i < size; i++)
	{
		if(piat->table[i].dwAddr && (piat->table[i].dwAddr != 0x0100007F))	//127.0.0.1
		{
			pAn[n].addr.s_addr = piat->table[i].dwAddr;
			pAn[n].netmask.s_addr = piat->table[i].dwMask;
			pAn[n].ifIdx = piat->table[i].dwIndex;
			n++;
		}
	}
	free(piat);
	return n;
}
#endif


int init_sai(struct sockaddr_in* sai, const char* shost, unsigned short def_port)
{
	uint32_t ip;
	char *ss = NULL;

	const char *colon = strchr(shost, ':');
	if(colon) 
	{
		def_port = atoi(colon+1); 
		ss = (char*)malloc(colon - shost); 
		strncpy(ss, shost, colon - shost); 
		ss[colon-shost] = '\0'; 
	}

	if(!ResolveHost(ss?ss:shost, (uint32_t*)&ip))
	{
		if(ss) free(ss);
		return -1;
	}

	if(ss) free(ss);
	memset(sai, 0, sizeof(struct sockaddr_in));
	sai->sin_family = AF_INET;
	sai->sin_port = htons(def_port);
	sai->sin_addr.s_addr = ip;
	return 0;
}

int timed_wait_fd(int fd, unsigned int ms)
{
	fd_set rfds;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	tv.tv_sec = ms/1000;
	tv.tv_usec = (ms%1000)*1000;
	return select(fd+1, &rfds, NULL, NULL, &tv);
}

/// returen: 
//	-2  error
//	 -1 timeout 
//	 0  peer closed
//	 >0 bytes recved
int timed_recv(int sk, void* ptr, int size, unsigned int ms)
{
	int r = timed_wait_fd(sk, ms);
	if(r < 0)
	{
		perror("timed recv select"); 
		return  -2;
	}
	if(r == 0) 
	{
		dbg_msg("select timeout.............\n");
		return -1;
	}
	r = PA_Recv(sk, ptr, size, 0);
	if(r < 0)
	{ 
		perror("recv error");
		return -2;
	}
	return r;
}

int timed_recv_from(int sk, void* ptr, int size, struct sockaddr* addr, unsigned int *sock_len, unsigned int ms)
{
	int r = timed_wait_fd(sk, ms);
	if(r < 0) 
	{
#ifdef WIN32
		r = WSAGetLastError();
#else
		perror("select");
#endif
		return  -2;
	}
	if(r == 0) return -1;

	r = PA_RecvFrom(sk, ptr, size, 0, addr, (socklen_t*)sock_len);
	if(r < 0) 
		return -2; 
	return r;
}

int timed_wait_fd_w(int fd, unsigned int ms)
{
	fd_set wfds;
	struct timeval tv;

	FD_ZERO(&wfds);
	FD_SET(fd, &wfds);
	tv.tv_sec = ms/1000;
	tv.tv_usec = (ms%1000)*1000;
	return select(fd+1, NULL, &wfds, NULL, &tv);
}

int writen(int sock, void *p, int len, unsigned int tmout/*ms*/)
{
	fd_set wfds, rfds, efds;
	struct timeval tv;

	int wtotal = 0, sel;
	while(wtotal < len)
	{
		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);
		FD_ZERO(&wfds);
		FD_SET(sock, &wfds);
		FD_ZERO(&efds);
		FD_SET(sock, &efds);
		if(tmout >= 0)
		{
			tv.tv_sec = tmout/1000;
			tv.tv_usec = (tmout%1000)*1000;
		}
		sel = select(sock+1, &rfds, &wfds, &efds, (tmout==~0L)?&tv:NULL);
		if(sel < 0)
		{
#ifdef WIN32
			if(WSAGetLastError() == WSAEINTR) continue;
#else
			if(errno == EINTR) continue;
#endif
			return -1;
		}
		if(sel == 0) return -1;
		if(FD_ISSET(sock, &efds)) return -1;
		if(  FD_ISSET(sock, &rfds ) )
		{
			char buf[101];
			if(recv(sock, buf, 100, 0) == 0) return 0;
		}

		if(FD_ISSET(sock, &wfds))
		{
			int wlen = PA_Send(sock, p, len - wtotal, 0);
			if(wlen < 0)
			{
#ifdef WIN32
				if(WSAGetLastError() == WSAEINTR) continue;
#else
				if(errno == EINTR) continue;
#endif
			   	return -2;
			}
			wtotal += wlen;
			p = (char*)p + wlen;
		}
		else
			continue;
	}
	return wtotal;
}


#ifdef WIN32
int setblk(int s, int b)
{
	int opt = b?0:1;
	return ioctlsocket(s, FIONBIO, (u_long*)&opt);
}
int isfatal(int r)
{
	if(r < 0 && WSAGetLastError() != WSAEWOULDBLOCK) return 1;
	return 0;
}

#else
int setblk(int s, int b)
{
	int opt = fcntl(s, F_GETFL, &opt, 0);
	if(b) opt &= ~O_NONBLOCK;
	else opt |= O_NONBLOCK;
	return fcntl(s, F_SETFL, opt);
}
int isfatal(int r)
{
	if(r < 0 && errno != EWOULDBLOCK) return 1;
	return 0;
}
#endif

int setlinger(int s, int onoff, int linger)
{
	struct linger opt = { onoff, linger };
	return setsockopt(s, SOL_SOCKET, SO_LINGER, 
#ifdef WIN32
		(const char*)&opt, 
#else
		&opt,
#endif
		sizeof(opt));
}

/*
void key2string(const unsigned char key[16], char str[25])
{
	int i, j;
	//0~12
	int ci, bi, val;
	for(i=0, j=0; i<20; i++, j++)
	{
		if(i==4 || i==8 || i==12 || i==16) str[j++] = '-';
		ci = i*5/8;
		bi = (i*5)%8;
		if(bi < 3)
			val = (key[ci] >> (3-bi)) & 0x1F;
		else
			val = ((key[ci] << (bi-3)) | (key[ci+1] >> (11-bi))) & 0x1F;
		if(val <= 9)
			str[j] = '0' + val;
		else
			str[j] = 'A' + val - 10;
	}
	str[24] = '\0';
}

BOOL string2key(const char *str, unsigned char key[16])
{
	int i, j;
	int ci, bi;
	char strkey[25];

	memset(key, 0, 16);
	for(i=0, j=0; str[j]; j++)
	{
		if(isalnum(str[j])) strkey[i++] = toupper(str[j]);
		else if(str[j] != '-') return FALSE;
	}
	strkey[i++] = '\0';
	if(i != 21) return 0;
	for(i=0; i<20; i++)
	{
		if(isdigit(strkey[i])) strkey[i] -= '0';
		else strkey[i] -= ('A' - 10);
	}
	for(i=0; i<13; i++)
	{
		ci = 8*i/5;
		bi = (8*i)%5;
		if(bi<=2)
			key[i] = (strkey[ci] << (bi+3)) | (strkey[ci+1] >> (2-bi));
		else
			key[i] = (strkey[ci] << (bi+3)) | (strkey[ci+1] << (bi-2)) | (strkey[ci+2] >> (7-bi));
	}
	key[12] &= 0xF0;
	return TRUE;
}
*/
