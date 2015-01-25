#include "rwlock.h"

#ifdef WIN32
enum { LOCK_LEVEL_NONE, LOCK_LEVEL_READ, LOCK_LEVEL_WRITE };

RWLOCK *RWLockCreate()
{
	RWLOCK *pLock = (RWLOCK)malloc(sizeof(RWLOCK));
	pLock->m_currentLevel = LOCK_LEVEL_NONE;
	pLock->m_readerCount    =  pLock->m_writeCount = 0;
	pLock->m_writerId = 0;
	//pLock->m_unlockEvent  = CreateEvent( NULL, TRUE, FALSE, NULL );
	pLock->m_unlockEvent  = CreateEvent( NULL, FALSE, FALSE, NULL );
	pLock->m_accessMutex  = CreateMutex( NULL, FALSE, NULL );
	InitializeCriticalSection( &pLock->m_csStateChange );
}

BOOL RWLockDestroy(RWLOCK *pLock)
{
	DeleteCriticalSection( &pLock->m_csStateChange );
	if ( pLock->m_accessMutex ) CloseHandle( pLock->m_accessMutex );
	if ( pLock->m_unlockEvent ) CloseHandle( pLock->m_unlockEvent );
	free(pLock);
}

static BOOL _Lock(RWLOCK *pLock, int level, DWORD timeout) 
{
	BOOL  bresult    = true;
	DWORD waitResult = 0;

	waitResult = WaitForSingleObject( m_accessMutex, timeout );
	if ( waitResult != WAIT_OBJECT_0 )  return false;

	if ( level == LOCK_LEVEL_READ && m_currentLevel != LOCK_LEVEL_WRITE )
	{
		EnterCriticalSection( &m_csStateChange );
		m_currentLevel = level;
		m_readerCount += 1;
		ResetEvent( m_unlockEvent );
		LeaveCriticalSection( &m_csStateChange );
	}
	else if ( level == LOCK_LEVEL_READ && 
			m_currentLevel == LOCK_LEVEL_WRITE )
	{
		waitResult = WaitForSingleObject( m_unlockEvent, timeout );
		if ( waitResult == WAIT_OBJECT_0 )
		{
			EnterCriticalSection( &m_csStateChange );
			m_currentLevel = level;
			m_readerCount += 1;
			ResetEvent( m_unlockEvent );
			LeaveCriticalSection( &m_csStateChange );
		}
		else bresult = false;
	}
	else if ( level == LOCK_LEVEL_WRITE && 
			m_currentLevel == LOCK_LEVEL_NONE )
	{
		EnterCriticalSection( &m_csStateChange );
		m_currentLevel = level;
		m_writerId = GetCurrentThreadId();
		m_writeCount = 1;
		ResetEvent( m_unlockEvent );
		LeaveCriticalSection( &m_csStateChange );
	}
	else if ( level == LOCK_LEVEL_WRITE && 
			m_currentLevel != LOCK_LEVEL_NONE )
	{
		DWORD id = GetCurrentThreadId();
		if(id == m_writerId) m_writeCount++;
		else
		{
			waitResult = WaitForSingleObject( m_unlockEvent, timeout );
			if ( waitResult == WAIT_OBJECT_0 )
			{
				EnterCriticalSection( &m_csStateChange );
				m_currentLevel = level;
				m_writerId = GetCurrentThreadId();
				m_writeCount = 1;
				ResetEvent( m_unlockEvent );
				LeaveCriticalSection( &m_csStateChange );
			}
			else bresult = false;
		}
	}

	ReleaseMutex( m_accessMutex );
	return bresult;

} // lock()

BOOL RWLockLockR(RWLOCK *pLock, DWORD timeout)
{
	return _Lock(pLock, LOCK_LEVEL_READ, timeout);
}

BOOL RWLockLockW(RWLOCK *pLock, DWORD timeout)
{
       return _Lock(pLock, LOCK_LEVEL_WRITE, timeout); 
}

void RWLockUnlock(RWLOCK *pLock)
{ 
	EnterCriticalSection( &pLock->m_csStateChange );
	if ( pLock->m_currentLevel == LOCK_LEVEL_READ )
	{
		pLock->m_readerCount --;
		if ( pLock->m_readerCount == 0 ) 
		{
			pLock->m_currentLevel = LOCK_LEVEL_NONE;
			SetEvent (pLock->m_unlockEvent);
		}
	}
	else if ( pLock->m_currentLevel == LOCK_LEVEL_WRITE )
	{
		pLock->m_writeCount--;
		if(pLock->m_writeCount == 0)
		{
			pLock->m_currentLevel = LOCK_LEVEL_NONE;
			pLock->m_writerId = 0;
			SetEvent ( pLock->m_unlockEvent );
		}
	}
	LeaveCriticalSection( &pLock->m_csStateChange );
}

#elif defined(__LINUX__)

BOOL _RWLockLockR(pthread_rwlock_t *lock, DWORD timeout)
{
	if(timeout == INFINITE)
		return pthread_rwlock_rdlock(lock) == 0;
	else
	{
		while(!pthread_rwlock_tryrdlock(lock) && timeout > 0)
		{
			usleep(10000);
			if(timeout > 10) timeout -= 10;
			else return FALSE;
		}
		return TRUE;
	}
}
BOOL _RWLockLockW(pthread_rwlock_t *lock, DWORD timeout)
{
	if(timeout == INFINITE)
		return pthread_rwlock_wrlock(lock)==0;
	else
	{
		while(!pthread_rwlock_trywrlock(lock) && timeout > 0)
		{
			usleep(10000);
			if(timeout > 10) timeout -= 10;
			else return FALSE;
		}
		return TRUE;
	}
}

#endif
