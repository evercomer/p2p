#ifndef __RWLock_h__
#define __RWLock_h__

#include "platform_adpt.h"

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------------------

#ifdef WIN32

typedef struct ReadWriteLock
{
	int    m_currentLevel;
	int    m_readerCount, m_writeCount;
	DWORD  m_writerId;
	HANDLE m_unlockEvent; 
	HANDLE m_accessMutex;
	CRITICAL_SECTION m_csStateChange;
} RWLOCK, *LPRWLOCK;

RWLOCK *RWLockCreate();
BOOL RWLockDestroy(RWLOCK *pLock);
BOOL RWLockLockR(RWLOCK *pLock, DWORD timeout);
BOOL RWLockLockW(RWLOCK *pLock, DWORD timeout);
void RWLockUnlock(RWLOCK *pLock);

#define PA_RWLOCK	LPRWLOCK
#define PA_RWLockInit(x) x = RWLockCreate()
#define PA_RWLockUninit(x) RWLockDestroy(x)
//BOOL PA_RWLockLockR(x);
#define PA_RWLockLockR(x) RWLockLockR(x, INFINITE)
#define PA_RWLockLockW(x) RWLockLockW(x, INFINITE)
#define PA_RWLockLockRTimed(x, timeout) RWLockLockR(x, timeout)
#define PA_RWLockLockWTimed(x, timeout) RWLockLockW(x, timeout)
#define PA_RWLockUnlock(x) RWLockUnlock(x)

//End of WIN32
#elif defined(__LINUX__)

#define INFINITE 0xFFFFFFFF

#define PA_RWLOCK	pthread_rwlock_t	
#define PA_RWLockInit(x) pthread_rwlock_init(&x, NULL)
#define PA_RWLockUninit(x) pthread_rwlock_destroy(&x)
BOOL _RWLockLockR(PA_RWLOCK *x, DWORD timeout);
BOOL _RWLockLockW(PA_RWLOCK *x, DWORD timeout);
#define PA_RWLockLockR(x) (pthread_rwlock_rdlock(&x)==0)
#define PA_RWLockLockW(x) (pthread_rwlock_wrlock(&x)==0)
#define PA_RWLockLockRTimed(x, timeout) _RWLockLockR(&x, timeout)
#define PA_RWLockLockWTimed(x, timeout) _RWLockLockR(&x, timeout)
#define PA_RWLockUnlock(x) pthread_rwlock_unlock(&x)

#else //end of __LINUX__

#error "A proper platform must be specified !"

#endif

//-----------------------------------------------------------
#ifdef __cplusplus
}
#endif

#endif
