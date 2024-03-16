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
	thread_try_yield();
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

	struct thread *curr_thread = thread_current();
	struct thread *lock_holder = lock->holder;

	/* lock_holder가 존재해서 당장 Lock을 획득할 수 없다면

		해당 lock_holder,
		그리고 혹시 그가 기다리는 다른 lock의 holder가 연쇄적으로 있다면 끝까지

		우선순위를 조정하여 업데이트 시켜주기
	*/
	if (lock_holder != NULL)
	{

		// 이 lock을 내가 기다리는 것으로 기록
		curr_thread->wait_on_lock = lock;

		// lock_holder의 donations에 priority 순으로 나자신 들어가기
		struct list *donations = &lock_holder->donations;
		list_insert_ordered(donations, &curr_thread->d_elem, list_priority, NULL);

		// lock_holder의 우선순위를 curr_thread 우선순위와 비교하여 업데이트
		// 만약 chained 상태이면, lock의 holder가 기다리는 lock이 없을 때까지 lock_holder 추적하여 올라가기
		while (curr_thread->wait_on_lock != NULL)
		{

			// lock_holder의 priority가 나보다 낮으면 업데이트
			if ((curr_thread->wait_on_lock->holder->priority) < (curr_thread->priority))
			{
				curr_thread->wait_on_lock->holder->priority = curr_thread->priority;
			}

			// 만약 chained 상태이면, 그 위의 holder 상황도 확인하기 위해 curr_thread를 재할당
			curr_thread = curr_thread->wait_on_lock->holder;
		}
	}

	// lock 가져보겠다고, semaphore 하나 내려주기
	// 만약 그 순간 sema가 0이면,
	// sema가 알아서 current thread를 waiting list로 넣어서 sleep 시킴
	sema_down(&lock->semaphore);

	// sema가 원래 0이상인 상태였거나, (lock_holder가 NULL)
	// 0이상이 되어 curr_thread가 깨어나면
	// sema_down 함수가 완료되면서 lock 획득에 성공

	// 혹시 앞의 if문에 들어갔을 수 있으니, curr_thread 다시 초기화
	curr_thread = thread_current();

	// 내가 기다리는 lock 없애주기
	curr_thread->wait_on_lock = NULL;

	// 내가 lock의 주인이 되기
	lock->holder = curr_thread;
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

	struct thread *curr_thread = thread_current(); // == lock_holder
	struct list *donations = &curr_thread->donations;

	// 나에게 donations 있는 경우, 현재 lock을 기다리는 d_elem을 삭제하기
	if (!list_empty(donations))
	{
		struct thread *dona_thread;
		struct list_elem *curr_elem = list_begin(donations);

		// donations의 끝까지 탐색
		while (curr_elem != list_end(donations))
		{
			dona_thread = list_entry(curr_elem, struct thread, d_elem);

			// 만약 해당 donator가 기다리는 lock이 현재 lock가 동일하다면,
			// donations 리스트에서 삭제
			if (dona_thread->wait_on_lock == lock)
			{
				list_remove(curr_elem);
			}
			curr_elem = list_next(curr_elem);
		}
	}

	// 현재 lock을 기다리고 있던 d_elem들을 지우고
	// 남은 donations 확인
	if (!list_empty(donations))
	{
		// 해당 lock이 아닌, 다른 lock에 대한 donations가 있는 경우
		// 나머지 donations애들 중 highest priority에 donate받기
		struct thread *highest_prio_thread = list_entry(list_front(donations), struct thread, d_elem);
		curr_thread->priority = highest_prio_thread->priority;
	}
	else
	{
		// donations가 남아있지 않다면
		// 나의 원래 우선순위로 되돌아가기
		curr_thread->priority = curr_thread->own_priority;
	}

	// lock->holder 자리 내려놓기
	lock->holder = NULL;

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

bool sema_comp_priority(const struct list_elem *sema_list_a, const struct list_elem *sema_list_b, void *aux)
{
	struct semaphore_elem *sema_a = list_entry(sema_list_a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(sema_list_b, struct semaphore_elem, elem);

	struct list *sema_a_wait_list = &(sema_a->semaphore.waiters);
	struct list *sema_b_wait_list = &(sema_b->semaphore.waiters);

	struct thread *wait_thread_a = list_entry(list_begin(sema_a_wait_list), struct thread, elem);
	struct thread *wait_thread_b = list_entry(list_begin(sema_b_wait_list), struct thread, elem);

	return wait_thread_a->priority > wait_thread_b->priority;
}

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
	list_insert_ordered(&cond->waiters, &waiter.elem, sema_comp_priority, NULL);
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

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, sema_comp_priority, NULL);
		sema_up(&list_entry(list_pop_front(&cond->waiters),
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
