/* Threads.h -- multithreading library
2021-12-21 : Igor Pavlov : Public domain 
2022-06-21 : Peter Hyman (lrzip-next, remove WIN32 #ifdefs */

#ifndef __7Z_THREADS_H
#define __7Z_THREADS_H

#if defined(__linux__)
#if !defined(__APPLE__) && !defined(_AIX) && !defined(__ANDROID__)
#ifndef _7ZIP_AFFINITY_DISABLE
#define _7ZIP_AFFINITY_SUPPORTED
// #pragma message(" ==== _7ZIP_AFFINITY_SUPPORTED")
// #define _GNU_SOURCE
#endif
#endif
#endif

#include <pthread.h>

#include "7zTypes.h"

EXTERN_C_BEGIN

typedef struct _CThread
{
  pthread_t _tid;
  int _created;
} CThread;

#define Thread_Construct(p) { (p)->_tid = 0; (p)->_created = 0; }
#define Thread_WasCreated(p) ((p)->_created != 0)
WRes Thread_Close(CThread *p);
// #define Thread_Wait Thread_Wait_Close

typedef void * THREAD_FUNC_RET_TYPE;

typedef UInt64 CAffinityMask;

#ifdef _7ZIP_AFFINITY_SUPPORTED

typedef cpu_set_t CCpuSet;
#define CpuSet_Zero(p) CPU_ZERO(p)
#define CpuSet_Set(p, cpu) CPU_SET(cpu, p)
#define CpuSet_IsSet(p, cpu) CPU_ISSET(cpu, p)

#else

typedef UInt64 CCpuSet;
#define CpuSet_Zero(p) { *(p) = 0; }
#define CpuSet_Set(p, cpu) { *(p) |= ((UInt64)1 << (cpu)); }
#define CpuSet_IsSet(p, cpu) ((*(p) & ((UInt64)1 << (cpu))) != 0)

#endif

#define THREAD_FUNC_CALL_TYPE MY_STD_CALL

#define THREAD_FUNC_ATTRIB_ALIGN_ARG

#define THREAD_FUNC_DECL  THREAD_FUNC_ATTRIB_ALIGN_ARG THREAD_FUNC_RET_TYPE THREAD_FUNC_CALL_TYPE

typedef THREAD_FUNC_RET_TYPE (THREAD_FUNC_CALL_TYPE * THREAD_FUNC_TYPE)(void *);
WRes Thread_Create(CThread *p, THREAD_FUNC_TYPE func, LPVOID param);
WRes Thread_Create_With_Affinity(CThread *p, THREAD_FUNC_TYPE func, LPVOID param, CAffinityMask affinity);
WRes Thread_Wait_Close(CThread *p);

WRes Thread_Create_With_CpuSet(CThread *p, THREAD_FUNC_TYPE func, LPVOID param, const CCpuSet *cpuSet);

typedef struct _CEvent
{
  int _created;
  int _manual_reset;
  int _state;
  pthread_mutex_t _mutex;
  pthread_cond_t _cond;
} CEvent;

typedef CEvent CAutoResetEvent;
typedef CEvent CManualResetEvent;

#define Event_Construct(p) (p)->_created = 0
#define Event_IsCreated(p) ((p)->_created)

WRes ManualResetEvent_Create(CManualResetEvent *p, int signaled);
WRes ManualResetEvent_CreateNotSignaled(CManualResetEvent *p);
WRes AutoResetEvent_Create(CAutoResetEvent *p, int signaled);
WRes AutoResetEvent_CreateNotSignaled(CAutoResetEvent *p);
WRes Event_Set(CEvent *p);
WRes Event_Reset(CEvent *p);
WRes Event_Wait(CEvent *p);
WRes Event_Close(CEvent *p);


typedef struct _CSemaphore
{
  int _created;
  UInt32 _count;
  UInt32 _maxCount;
  pthread_mutex_t _mutex;
  pthread_cond_t _cond;
} CSemaphore;

#define Semaphore_Construct(p) (p)->_created = 0
#define Semaphore_IsCreated(p) ((p)->_created)

WRes Semaphore_Create(CSemaphore *p, UInt32 initCount, UInt32 maxCount);
WRes Semaphore_OptCreateInit(CSemaphore *p, UInt32 initCount, UInt32 maxCount);
WRes Semaphore_ReleaseN(CSemaphore *p, UInt32 num);
#define Semaphore_Release1(p) Semaphore_ReleaseN(p, 1)
WRes Semaphore_Wait(CSemaphore *p);
WRes Semaphore_Close(CSemaphore *p);


typedef struct _CCriticalSection
{
  pthread_mutex_t _mutex;
} CCriticalSection;

WRes CriticalSection_Init(CCriticalSection *p);
void CriticalSection_Delete(CCriticalSection *cs);
void CriticalSection_Enter(CCriticalSection *cs);
void CriticalSection_Leave(CCriticalSection *cs);

LONG InterlockedIncrement(LONG volatile *addend);

EXTERN_C_END

#endif
