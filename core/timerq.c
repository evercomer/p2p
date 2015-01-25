#ifdef __LINUX__
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#elif defined(WIN32)
#endif
#include <string.h>
#include "timerq.h"
#include "linux_list.h"
#include "platform_adpt.h"


#define MAX_QUEUE_SIZE	10


struct service_item {
	char name[16];
	SERVICEFUNC func;
	void *data;
	int run_after;
};

#define CMD_ADD_ITEM	0
#define CMD_DEL_ITEM	1
struct queue_cmd {
	int cmd;
	struct service_item item;
};
struct service_queue_item {
	struct list_head list;
	struct service_item item;
	BOOL bNew;
};

struct service_queue {
	PA_HTHREAD hThread;
	PA_PIPE pipe[2];
};
TIMERQUEUE *g_pSlowTq = NULL, *g_pFastTq = NULL;


/// Does not queue same item twice at same time.
static void queueServiceItem(struct list_head *free_list, struct list_head *wait_list, struct service_item *svi)
{
	struct list_head* p;
	struct service_queue_item *item;

	list_for_each(p, wait_list)
	{
		item = list_entry(p, struct service_queue_item, list);
		if(item->item.func == svi->func && item->item.data == svi->data)
		{
			return;
		}
	}

	if(list_empty(free_list)) return;
	p = free_list->next;
	list_del(p);
	INIT_LIST_HEAD(p);
	item = list_entry(p, struct service_queue_item, list);
	memcpy(&item->item, svi, sizeof(struct service_item));
	item->bNew = TRUE;
	//dbg_msg("service item queued, run after=%d\n", item->item.run_after);

	list_add_tail(p, wait_list);
}
static void dequeueServiceItem(struct list_head *free_list, struct list_head *wait_list, const struct service_item *psi)
{
	struct list_head *p, *q;
	struct service_queue_item *item;

	list_for_each_safe(p, q, wait_list)
	{
		item = list_entry(p, struct service_queue_item, list);
		if(item->item.func == psi->func && (psi->run_after/*bIgnoreData*/ || item->item.data == psi->data))
		{
			list_del(&item->list);
			INIT_LIST_HEAD(&item->list);
			list_add_tail(&item->list, free_list);
		}
	}
}

PA_THREAD_RETTYPE __STDCALL TimerQueueThread(void* data)
{
	struct service_queue *pq = (struct service_queue*)data;
	DWORD t1, t2;
	struct list_head *p;
	int elapse = 0;
	struct list_head wait_list, free_list;
	struct service_queue_item *pItems;
	int i;

	INIT_LIST_HEAD(&wait_list);
	INIT_LIST_HEAD(&free_list);
	pItems = (struct service_queue_item*)malloc(sizeof(struct service_queue_item)*MAX_QUEUE_SIZE);
	for(i=0; i<MAX_QUEUE_SIZE; i++)
	{
		INIT_LIST_HEAD(&pItems[i].list);
		list_add_tail(&pItems[i].list, &free_list);
	}
	t1 = PA_GetTickCount();
	while(1)
	{
		struct service_queue_item* itemToProcess = NULL;
		unsigned int min_wait = ~0UL; //ms

		//find the minimum waiting time
		list_for_each(p, &wait_list)
		{
			struct service_queue_item *item;
			item = list_entry(p, struct service_queue_item, list);

			if(item->bNew) item->bNew = FALSE;
			else 
			{
				item->item.run_after -= elapse;
				//dbg_msg("run_after = %d, elapsed = %d\n", item->item.run_after, elapse);
				if(item->item.run_after <= 0)
				{
					if(!itemToProcess) itemToProcess = item;
				}
			}
			if(item->item.run_after > 0 && item->item.run_after < min_wait) 
				min_wait = item->item.run_after;
		}


		if(itemToProcess)
		{
			list_del(&itemToProcess->list);
			itemToProcess->item.func(itemToProcess->item.data);
			list_add_tail(&itemToProcess->list, &free_list);
		}
		else
		{
#ifdef __LINUX__
			struct timeval tv;
			fd_set rfds;

			FD_ZERO(&rfds);
			FD_SET(pq->pipe[0], &rfds);
			tv.tv_sec = min_wait/1000;
			tv.tv_usec = (min_wait%1000)*1000;
			if(select(pq->pipe[0]+1, &rfds, NULL, NULL, min_wait?&tv:NULL) > 0 && FD_ISSET(pq->pipe[0], &rfds))
#elif defined(WIN32)
			if(WaitForSingleObject(pq->pipe[0], min_wait) == WAIT_OBJECT_0)
#endif
			{
				struct queue_cmd cmd;
#ifdef __LINUX__
				if((read(pq->pipe[0], &cmd, sizeof(cmd)) == sizeof(cmd)) && cmd.item.func )
#elif defined(WIN32)
				DWORD dwRead;
				if(ReadFile(pq->pipe[0], &cmd, sizeof(cmd), &dwRead, NULL) && dwRead == sizeof(cmd) && cmd.item.func)
#endif
				{
					if(cmd.cmd == CMD_ADD_ITEM) 
					{
						if(cmd.item.run_after <= 0)
							cmd.item.func(cmd.item.data);
						else
							queueServiceItem(&free_list, &wait_list, &cmd.item);
					}
					else if(cmd.cmd == CMD_DEL_ITEM) dequeueServiceItem(&free_list, &wait_list, &cmd.item);
				}
				else
				{
					break;
				}
			}
		}
		t2 = PA_GetTickCount();
		elapse = t2 - t1;
		if(elapse) t1 = t2;
	}

	free(pItems);
dbg_msg("TimerQueueThread terminaed\n");
	return (PA_THREAD_RETTYPE)NULL;
}

