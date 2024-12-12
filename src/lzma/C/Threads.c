/* Threads.c -- multithreading library
   2024-03-28 : Igor Pavlov : Public domain
   2023-07-02 : Peter Hyman (lrzip-next) - remove _WIN32 cruft */

#include "Precomp.h"

// ---------- POSIX ----------

#if defined(__linux__) && !defined(__APPLE__) && !defined(_AIX) && !defined(__ANDROID__)
#ifndef Z7_AFFINITY_DISABLE
// _GNU_SOURCE can be required for pthread_setaffinity_np() / CPU_ZERO / CPU_SET
// clang < 3.6       : unknown warning group '-Wreserved-id-macro'
// clang 3.6 - 12.01 : gives warning "macro name is a reserved identifier"
// clang >= 13       : do not give warning
#if !defined(_GNU_SOURCE)
Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER
#define _GNU_SOURCE
Z7_DIAGNOSTIC_IGNORE_END_RESERVED_MACRO_IDENTIFIER
#endif // !defined(_GNU_SOURCE)
#endif // Z7_AFFINITY_DISABLE
#endif // __linux__

#include "Threads.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef Z7_AFFINITY_SUPPORTED
// #include <sched.h>
#endif


// #include <stdio.h>
// #define PRF(p) p
#define PRF(p)
#define Print(s) PRF(printf("\n%s\n", s);)

WRes Thread_Create_With_CpuSet(CThread *p, THREAD_FUNC_TYPE func, LPVOID param, const CCpuSet *cpuSet)
{
	// new thread in Posix probably inherits affinity from parrent thread
	Print("Thread_Create_With_CpuSet")

		pthread_attr_t attr;
	int ret;
	// int ret2;

	p->_created = 0;

	RINOK(pthread_attr_init(&attr))

		ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	if (!ret)
	{
		if (cpuSet)
		{
      // pthread_attr_setaffinity_np() is not supported for MUSL compile.
      // so we check for __GLIBC__ here
#if defined(Z7_AFFINITY_SUPPORTED) && defined( __GLIBC__)
			/*
			   printf("\n affinity :");
			   unsigned i;
			   for (i = 0; i < sizeof(*cpuSet) && i < 8; i++)
			   {
			   Byte b = *((const Byte *)cpuSet + i);
			   char temp[32];
#define GET_HEX_CHAR(t) ((char)(((t < 10) ? ('0' + t) : ('A' + (t - 10)))))
temp[0] = GET_HEX_CHAR((b & 0xF));
temp[1] = GET_HEX_CHAR((b >> 4));
			// temp[0] = GET_HEX_CHAR((b >> 4));  // big-endian
			// temp[1] = GET_HEX_CHAR((b & 0xF));  // big-endian
			temp[2] = 0;
			printf("%s", temp);
			}
			printf("\n");
			*/

			// ret2 =
			pthread_attr_setaffinity_np(&attr, sizeof(*cpuSet), cpuSet);
			// if (ret2) ret = ret2;
#endif
		}

		ret = pthread_create(&p->_tid, &attr, func, param);

		if (!ret)
		{
			p->_created = 1;
			/*
			   if (cpuSet)
			   {
			// ret2 =
			pthread_setaffinity_np(p->_tid, sizeof(*cpuSet), cpuSet);
			// if (ret2) ret = ret2;
			}
			*/
		}
	}
	// ret2 =
	pthread_attr_destroy(&attr);
	// if (ret2 != 0) ret = ret2;
	return ret;
}


WRes Thread_Create(CThread *p, THREAD_FUNC_TYPE func, LPVOID param)
{
	return Thread_Create_With_CpuSet(p, func, param, NULL);
}


WRes Thread_Create_With_Affinity(CThread *p, THREAD_FUNC_TYPE func, LPVOID param, CAffinityMask affinity)
{
	Print("Thread_Create_WithAffinity")
		CCpuSet cs;
	unsigned i;
	CpuSet_Zero(&cs);
	for (i = 0; i < sizeof(affinity) * 8; i++)
	{
		if (affinity == 0)
			break;
		if (affinity & 1)
		{
			CpuSet_Set(&cs, i);
		}
		affinity >>= 1;
	}
	return Thread_Create_With_CpuSet(p, func, param, &cs);
}


WRes Thread_Close(CThread *p)
{
	// Print("Thread_Close")
	int ret;
	if (!p->_created)
		return 0;

	ret = pthread_detach(p->_tid);
	p->_tid = 0;
	p->_created = 0;
	return ret;
}


WRes Thread_Wait_Close(CThread *p)
{
	// Print("Thread_Wait_Close")
	void *thread_return;
	int ret;
	if (!p->_created)
		return EINVAL;

	ret = pthread_join(p->_tid, &thread_return);
	// probably we can't use that (_tid) after pthread_join(), so we close thread here
	p->_created = 0;
	p->_tid = 0;
	return ret;
}



static WRes Event_Create(CEvent *p, int manualReset, int signaled)
{
	RINOK(pthread_mutex_init(&p->_mutex, NULL))
		RINOK(pthread_cond_init(&p->_cond, NULL))
		p->_manual_reset = manualReset;
	p->_state = (signaled ? True : False);
	p->_created = 1;
	return 0;
}

WRes ManualResetEvent_Create(CManualResetEvent *p, int signaled)
{ return Event_Create(p, True, signaled); }
WRes ManualResetEvent_CreateNotSignaled(CManualResetEvent *p)
{ return ManualResetEvent_Create(p, 0); }
WRes AutoResetEvent_Create(CAutoResetEvent *p, int signaled)
{ return Event_Create(p, False, signaled); }
WRes AutoResetEvent_CreateNotSignaled(CAutoResetEvent *p)
{ return AutoResetEvent_Create(p, 0); }


