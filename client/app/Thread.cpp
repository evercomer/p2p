#include "Thread.h"

void* _ThreadHandler(void *pData)
{
	CThread *pThread = (CThread*)pData;
	
	pThread->m_eState = CThread::THREADSTATE_RUNNING;
	while(1)
	{
		struct CThread::CmdNode node;
		pThread->m_cqLock.Lock();
		if(pThread->m_vCmdQueue.size())
		{
			node = pThread->m_vCmdQueue.at(0);
			pThread->m_vCmdQueue.erase(pThread->m_vCmdQueue.begin());
			pThread->m_cqLock.Unlock();
			
			switch(node.cmd)
			{
			case CThread::CMD_RUN:
				pThread->m_eState = CThread::THREADSTATE_RUNNING;
				break;
			case CThread::CMD_PAUSE:
				pThread->m_eState = CThread::THREADSTATE_PAUSED;
				break;
			case CThread::CMD_CONTINUE:
				pThread->m_eState = CThread::THREADSTATE_RUNNING;
				break;
			}
		}
		else
		{
			pThread->m_cqLock.Unlock();
			node.cmd = CThread::CMD_NONE;
			node.hEvent = NULL;
		}
		pThread->CommandHandler(node.cmd);
		if(node.hEvent) PA_EventSet(node.hEvent);
		if(node.cmd == CThread::CMD_STOP)
			break;
	}
	pThread->m_eState = CThread::THREADSTATE_STOPPED;
	return NULL;
}

CThread::CThread() : m_bThreadCreated(FALSE), m_eState(THREADSTATE_STOPPED), m_bNoCommand(FALSE)
{
}

CThread::~CThread()
{
	if(m_bThreadCreated)
		Stop();
}

void CThread::Start()
{
	if(!m_bThreadCreated)
	{
		m_hThread = PA_ThreadCreate(_ThreadHandler, this);
		m_bThreadCreated = TRUE;
	}
	PostCommand(CMD_INITIALIZE);
}

void CThread::Stop()
{
	PostCommand(CThread::CMD_STOP);
	PA_ThreadWaitUntilTerminate(m_hThread);
	PA_ThreadCloseHandle(m_hThread);
	m_eState = THREADSTATE_STOPPED;

	m_bThreadCreated = FALSE;	
}

void CThread::Pause()
{
	if(m_eState == CThread::THREADSTATE_RUNNING)
	{
		PA_EVENT hEvent;
	       	PA_EventInit(hEvent);
		SendCommand(CThread::CMD_PAUSE, hEvent);
		PA_EventUninit(hEvent);
		//m_eState = THREADSTATE_PAUSED;
	}
}

void CThread::Resume()
{
	if(m_eState == CThread::THREADSTATE_PAUSED)
	{
		PA_EVENT hEvent;
	        PA_EventInit(hEvent);
		SendCommand(CThread::CMD_CONTINUE, hEvent);
		PA_EventUninit(hEvent);
		//m_eState = THREADSTATE_RUNNING;
	}
}

void CThread::PostCommand(THREAD_COMMAND cmd)
{
	if(m_bNoCommand) return;
	struct CmdNode node = { cmd, NULL };
	m_cqLock.Lock();
	m_vCmdQueue.push_back(node);
	if(cmd == CMD_STOP)
		m_bNoCommand = TRUE;
	m_cqLock.Unlock();
}

void CThread::SendCommand(THREAD_COMMAND cmd, PA_EVENT hEvent)
{
	if(m_bNoCommand) return;
	struct CmdNode node = { cmd, hEvent };
	m_cqLock.Lock();
	m_vCmdQueue.push_back(node);
	if(cmd == CMD_STOP)
		m_bNoCommand = TRUE;
	m_cqLock.Unlock();
	PA_EventWait(hEvent);
}
