/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();

	while (sema->value == 0)
	{
		list_sort(&sema->waiters, list_priority, NULL);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, list_priority, NULL);
		thread_block();
	}

	sema->value--;

	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters))
	{
		list_sort(&sema->waiters, list_priority, NULL);
		thread_unblock(list_entry(list_pop_front(&sema->waiters),
								  struct thread, elem));
	}
	sema->value++;
	intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{

	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	/*====원본 코드====
		// sema_down(&lock->semaphore);
		// lock->holder = thread_current();
		// 이하 아예 없었음
	*/
	enum intr_level old_level;

	struct thread *curr_thread = thread_current();

	// current thread -> lock가질 수 있다면
	// = (본인이 이미 가진 lock이 아니고, lock의 holder가 없다면)
	// lock 획득
	if (lock->holder == NULL)
	{
		// 내가 기다리는 lock 없애주기
		curr_thread->wait_on_lock = NULL;

		// 내가 lock의 주인이 되기
		lock->holder = curr_thread;

		// 나에게 donations 없는 경우 list 초기화
		if (curr_thread->donations == NULL)
		{
			list_init(&lock->holder->donations);
		}

		// semaphore 하나 내려주기
		sema_down(&lock->semaphore);
	}
	else
	{
		/*
			현재 lock->holder가 존재할 때, 기다리기
		 */

		// 시간 멈춰!
		old_level = intr_disable();

		// 이 lock을 내가 기다리는 것으로 기록
		curr_thread->wait_on_lock = lock;

		// lock_holder의 donations에 들어가기
		struct list *donations = lock->holder->donations;

		// donations 있으면 priority 순으로 정렬해주고
		if (list_size(donations))
		{
			printf("=======[LOCK ACQ] inside donation > 0========\n");
			list_sort(donations, list_priority, NULL);
			printf("=======[LOCK ACQ] after sort donation========\n");
		}

		// priority 순으로 나자신 들어가
		list_insert_ordered(donations, &curr_thread->d_elem, list_priority, NULL);
		printf("=======[LOCK ACQ] after insert curr into donations========\n");

		// lock_holder의 priority를 최상으로 올려줘
		struct thread *highest_prio_thread = list_entry(list_front(donations), struct thread, d_elem);
		if ((lock->holder->priority) < (highest_prio_thread->priority))
		{
			lock->holder->priority = highest_prio_thread->priority;

			// ready_list update 해서 lock_holder의 바뀐 우선순위가 ready_list에 반영되게... 어떻게 해주지..?
			// list_sort(&ready_list, list_priority, NULL);
		}

		// 나 자신은 lock 풀릴 때까지 기다려
		thread_block();

		// 시간 되살려!
		intr_set_level(old_level);
	}
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{

	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	// 시간 멈춰!
	enum intr_level old_level;
	old_level = intr_disable();

	struct thread *curr_thread = thread_current();
	struct list *donations = curr_thread->donations;

	// 나에게 donations 없는 경우
	if (list_empty(donations))
	{
		// 나의 원래 우선순위로 되돌아가기
		curr_thread->priority = curr_thread->own_priority;
	}
	else
	{
		// 나에게 donations 있는 경우

		// 다음 holder에게 물려줄 donations
		struct list *inherit_donations;
		list_init(inherit_donations);
		struct thread *dona_thread;

		// donations의 d_elem이 NULL이 아닐 때까지 탐색
		for (struct list_elem *dona_elem = list_front(donations); dona_elem != NULL;)
		{

			// 만약 d_elem의 wait_on_lock이 현재의 lock과 일치한다면
			dona_thread = list_entry(dona_elem, struct thread, d_elem);

			dona_elem = list_next(&dona_elem);
			if (dona_thread->wait_on_lock == lock)
			{
				list_insert_ordered(inherit_donations, dona_elem, list_priority, NULL);
				list_remove(dona_elem);
			}
		}

		// 다음 lock holder에게 물려줄 donations가 없다면
		if (list_empty(inherit_donations))
		{
			if (list_empty(donations))
			{
				// donations에 있던 모든 thread들이 해당 Lock에 딸린 것이었다면
				// curr_thread는 더이상 lock과 관련 없음
				// 본연으로 돌아가기
				curr_thread->priority = curr_thread->own_priority;
			}
			else
			{
				// 해당 lock이 아닌 다른 lock에 대한 donations가 있는 경우
				// 나머지 donations애들중 highest priority에 맞춰주기
				list_sort(donations, list_priority, NULL);
				struct thread *highest_prio_thread = list_entry(list_front(donations), struct thread, d_elem);
				curr_thread->priority = highest_prio_thread->priority;
			}
		}
		// 다음 lock holder에게 물려줄 donations가 하나밖에 없다면
		else if (list_size(inherit_donations) == 1)
		{
			// 하나 남은 그 thread가 바로 next holder
			struct thread *next_holder = list_entry(list_pop_front(inherit_donations), struct thread, d_elem);

			// next lock holder를 Ready list로 깨워서 올려주기
			thread_unblock(next_holder);
		}
		// 다음 lock holder에게 물려줄 donations가 하나 이상 있다면
		else if (list_size(inherit_donations > 1))
		{
			list_sort(inherit_donations, list_priority, NULL);
			struct thread *next_holder = list_entry(list_pop_front(inherit_donations), struct thread, d_elem);
			next_holder->donations = inherit_donations;
			thread_unblock(next_holder);
		}
	}

	// 맨 마지막
	lock->holder = NULL;

	// 시간 되살려!
	intr_set_level(old_level);

	// sema 하나 올려두고 퇴장
	sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	list_sort(&cond->waiters, list_priority, NULL);
	list_insert_ordered(&cond->waiters, &(waiter.elem), list_priority, NULL);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&(cond->waiters)))
	{
		list_sort(&(cond->waiters), list_priority, NULL);
		sema_up(&list_entry(list_pop_front(&(cond->waiters)),
							struct semaphore_elem, elem)
					 ->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&(cond->waiters)))
		cond_signal(cond, lock);
}