////////////////////////////////////////////////////////////////////////////////
//-----------------------------------------
int TimerQueueQueueItem(TIMERQUEUE *pQueue, SERVICEFUNC func, void* data, int run_after, const char *name)
{
	struct queue_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = CMD_ADD_ITEM;
	if(name) 
	{
		strncpy(cmd.item.name, name, sizeof(cmd.item.name));
		cmd.item.name[15] = '\0';
	}
	else
		cmd.item.name[0] = '\0';
	cmd.item.func = func;
	cmd.item.data = data;
	cmd.item.run_after = run_after;
#ifdef __LINUX__
	return write(pQueue->pipe[1], &cmd, sizeof(cmd))>0?0:-1;
#elif defined(WIN32)
	{ DWORD dwWritten;	return WriteFile(pQueue->pipe[1], &cmd, sizeof(cmd), &dwWritten, NULL)?0:-1; }
#endif
}
void TimerQueueDequeueItem(TIMERQUEUE *pQueue, SERVICEFUNC func, void* data, BOOL bIgnoreData)
{
	struct queue_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = CMD_DEL_ITEM;
	cmd.item.func = func;
	cmd.item.data = data;
	cmd.item.run_after = bIgnoreData;
#ifdef __LINUX__
	write(pQueue->pipe[1], &cmd, sizeof(cmd));
#elif defined(WIN32)
	{ DWORD dwWritten; WriteFile(pQueue->pipe[1], &cmd, sizeof(cmd), &dwWritten, NULL); }
#endif
}

TIMERQUEUE *TimerQueueCreate()
{
	TIMERQUEUE *pQueue = (TIMERQUEUE*)malloc(sizeof(TIMERQUEUE));
	PA_PipeCreate(&pQueue->pipe[0], &pQueue->pipe[1]);
	pQueue->hThread = PA_ThreadCreate(TimerQueueThread, pQueue);
	return pQueue;
}

void TimerQueueDestroy(TIMERQUEUE *pQueue)
{
	TimerQueueQueueItem(pQueue, NULL, 0, 0, NULL);
	PA_ThreadWaitUntilTerminate(pQueue->hThread);
	PA_ThreadCloseHandle(pQueue->hThread);
	dbg_msg("private service joined\n");
	PA_PipeClose(pQueue->pipe[0]);
	PA_PipeClose(pQueue->pipe[1]);
	free(pQueue);
}
