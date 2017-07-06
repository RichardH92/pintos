#ifndef THREADS_ALARM_H
#define THREADS_ALARM_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/thread.h"

struct alarm {
	struct list_elem sleep_elem;
    uint64_t start_sleep_tick;
    uint64_t amount_ticks_to_sleep;
    struct thread *sleeping_thread;
};


void alarm_sleep_current_thread (uint64_t start, uint64_t ticks);
void alarm_check_sleeping_list (uint64_t curr_tick);
//void thread_wake (struct thread *t);

#endif