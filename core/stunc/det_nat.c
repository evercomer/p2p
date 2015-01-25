#include "udp.h"
#include "stun.h"
#include "det_nat.h"

//stun_server: 
//		xxx.xxx.xxx[:port]
//return: bits 0~3: nat type
//	  bit 4: hairpin
//	  bit 5: preserve port
int detect_nat(const char *stun_server)
{
	StunAddress4 stunServerAddr;

	stunServerAddr.addr=0;
	if(!stunParseServerName( stun_server, &stunServerAddr)) return -1;


	BOOL presPort=FALSE;
	BOOL hairpin=FALSE;

	int stype = stunNatType( &stunServerAddr, &presPort, &hairpin, 0, NULL );
	if(presPort) stype |= 0x10;
	if(hairpin) stype |= 0x20;

	return stype;
}

#ifdef DET_NAT_MAIN
int main(int argc, char **argv)
{
	if(argc > 1)
	{
		int nat = detect_nat(argv[1]);
		printf("0x%X\n", nat);
	}
	else
	{
		printf("stun server is required !\n");
	}
	return 0;
}
#endif

