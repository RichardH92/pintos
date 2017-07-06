#include "threads/alarm.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"

/* List of all threads that are currently sleeping until set time */
static struct list sleeping_list;

static bool sleep_time_elapsed (struct alarm *t, uint64_t curr_tick);
static void wake_thread (struct alarm *t);
static struct alarm construct_alarm (struct thread *t, uint64_t start, uint64_t ticks);


/*  Adds the current thread to the sleeping list for ticks amount of
    time */
void
alarm_sleep_current_thread (uint64_t start, uint64_t ticks)
{
  ASSERT (intr_get_level () == INTR_ON);
  ASSERT (start > 0);
  ASSERT (ticks > 0);

  struct thread *t = thread_current();

  struct alarm alarm = construct_alarm (t, start, ticks);

  enum intr_level old_level = intr_disable ();

  list_push_back (&sleeping_list, &alarm.sleep_elem);
  thread_block();

  intr_set_level (old_level);
}

static struct alarm 
construct_alarm (struct thread *t, uint64_t start, uint64_t ticks)
{
	struct alarm alarm;

    alarm.start_sleep_tick = start;
    alarm.amount_ticks_to_sleep = ticks;
    alarm.sleeping_thread = t;

    return alarm;
}


/* Check if any sleeping threads need to be waken */
void
alarm_check_sleeping_list (uint64_t curr_tick) 
{
  ASSERT (curr_tick > 0);
  struct list_elem *e;

  for (e = list_begin (&sleeping_list); e != list_end (&sleeping_list);
       e = list_next (e))
    {
      struct alarm *t = list_entry (e, struct alarm, sleep_elem);

      if (sleep_time_elapsed (t, curr_tick))
        wake_thread (t);
    }
}

/* Returns true if a thread's sleeping interval has elapsed */
static bool sleep_time_elapsed (struct alarm *t, uint64_t curr_tick)
{

  int64_t start = t->start_sleep_tick;
  ASSERT (curr_tick >= start);

  int64_t interval = t->amount_ticks_to_sleep;
  ASSERT (interval >= 0);

  if ((curr_tick - start) >= interval)
    return true;

  return false;
}

/* Wake a sleeping thread */
static void
wake_thread (struct alarm *t)
{
  enum intr_level old_level;

  old_level = intr_disable ();
  ASSERT (t->sleeping_thread->status == THREAD_BLOCKED);

  list_remove (&t->sleep_elem);

  intr_set_level (old_level);

  thread_unblock (t->sleeping_thread);
}