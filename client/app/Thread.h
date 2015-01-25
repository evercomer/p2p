#include "platform_adpt.h"
#include <vector>
#include "RWLock.h"

class CThread {
friend PA_ThreadRoutine _ThreadHandler;
public:
	CThread();
	virtual ~CThread();
	
	void Start();
	void Stop();
	void Pause();
	void Resume();
	
	typedef enum { THREADSTATE_STOPPED,  THREADSTATE_RUNNING, THREADSTATE_PAUSED } THREAD_STATE;
	typedef enum { CMD_INITIALIZE, CMD_RUN, CMD_PAUSE, CMD_CONTINUE, CMD_STOP, CMD_USER, CMD_NONE } THREAD_COMMAND;
	
	void PostCommand(THREAD_COMMAND cmd);
	void SendCommand(THREAD_COMMAND cmd, PA_EVENT hEvent);
	THREAD_STATE GetActivityStatus() { return m_eState; }
	
protected:
	struct CmdNode {
		UINT cmd;
		PA_EVENT hEvent;
	};
	std::vector<struct CmdNode> m_vCmdQueue;
	CMutexLock	m_cqLock;
		
	BOOL	m_bThreadCreated, m_bNoCommand;
	THREAD_STATE m_eState;
	PA_HTHREAD m_hThread;
	
	virtual void CommandHandler(UINT cmd) = 0;
};
