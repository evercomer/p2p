#ifndef __upnp_igd_cp_h__
#define __upnp_igd_cp_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "platform_adpt.h"

void UpnpIgdCpInitialize();

#define IGD_PORTTYPE_UDP	0
#define IGD_PORTTYPE_TCP	1
int UpnpIgdCpAddPortMap(const char *desc, unsigned short loc_port, unsigned short ext_port, int port_type);
void UpnpIgdCpDelPortMap(unsigned short ext_port, int port_type);

void UpnpIgdCpStop();

BOOL UpnpIgdCpGetNatMappedAddress(unsigned short loc_port, uint32_t *ext_ip, uint16_t *ext_port);

#ifdef __cplusplus
}
#endif

#endif

