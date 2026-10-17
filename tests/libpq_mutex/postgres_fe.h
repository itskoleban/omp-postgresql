/* Minimal frontend preamble for compiling the actual libpq mutex source in isolation.
 * Windows primitives remain real; hooks only control the first-initialization schedule.
 */
#pragma once
#include <errno.h>
#include <windows.h>

LONG WINAPI pg_test_exchange(LONG volatile *address, LONG value);
LONG WINAPI pg_test_compare_exchange(LONG volatile *address, LONG value, LONG expected);

#undef InterlockedExchange
#undef InterlockedCompareExchange
#define InterlockedExchange pg_test_exchange
#define InterlockedCompareExchange pg_test_compare_exchange
