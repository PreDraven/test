#ifndef UDPCTHREADS_H
#define UDPCTHREADS_H

#ifdef __MINGW32__

#include "socklib.h"
#include <errno.h>
#include <winsock2.h>
#include <winbase.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
typedef HANDLE pthread_t;
typedef CRITICAL_SECTION pthread_mutex_t;
typedef HANDLE pthread_cond_t;

static inline int pthread_create(pthread_t *thread, void *dummy1,
				 LPTHREAD_START_ROUTINE start_routine, 
				 void *arg) {
  /* Start thread ... 
     * see http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dllproc/base/createthread.asp
     */	       
    *thread = CreateThread(NULL,	/* lpThreadAttributes */
			   0,	/* dwStackSize */
			   start_routine,
			   arg,	/* lpParameter */
			   0,	/* dwCreationFlags */
			   NULL    /* lpThreadId */);
    return *thread != NULL ? 0 : -1;
}

static inline int pthread_join(pthread_t th, void **thread_return) {
  return WaitForSingleObject(th, INFINITE) == WAIT_OBJECT_0 ? 0 : -1;
}

static inline int pthread_mutex_init(pthread_mutex_t  *mutex, void *dummy) {
  InitializeCriticalSection(mutex);
  return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t  *mutex) {
  EnterCriticalSection(mutex);
  return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t  *mutex) {
  LeaveCriticalSection(mutex);
  return 0;
}


static inline int pthread_cond_init(pthread_cond_t  *cond, void *dummy) {
  *cond = CreateEvent(NULL, TRUE, TRUE, NULL);
    if(*cond == NULL)
      return -1;
    else
      return 0;
}

static inline int pthread_cond_signal(pthread_cond_t  *cond) {
  return SetEvent(*cond) ? 0 : -1;
}

static inline int pthread_cond_wait(pthread_cond_t  *cond, 
				    pthread_mutex_t *mutex) {
  int r;
  ResetEvent(*cond);
  LeaveCriticalSection(mutex);
  r= WaitForSingleObject(*cond, INFINITE) == WAIT_OBJECT_0 ? 0 : -1;
  EnterCriticalSection(mutex);
  return r;
}

static inline void pthread_cancel(pthread_t *thread)
{
  TerminateThread(thread, 0);
}

#define MILLION    1000000
#define BILLION 1000000000

static inline int pthread_cond_timedwait(pthread_cond_t  *cond, 
					 pthread_mutex_t *mutex,
					 struct timespec *ts) {
  int r;
  struct timeval tv;
  long delta;

  gettimeofday(&tv, NULL);

  delta = (ts->tv_sec - tv.tv_sec) * 1000 + 
    (ts->tv_nsec / BILLION - tv.tv_usec / MILLION);
  if(delta < 0)
    delta = 0;

  ResetEvent(*cond);
  LeaveCriticalSection(mutex);
  
  switch(WaitForSingleObject(*cond, delta )) {
  case WAIT_OBJECT_0:
    r=0;
    break;
  case WAIT_TIMEOUT:
    r=ETIMEDOUT;
    break;
  default:
    r=-1;
    break;
  }
  EnterCriticalSection(mutex);
  return  r;
}

#define THREAD_RETURN DWORD WINAPI


#else /* __MINGW32__ */
#include <pthread.h>
#define THREAD_RETURN void *
#endif /* __MINGW32__ */

#endif
