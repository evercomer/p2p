#ifndef __rtspsvc_h__
#define __rtspsvc_h__

#include "rtspsvc.h"
#include "ctp.h"

int HandleRTSPCommand(CLIENT *pclt, REQUESTLINE *preq, REQUESTOPTIONS *popt, char *body);


#endif
