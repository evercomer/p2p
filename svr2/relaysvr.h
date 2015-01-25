#ifndef __relaysvr_h__
#define __relaysvr_h__

#include "p2pbase.h"

#define OP_RELAY_NOTIFY	11
#define ST_RELAYER		3

struct p2pcore_relay_notification {
	struct p2pcore_header dh;
	uint8_t  sess_id[LENGTH_OF_SESSION_ID];
};

#define RELAYSERVER_NOTIFY_PORT		9004
#define RELAYSERVER_MEDIA_PORT		9005

#endif
