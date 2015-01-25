#include "miniupnpc/miniupnpc.h"
#include "miniupnpc/miniwget.h"
#include "miniupnpc/upnpcommands.h"
#include "upnp_igd_cp.h"
#include "timerq.h"
#include "netbase.h"

//#undef dbg_msg
//#define dbg_msg printf


static unsigned long external_ip = 0;

static char *strncpyz(char *dest, const char *src, int size)
{
	int i;
	size--;
	for(i=0; i<size&&*src; i++)
	{
		dest[i] = src[i];
	}
	dest[i] = '\0';
	return dest;
}

//----------------------------------------------------------------
static const char *pt_name[2] = { "UDP", "TCP" };
#define OP_ADD		1
#define OP_REMOVE	2
typedef struct IgdPortMap {
	int op;
	char desc[24];
	int  port_type;
	//unsigned long lan_ip;
	unsigned short loc_port, ext_port;

	int	mapped;
} IGDPORTMAP;

#define PORTMAP_SIZE	8
typedef struct UPnPIGDWorkParam {
	struct UPNPDev *devList;
	struct UPNPDev *device, *pNext;
	struct UPNPUrls url;
	struct IGDdatas data;

	int 	nItem;
	IGDPORTMAP	map[PORTMAP_SIZE];
} UPNPIGDWORKPARAM;

static UPNPIGDWORKPARAM igd_wp;
static PA_MUTEX h_mutex;
static BOOL bInited = FALSE;

static void clearIgdWp(UPNPIGDWORKPARAM *pWp)
{
	if(pWp->device)
	{
		pWp->device->pNext = pWp->pNext;
		freeUPNPDevlist(pWp->devList);
		pWp->device = pWp->pNext = NULL;
		pWp->devList = NULL;
		FreeUPNPUrls(&pWp->url);
	}
}

BOOL UpnpIgdCpGetNatMappedAddress(unsigned short loc_port, uint32_t *ext_ip, uint16_t *ext_port)
{
	int i;
	if(external_ip == 0) return FALSE;
	*ext_ip = external_ip;
	for(i=0; i<igd_wp.nItem; i++)
	{
		if(igd_wp.map[i].loc_port == loc_port && igd_wp.map[i].mapped)
		{
			*ext_port = igd_wp.map[i].ext_port;
			return TRUE;
		}
	}
	return FALSE;
}

void UpnpIgdCp(UPNPIGDWORKPARAM *pWp)
{
	unsigned int gw, lanip;
	struct UPNPDev *device;
	int interval;

	do {
		interval = 30;
		if( !GetDefaultRoute(&gw, &lanip) || gw == 0 ) 
			break;
		
		char sIp[16];
		IP2STR(lanip, sIp);
		if(!pWp->devList && !(pWp->devList = upnpDiscover(3000, NULL, sIp, 0)) )
			break;

		if(!pWp->device)
		{
			for(device = pWp->devList; device; device = device->pNext)
			{
				char host[100/*MAXHOSTNAMELEN required by parseURL(...)*/], *path;
				unsigned short port;
				if( parseURL(device->descURL, host, &port, &path) && (gw == inet_addr(host)) )
				{
					dbg_msg("found device: %s\n", device->descURL);
					char local_ip[21];
					int rlt;
					if( (rlt = UPNP_GetValidIGD(device, &pWp->url, &pWp->data, local_ip, sizeof(local_ip))) != 1)
					{
						if(rlt) FreeUPNPUrls(&pWp->url);
						freeUPNPDevlist(pWp->devList);
						pWp->devList = NULL;
					}
					else 
					{
						dbg_msg("............local_ip = %s\n", local_ip);
						pWp->pNext = device->pNext;
						device->pNext = NULL;
						pWp->device = device;
					}
					break;
				}
			}
			if(!pWp->device)
				break;
		}



		char inClient[16], inPort[6], ext_ip[16];

		/* 通信失败，一切从头来过 */
		if(UPNP_GetExternalIPAddress(pWp->url.controlURL, pWp->data.first.servicetype, ext_ip) != 0 
				|| (external_ip && (inet_addr(ext_ip) != external_ip)) )
		{
			clearIgdWp(&igd_wp);
			interval = 5;
			break;
		}
		external_ip = inet_addr(ext_ip);

		/* P2P Ports */
		int i;
		PA_MutexLock(h_mutex);
		for(i=0; i < pWp->nItem; )
		{
			char extPort[6]/*external port*/;
		    const char *pt;
			IGDPORTMAP *pItem = &igd_wp.map[i];

			sprintf(extPort, "%u", pItem->ext_port);
			pt = pItem->port_type == IGD_PORTTYPE_UDP ? pt_name[0] : pt_name[1];

			if(pItem->op == OP_REMOVE) 
			{
				if( 0 == UPNP_GetSpecificPortMappingEntry(pWp->url.controlURL, 
							pWp->data.first.servicetype, extPort, 
							pt, inClient, inPort) )
					UPNP_DeletePortMapping(igd_wp.url.controlURL, igd_wp.data.first.servicetype, 
							extPort, pt, NULL);
				memcpy(&igd_wp.map[i], &igd_wp.map[i+1], sizeof(IGDPORTMAP)*igd_wp.nItem-i-1);
				pWp->nItem--;
				continue;
			}

			if(pItem->op == OP_ADD)
			{
				if( 0 != UPNP_GetSpecificPortMappingEntry(pWp->url.controlURL, 
							pWp->data.first.servicetype, extPort, 
							pt, inClient, inPort)
						|| ( ( inet_addr(inClient) != lanip || atoi(inPort) != pItem->ext_port ) &&
							UPNP_DeletePortMapping(pWp->url.controlURL, pWp->data.first.servicetype, 
								extPort, pt, NULL) == 0) )
				{
					char sLocalPort[8], sLanIp[16];

					sprintf(sLocalPort, "%u", pItem->loc_port);
					IP2STR(lanip, sLanIp);
					pItem->mapped = (UPNP_AddPortMapping(pWp->url.controlURL, pWp->data.first.servicetype, 
								extPort, sLocalPort, sLanIp, pItem->desc, pt, NULL) == 0);
				}
				dbg_msg("upnp map port %s on %s, external address:%s, mapped:%d\n", extPort, pWp->url.controlURL, ext_ip, pItem->mapped);
			}
			i++;
		}
		PA_MutexUnlock(h_mutex);

		interval = 1800;
	} while(0);

	TimerQueueQueueItem(g_pSlowTq, (SERVICEFUNC)UpnpIgdCp, &igd_wp, interval*1000, "UpnpIgdCp");
}

