/* LzFindOpt.c -- multithreaded Match finder for LZ algorithms
   2023-04-02 : Igor Pavlov : Public domain
   2023-07-02 : Peter Hyman (lrzip-next) -- remove large comment blocks
   for readability */


#include "Precomp.h"

#include "CpuArch.h"
#include "LzFind.h"

// #include "LzFindMt.h"

// #define LOG_ITERS

// #define LOG_THREAD

#ifdef LOG_THREAD
#include <stdio.h>
#define PRF(x) x
#else
// #define PRF(x)
#endif

#ifdef LOG_ITERS
#include <stdio.h>
UInt64 g_NumIters_Tree;
UInt64 g_NumIters_Loop;
UInt64 g_NumIters_Bytes;
#define LOG_ITER(x) x
#else
#define LOG_ITER(x)
#endif

// ---------- BT THREAD ----------

#define USE_SON_PREFETCH
#define USE_LONG_MATCH_OPT

#define kEmptyHashValue 0

// #define CYC_TO_POS_OFFSET 0

// #define CYC_TO_POS_OFFSET 1 // for debug

/* define cbs if you use 2 functions.
   GetMatchesSpecN_1() :  (pos <  _cyclicBufferSize)
   GetMatchesSpecN_2() :  (pos >= _cyclicBufferSize)

   do not define cbs if you use 1 function:
   GetMatchesSpecN_2()
   */

// #define cbs _cyclicBufferSize

/*
   we use size_t for (pos) and (_cyclicBufferPos_ instead of UInt32
   to eliminate "movsx" BUG in old MSVC x64 compiler.
   */

UInt32 * Z7_FASTCALL GetMatchesSpecN_2(const Byte *lenLimit, size_t pos, const Byte *cur, CLzRef *son,
		UInt32 _cutValue, UInt32 *d, size_t _maxLen, const UInt32 *hash, const UInt32 *limit, const UInt32 *size,
		size_t _cyclicBufferPos, UInt32 _cyclicBufferSize,
		UInt32 *posRes);

Z7_NO_INLINE
UInt32 * Z7_FASTCALL GetMatchesSpecN_2(const Byte *lenLimit, size_t pos, const Byte *cur, CLzRef *son,
		UInt32 _cutValue, UInt32 *d, size_t _maxLen, const UInt32 *hash, const UInt32 *limit, const UInt32 *size,
		size_t _cyclicBufferPos, UInt32 _cyclicBufferSize,
		UInt32 *posRes)
{
	do // while (hash != size)
	{
		UInt32 delta;

#ifndef cbs
		UInt32 cbs;
#endif

		if (hash == size)
			break;

		delta = *hash++;

		if (delta == 0)
			return NULL;

		lenLimit++;

#ifndef cbs
		cbs = _cyclicBufferSize;
		if ((UInt32)pos < cbs)
		{
			if (delta > (UInt32)pos)
				return NULL;
			cbs = (UInt32)pos;
		}
#endif

		if (delta >= cbs)
		{
			CLzRef *ptr1 = son + ((size_t)_cyclicBufferPos << 1);
			*d++ = 0;
			ptr1[0] = kEmptyHashValue;
			ptr1[1] = kEmptyHashValue;
		}
		else
		{
			UInt32 *_distances = ++d;

			CLzRef *ptr0 = son + ((size_t)_cyclicBufferPos << 1) + 1;
			CLzRef *ptr1 = son + ((size_t)_cyclicBufferPos << 1);

			UInt32 cutValue = _cutValue;
			const Byte *len0 = cur, *len1 = cur;
			const Byte *maxLen = cur + _maxLen;

			// if (cutValue == 0) { *ptr0 = *ptr1 = kEmptyHashValue; } else
			for (LOG_ITER(g_NumIters_Tree++);;)
			{
				LOG_ITER(g_NumIters_Loop++);
				{
					// SPEC code
					CLzRef *pair = son + ((size_t)((ptrdiff_t)_cyclicBufferPos - (ptrdiff_t)delta
								+ (ptrdiff_t)(UInt32)(_cyclicBufferPos < delta ? cbs : 0)
								) << 1);

					const ptrdiff_t diff = (ptrdiff_t)0 - (ptrdiff_t)delta;
					const Byte *len = (len0 < len1 ? len0 : len1);

#ifdef USE_SON_PREFETCH
					const UInt32 pair0 = *pair;
#endif

					if (len[diff] == len[0])
					{
						if (++len != lenLimit && len[diff] == len[0])
							while (++len != lenLimit)
							{
								LOG_ITER(g_NumIters_Bytes++);
								if (len[diff] != len[0])
									break;
							}
						if (maxLen < len)
						{
							maxLen = len;
							*d++ = (UInt32)(len - cur);
							*d++ = delta - 1;

							if (len == lenLimit)
							{
								const UInt32 pair1 = pair[1];
								*ptr1 =
#ifdef USE_SON_PREFETCH
									pair0;
#else
								pair[0];
#endif
								*ptr0 = pair1;

								_distances[-1] = (UInt32)(d - _distances);

#ifdef USE_LONG_MATCH_OPT

								if (hash == size || *hash != delta || lenLimit[diff] != lenLimit[0] || d >= limit)
									break;

								{
									for (;;)
									{
										*d++ = 2;
										*d++ = (UInt32)(lenLimit - cur);
										*d++ = delta - 1;
										cur++;
										lenLimit++;
										// SPEC
										_cyclicBufferPos++;
										{
											// SPEC code
											CLzRef *dest = son + ((size_t)(_cyclicBufferPos) << 1);
											const CLzRef *src = dest + ((diff
														+ (ptrdiff_t)(UInt32)((_cyclicBufferPos < delta) ? cbs : 0)) << 1);
											// CLzRef *ptr = son + ((size_t)(pos) << 1) - CYC_TO_POS_OFFSET * 2;
#if 0
											*(UInt64 *)(void *)dest = *((const UInt64 *)(const void *)src);
#else
											const UInt32 p0 = src[0];
											const UInt32 p1 = src[1];
											dest[0] = p0;
											dest[1] = p1;
#endif
										}
										pos++;
										hash++;
										if (hash == size || *hash != delta || lenLimit[diff] != lenLimit[0] || d >= limit)
											break;
									} // for() end for long matches
								}
#endif

								break; // break from TREE iterations
							}
						}
					}
					{
						const UInt32 curMatch = (UInt32)pos - delta; // (UInt32)(pos + diff);
						if (len[diff] < len[0])
						{
							delta = pair[1];
							*ptr1 = curMatch;
							ptr1 = pair + 1;
							len1 = len;
							if (delta >= curMatch)
								return NULL;
						}
						else
						{
							delta = *pair;
							*ptr0 = curMatch;
							ptr0 = pair;
							len0 = len;
							if (delta >= curMatch)
								return NULL;
						}
						delta = (UInt32)pos - delta;

						if (--cutValue == 0 || delta >= cbs)
						{
							*ptr0 = *ptr1 = kEmptyHashValue;
							_distances[-1] = (UInt32)(d - _distances);
							break;
						}
					}
				}
			} // for (tree iterations)
		}
		pos++;
		_cyclicBufferPos++;
		cur++;
	}
	while (d < limit);
	*posRes = (UInt32)pos;
	return d;
}
