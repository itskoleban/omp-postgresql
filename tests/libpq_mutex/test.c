/* Compile with PostgreSQL's real pthread-win32.c and pthread-win32.h. */
#include <errno.h>
#include <stdio.h>
#include <windows.h>

#include "pthread-win32.h"

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static HANDLE contender_waiting, holder_locked, contender_attempted, holder_unlocked;
static DWORD contender_id;
static LONG schedule_claim = 1;
static LONG entries;

static void check(int ok, const char *message) {
	if (!ok) {
		fprintf(stderr, "FAIL: %s\n", message);
		fflush(stderr);
		ExitProcess(1);
	}
}

static void wait_event(HANDLE event) {
	check(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "schedule deadline");
}

/* The contender has observed the uninitialized state, then pauses before claiming it.
 * The holder initializes and enters. The contender then attempts its stale claim while
 * the holder still owns the critical section. An unconditional exchange changes 1 to 2
 * and makes the holder's real pthread_mutex_unlock return EINVAL without releasing it.
 */
static int before_claim(LONG value) {
	if (GetCurrentThreadId() != contender_id || value != 2 ||
	    InterlockedCompareExchange(&schedule_claim, 0, 1) != 1) {
		return 0;
	}
	check(SetEvent(contender_waiting), "signal contender");
	wait_event(holder_locked);
	return 1;
}

static void after_claim(int scheduled) {
	if (scheduled) {
		check(SetEvent(contender_attempted), "signal claim");
		wait_event(holder_unlocked);
	}
}

LONG WINAPI pg_test_exchange(LONG volatile *address, LONG value) {
	int scheduled = before_claim(value);
	LONG previous = InterlockedExchange(address, value);
	after_claim(scheduled);
	return previous;
}

LONG WINAPI pg_test_compare_exchange(LONG volatile *address, LONG value, LONG expected) {
	int scheduled = before_claim(value);
	LONG previous = InterlockedCompareExchange(address, value, expected);
	after_claim(scheduled);
	return previous;
}

static DWORD WINAPI contender(void *unused) {
	(void)unused;
	contender_id = GetCurrentThreadId();
	check(pthread_mutex_lock(&mutex) == 0, "contender lock");
	check(mutex.initstate == 1, "initialized state remains published");
	++entries;
	check(pthread_mutex_unlock(&mutex) == 0, "contender unlock");
	return 0;
}

static DWORD WINAPI contend(void *unused) {
	int index;
	(void)unused;
	for (index = 0; index < 10000; ++index) {
		check(pthread_mutex_lock(&mutex) == 0, "repeated lock");
		++entries;
		check(pthread_mutex_unlock(&mutex) == 0, "repeated unlock");
	}
	return 0;
}

int main(void) {
	HANDLE thread, threads[8];
	pthread_mutex_t uninitialized = PTHREAD_MUTEX_INITIALIZER;
	int index;
	check(pthread_mutex_unlock(&uninitialized) == EINVAL, "uninitialized unlock rejected");
	contender_waiting = CreateEventW(NULL, TRUE, FALSE, NULL);
	holder_locked = CreateEventW(NULL, TRUE, FALSE, NULL);
	contender_attempted = CreateEventW(NULL, TRUE, FALSE, NULL);
	holder_unlocked = CreateEventW(NULL, TRUE, FALSE, NULL);
	check(contender_waiting && holder_locked && contender_attempted && holder_unlocked,
	      "create schedule events");
	thread = CreateThread(NULL, 0, contender, NULL, 0, NULL);
	check(thread != NULL, "create contender");
	wait_event(contender_waiting);
	check(pthread_mutex_lock(&mutex) == 0, "holder lock");
	check(SetEvent(holder_locked), "signal holder");
	wait_event(contender_attempted);
	check(pthread_mutex_unlock(&mutex) == 0, "holder unlock after stale initialization claim");
	check(SetEvent(holder_unlocked), "signal unlock");
	wait_event(thread);
	CloseHandle(thread);
	check(entries == 1 && mutex.initstate == 1, "contender entered initialized mutex");
	for (index = 0; index < 8; ++index) {
		threads[index] = CreateThread(NULL, 0, contend, NULL, 0, NULL);
		check(threads[index] != NULL, "create contention worker");
	}
	for (index = 0; index < 8; ++index) {
		wait_event(threads[index]);
		CloseHandle(threads[index]);
	}
	check(entries == 80001, "mutual exclusion retains all increments");
	DeleteCriticalSection(&mutex.csection);
	CloseHandle(contender_waiting);
	CloseHandle(holder_locked);
	CloseHandle(contender_attempted);
	CloseHandle(holder_unlocked);
	puts("libpq Windows mutex: deterministic stale claim and 80000 contended locks passed");
	return 0;
}
