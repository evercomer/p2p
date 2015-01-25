#ifndef __det_net_h__
#define __det_net_h__

#ifdef __cplusplus
extern "C" {
#endif
//stun_server: 
//		xxx.xxx.xxx[:port]
//return: bits 0~3: nat type
//	  bit 4: hairpin
//	  bit 5: preserve port
int detect_nat(const char *stun_server);

#ifdef __cplusplus
}
#endif

#endif