#if defined(Z7_LLVM_CLANG_VERSION) && (__clang_major__ == 13)
// freebsd:
#pragma GCC diagnostic ignored "-Wthread-safety-analysis"
#endif

WRes Event_Set(CEvent *p)
{
	RINOK(pthread_mutex_lock(&p->_mutex))
		p->_state = True;
	{
	const int res1 = pthread_cond_broadcast(&p->_cond);
	const int res2 = pthread_mutex_unlock(&p->_mutex);
	return (res2 ? res2 : res1);
	}
}

WRes Event_Reset(CEvent *p)
{
	RINOK(pthread_mutex_lock(&p->_mutex))
		p->_state = False;
	return pthread_mutex_unlock(&p->_mutex);
}

WRes Event_Wait(CEvent *p)
{
	RINOK(pthread_mutex_lock(&p->_mutex))
		while (p->_state == False)
		{
			// ETIMEDOUT
			// ret =
			pthread_cond_wait(&p->_cond, &p->_mutex);
			// if (ret != 0) break;
		}
	if (p->_manual_reset == False)
	{
		p->_state = False;
	}
	return pthread_mutex_unlock(&p->_mutex);
}

WRes Event_Close(CEvent *p)
{
	if (!p->_created)
		return 0;
	p->_created = 0;
	{
    const int res1 = pthread_mutex_destroy(&p->_mutex);
    const int res2 = pthread_cond_destroy(&p->_cond);
		return (res1 ? res1 : res2);
	}
}


WRes Semaphore_Create(CSemaphore *p, UInt32 initCount, UInt32 maxCount)
{
	if (initCount > maxCount || maxCount < 1)
		return EINVAL;
	RINOK(pthread_mutex_init(&p->_mutex, NULL))
		RINOK(pthread_cond_init(&p->_cond, NULL))
		p->_count = initCount;
	p->_maxCount = maxCount;
	p->_created = 1;
	return 0;
}


WRes Semaphore_OptCreateInit(CSemaphore *p, UInt32 initCount, UInt32 maxCount)
{
	if (Semaphore_IsCreated(p))
	{
		/*
		   WRes wres = Semaphore_Close(p);
		   if (wres != 0)
		   return wres;
		   */
		if (initCount > maxCount || maxCount < 1)
			return EINVAL;
		// return EINVAL; // for debug
		p->_count = initCount;
		p->_maxCount = maxCount;
		return 0;
	}
	return Semaphore_Create(p, initCount, maxCount);
}


WRes Semaphore_ReleaseN(CSemaphore *p, UInt32 releaseCount)
{
	UInt32 newCount;
	int ret;

	if (releaseCount < 1)
		return EINVAL;

	RINOK(pthread_mutex_lock(&p->_mutex))

		newCount = p->_count + releaseCount;
	if (newCount > p->_maxCount)
		ret = ERROR_TOO_MANY_POSTS; // EINVAL;
	else
	{
		p->_count = newCount;
		ret = pthread_cond_broadcast(&p->_cond);
	}
	RINOK(pthread_mutex_unlock(&p->_mutex))
		return ret;
}

WRes Semaphore_Wait(CSemaphore *p)
{
	RINOK(pthread_mutex_lock(&p->_mutex))
		while (p->_count < 1)
		{
			pthread_cond_wait(&p->_cond, &p->_mutex);
		}
	p->_count--;
	return pthread_mutex_unlock(&p->_mutex);
}

WRes Semaphore_Close(CSemaphore *p)
{
	if (!p->_created)
		return 0;
	p->_created = 0;
	{
    const int res1 = pthread_mutex_destroy(&p->_mutex);
    const int res2 = pthread_cond_destroy(&p->_cond);
		return (res1 ? res1 : res2);
	}
}



WRes CriticalSection_Init(CCriticalSection *p)
{
	// Print("CriticalSection_Init")
	if (!p)
		return EINTR;
	return pthread_mutex_init(&p->_mutex, NULL);
}

void CriticalSection_Enter(CCriticalSection *p)
{
	// Print("CriticalSection_Enter")
	if (p)
	{
		// int ret =
		pthread_mutex_lock(&p->_mutex);
	}
}

void CriticalSection_Leave(CCriticalSection *p)
{
	// Print("CriticalSection_Leave")
	if (p)
	{
		// int ret =
		pthread_mutex_unlock(&p->_mutex);
	}
}

void CriticalSection_Delete(CCriticalSection *p)
{
	// Print("CriticalSection_Delete")
	if (p)
	{
		// int ret =
		pthread_mutex_destroy(&p->_mutex);
	}
}

LONG InterlockedIncrement(LONG volatile *addend)
{
	// Print("InterlockedIncrement")
#ifdef USE_HACK_UNSAFE_ATOMIC
	LONG val = *addend + 1;
	*addend = val;
	return val;
#else

#if defined(__clang__) && (__clang_major__ >= 8)
#pragma GCC diagnostic ignored "-Watomic-implicit-seq-cst"
#endif
	return __sync_add_and_fetch(addend, 1);
#endif
}

LONG InterlockedDecrement(LONG volatile *addend)
{
  // Print("InterlockedDecrement")
  #ifdef USE_HACK_UNSAFE_ATOMIC
    LONG val = *addend - 1;
    *addend = val;
    return val;
  #else
    return __sync_sub_and_fetch(addend, 1);
  #endif
}

WRes AutoResetEvent_OptCreate_And_Reset(CAutoResetEvent *p)
{
	if (Event_IsCreated(p))
		return Event_Reset(p);
	return AutoResetEvent_CreateNotSignaled(p);
}

#undef PRF
#undef Print
