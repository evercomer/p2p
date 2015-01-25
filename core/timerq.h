#ifndef __timerq_h__
#define __timerq_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "platform_adpt.h"

typedef void (*SERVICEFUNC)(void *);

struct service_queue;
typedef struct service_queue TIMERQUEUE, *LPTIMERQUEUE;

int TimerQueueQueueItem(TIMERQUEUE *pQueue, SERVICEFUNC func, void* data, int run_after/*milliseconds*/, const char *name);
void TimerQueueDequeueItem(TIMERQUEUE *pQueue, SERVICEFUNC func, void* data, BOOL bIgnoreData);

TIMERQUEUE *TimerQueueCreate();
void TimerQueueDestroy(TIMERQUEUE *pQueue);

extern TIMERQUEUE *g_pSlowTq, *g_pFastTq;

#ifdef __cplusplus
}
#endif

#endif