void UpnpIgdCpInitialize()
{
	if(!bInited)
	{
		PA_MutexInit(h_mutex);
		memset(&igd_wp, 0, sizeof(igd_wp));
		bInited = TRUE;
	}
}
void UpnpIgdCpStop()
{
	if(bInited)
	{
		TimerQueueDequeueItem(g_pSlowTq, (SERVICEFUNC)UpnpIgdCp, &igd_wp, TRUE);
		clearIgdWp(&igd_wp);
		external_ip = 0;
		PA_MutexUninit(h_mutex);
		bInited = FALSE;
	}
}

static void _UpnpIgdCpDelPortMap(unsigned short ext_port, int port_type);

//return: 1 - request posted;
//	  2 - map created;
int UpnpIgdCpAddPortMap(const char *desc, unsigned short loc_port, unsigned short ext_port, int port_type)
{
	int i;
	IGDPORTMAP *pItem = NULL;

	PA_MutexLock(h_mutex);
	for(i=0; i<igd_wp.nItem; i++)
	{
		if(igd_wp.map[i].ext_port == ext_port)
		{
			_UpnpIgdCpDelPortMap(ext_port, port_type);
			PA_MutexUnlock(h_mutex);
			return 0;
		}
	}

	if(igd_wp.nItem < PORTMAP_SIZE)
	{
		pItem = &igd_wp.map[igd_wp.nItem++];

		pItem->op = OP_ADD;
		pItem->loc_port = loc_port;
		pItem->ext_port = ext_port;
		pItem->port_type = port_type;
		pItem->mapped = 0;
		strncpyz(pItem->desc, desc, sizeof(pItem->desc));

		TimerQueueDequeueItem(g_pSlowTq, (SERVICEFUNC)UpnpIgdCp, NULL, TRUE);
		TimerQueueQueueItem(g_pSlowTq, (SERVICEFUNC)UpnpIgdCp, &igd_wp, 2*1000, "");
		PA_MutexUnlock(h_mutex);

		return 1;
	}
	else
	{
		PA_MutexUnlock(h_mutex);
		return -1;
	}

	return 0;
}
int UpnpIgdCpGetMappedState(unsigned short ext_port, int port_type)
{
	int i;
	for(i=0; i<igd_wp.nItem; i++)
	{
		IGDPORTMAP *pItem = &igd_wp.map[i];

		if(pItem->ext_port == ext_port && pItem->port_type == port_type)
			return pItem->mapped;
	}
	return 0;
}
static void _UpnpIgdCpDelPortMap(unsigned short ext_port, int port_type)
{
	int i;

	for(i=0; i<igd_wp.nItem; i++)
	{
		IGDPORTMAP *pItem = &igd_wp.map[i];
		if(pItem->ext_port == ext_port && pItem->port_type == port_type)
		{
			if(pItem->mapped)
			{
				pItem->op = OP_REMOVE;
				TimerQueueDequeueItem(g_pSlowTq, (SERVICEFUNC)UpnpIgdCp, &igd_wp, TRUE);
				TimerQueueQueueItem(g_pSlowTq, (SERVICEFUNC)UpnpIgdCp, &igd_wp, 2*1000, "");
			}
			else
			{
				memcpy(&igd_wp.map[i], &igd_wp.map[i+1], sizeof(IGDPORTMAP)*igd_wp.nItem-i-1);
				--igd_wp.nItem;
			}
			break;
		}
	}
}
void UpnpIgdCpDelPortMap(unsigned short ext_port, int port_type)
{
	PA_MutexLock(h_mutex);
	_UpnpIgdCpDelPortMap(ext_port, port_type);
	PA_MutexUnlock(h_mutex);
}
//=====================================================================

