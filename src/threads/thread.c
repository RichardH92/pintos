#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/fixed_point.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

static fixed_point_t load_avg;

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

static void insert_thread_ordered (struct thread *t);
static void update_priority (struct thread *t, int new_priority);
static struct thread * get_max_priority_donor (struct thread *donee);

static void thread_recalculate_all_priorities (void);
static void thread_recalculate_all_recent_cpu (void);
static void thread_recalculate_load_avg (void);
//static void thread_recalculate_current_priority (void);

static int recalculate_priority (struct thread *t);
static void recalculate_recent_cpu (struct thread *t);
static int get_num_ready_threads (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  list_init (&initial_thread->donor_list);
  initial_thread->is_a_donor = false;
  initial_thread->is_a_donee = false;
  initial_thread->original_priority = initial_thread->priority;
  initial_thread->donor_lock = NULL;

  initial_thread->nice = 0;
  initial_thread->recent_cpu = int_to_fixed (0);

  load_avg = int_to_fixed (0);
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  if (thread_mlfqs) 
    {

      enum intr_level old_level = intr_disable ();

      t->recent_cpu = fixed_int_add (t->recent_cpu, 1);

      if (timer_ticks () % TIMER_FREQ == 0)
        {
          thread_recalculate_load_avg ();
          thread_recalculate_all_recent_cpu ();
          thread_recalculate_all_priorities ();
        }

      else if (timer_ticks () % 4 == 0)
          thread_recalculate_all_priorities ();
      
      intr_set_level (old_level);
    }

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();
  list_init (&t->donor_list);
  t->is_a_donor = false;
  t->is_a_donee = false;
  t->donee = NULL;
  t->original_priority = priority;
  t->nice = thread_current ()->nice;
  t->recent_cpu = thread_current ()->recent_cpu;

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  if (thread_mlfqs)
    recalculate_priority (t);

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  if (t->priority > thread_current ()->priority)
    thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  
  insert_thread_ordered (t);
  
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

static void insert_thread_ordered (struct thread *t)
{
  /*List is empty, insert in front*/
  if (list_empty (&ready_list))
    {
      list_push_front (&ready_list, &t->elem);\
      return;
    }

  /*T's priority is greater than every thread, insert in front*/
  struct list_elem *e = list_begin (&ready_list);
  struct thread *curr = list_entry (e, struct thread, elem);

  if (curr->priority < t->priority)
    {
      list_insert(e, &t->elem);
      return;
    }

  /*Iterate through threads and insert at correct spot*/
  struct thread *prev = curr;
  e = list_next(&prev->elem);

  while (e != list_end (&ready_list))
  {
    curr = list_entry (e, struct thread, elem);

    if (prev->priority >= t->priority && t->priority > curr->priority)
      {
        list_insert(e, &t->elem);
        return;
      }

    prev = curr;
    e = list_next(&prev->elem);
  }

  list_push_back (&ready_list, &t->elem);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    insert_thread_ordered (cur);

  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

void 
thread_donate_priority (struct thread *donee, struct lock *donor_lock)
{ 

  ASSERT (!intr_context ());
  ASSERT (donee != NULL);
  ASSERT (is_thread (donee));

  enum intr_level old_level = intr_disable ();
  struct thread *donor = thread_current ();

  ASSERT (donor != NULL);
  ASSERT (is_thread (donor));
  ASSERT (donor->donee == NULL);
  ASSERT (donor->is_a_donor == false);

  donor->donee = donee;

  list_push_back (&donee->donor_list, &donor->donor_elem);
  donor->is_a_donor = true;
  donor->donor_lock = donor_lock;
  donee->priority = donor->priority;
  donee->is_a_donee = true;

  struct thread *nest_donor = donee;

  ASSERT (nest_donor != NULL);

  while (nest_donor->is_a_donor)
    {
      struct thread *nest_donee = nest_donor->donee;
      
      ASSERT (nest_donee != NULL);
      ASSERT (nest_donee->is_a_donee);

      if (donor->priority > nest_donee->priority)
        {
          nest_donee->priority = donor->priority;
          nest_donor = nest_donee;
          nest_donee = nest_donee->donee;
        }
      else
        break;

      if (!nest_donor->is_a_donor)
        {
          ASSERT (nest_donor->donee == NULL);
          break;
        }
    }

  intr_set_level (old_level);
}

/* Reverse the current thread's donated priority */
void 
thread_reverse_priority_donation (struct lock *donor_lock) 
{
  enum intr_level old_level;
  
  ASSERT (donor_lock != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();

  struct thread *donee = thread_current ();

  ASSERT (donee != NULL);
  ASSERT (is_thread (donee));
  ASSERT (!list_empty (&donee->donor_list));
  ASSERT (donor_lock->holder == donee);

  struct list_elem *e;

  for (e = list_begin (&donee->donor_list); e != list_end (&donee->donor_list);
   e = list_next (e))
    {
      ASSERT (e != NULL);

      struct thread *t = list_entry (e, struct thread, donor_elem);

      ASSERT (t != NULL);
      ASSERT (is_thread (t));

      if (t->donor_lock == donor_lock)
        {
          list_remove (&t->donor_elem);
          t->is_a_donor = false;
          t->donee = NULL;
        }
    }

  if (list_empty (&donee->donor_list))
    {
      donee->priority = donee->original_priority;
      donee->is_a_donee = false;
    }
  else
    {
      int new_priority = get_max_priority_donor (donee)->priority;
      donee->priority = new_priority;
    }

  intr_set_level (old_level);
}


/* Find the thread in the donee's donor list that has the highest priority */
static struct thread * get_max_priority_donor (struct thread *donee) 
{

  ASSERT (!list_empty (&donee->donor_list));

  struct thread *max = NULL;
  int priority = -1;
  struct list_elem *e;

  for (e = list_begin (&donee->donor_list); e != list_end (&donee->donor_list);
       e = list_next (e))
    {
      ASSERT (e != NULL);

      struct thread *t = list_entry (e, struct thread, donor_elem);

      ASSERT (t != NULL);
      ASSERT (is_thread (t));

      if (t->priority > priority)
        {
          priority = t->priority;
          max = t;
        }
    }

  ASSERT (max != NULL);
  ASSERT (is_thread (max));

  return max;
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if (thread_mlfqs)
    return;

  enum intr_level old_level;

  if (thread_current ()->is_a_donee)
    {
      old_level = intr_disable ();

      thread_current ()->original_priority = new_priority;

      if (new_priority > thread_current ()->priority)
        update_priority (thread_current (), new_priority);

      intr_set_level (old_level);
    }
  else
    {
      old_level = intr_disable ();

      int32_t temp_priority = thread_current ()->priority;

      thread_current ()->original_priority = new_priority;
      update_priority (thread_current (), new_priority);

      intr_set_level (old_level);

      if (new_priority < temp_priority)
        thread_yield ();
    }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{
  int temp_priority;
  enum intr_level old_level;

  if (thread_mlfqs)
    old_level = intr_disable ();

  temp_priority = thread_current ()->priority;

  if (thread_mlfqs)
    intr_set_level (old_level);

  return temp_priority;
}

/* Update thread t's priority to new_priority */
static void update_priority (struct thread *t, int new_priority)
{
  ASSERT (intr_get_level () == INTR_OFF);


  if (t->priority != new_priority)
    {
      t->priority = new_priority;

      if (t->priority > PRI_MAX)
        t->priority = PRI_MAX;
      else if (t->priority < PRI_MIN)
        t->priority = PRI_MIN;


      if (t->status == THREAD_READY) {
        list_remove (&t->elem);
        insert_thread_ordered (t);
      }

    } 
}

/* Recalculate the priority for all of the threads */
static void thread_recalculate_all_priorities (void)
{
  enum intr_level old_level = intr_disable ();

  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      int new_priority = recalculate_priority (t);
      update_priority (t, new_priority);
    } 

  intr_set_level (old_level);
}

/* Recalculate and update the priority for the current thread */
/*static void thread_recalculate_current_priority (void)
{  
  enum intr_level old_level = intr_disable ();

  struct thread *t = thread_current ();
  int new_priority = recalculate_priority (t);
  update_priority (t, new_priority);

  intr_set_level (old_level);
}*/

/* Recalculate the priority for thread t */
static int recalculate_priority (struct thread *t)
{
  ASSERT (intr_get_level () == INTR_OFF);

  //priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)

  fixed_point_t new_priority = int_to_fixed (PRI_MAX);
  fixed_point_t temp = fixed_int_div (t->recent_cpu, 4);
  new_priority = fixed_sub (new_priority, temp);
  temp = int_to_fixed (t->nice);
  temp = fixed_int_mult (temp, 2);
  new_priority = fixed_sub (new_priority, temp);

  return fixed_to_int (new_priority, 0);
}

/* Recalculate the recent cpu for all threads */
static void thread_recalculate_all_recent_cpu (void)
{
  enum intr_level old_level = intr_disable ();

  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      recalculate_recent_cpu (t);
    } 

  intr_set_level (old_level);
}

/* Recalculate the recent cpu for thread t */
static void recalculate_recent_cpu (struct thread *t)
{
  ASSERT (intr_get_level () == INTR_OFF);

  //recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice

  fixed_point_t temp_one = fixed_int_mult (load_avg, 2);
  fixed_point_t temp_two = fixed_int_add (temp_one, 1);
  temp_one = fixed_div (temp_one, temp_two);
  temp_one = fixed_mult (temp_one, t->recent_cpu);
  t->recent_cpu = fixed_int_add (temp_one, t->nice);
}

/* Recalculate the load average */
static void thread_recalculate_load_avg (void)
{
  //load_avg = (59/60)*load_avg + (1/60)*ready_threads

  enum intr_level old_level = intr_disable ();

  int ready_threads = get_num_ready_threads ();

  fixed_point_t temp_one = fixed_div (int_to_fixed(59), int_to_fixed(60));
  temp_one = fixed_mult (temp_one, load_avg);

  fixed_point_t temp_two = int_to_fixed (ready_threads);
  temp_two = fixed_div (temp_two, int_to_fixed(60));
  load_avg = fixed_add (temp_one, temp_two);

  intr_set_level (old_level);
}

static int get_num_ready_threads (void)
{
  int count = list_size (&ready_list);
  
  if (thread_current () != idle_thread && thread_current()->status == THREAD_RUNNING)
    count = count + 1;

  return count;
}


/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  if (nice > 20)
    nice = 20;
  else if (nice < -20)
    nice = -20;

  enum intr_level old_level = intr_disable ();

  int old_priority = thread_current ()->priority;
  thread_current ()->nice = nice;
  int new_priority = recalculate_priority (thread_current ());
  update_priority (thread_current (), new_priority);

  intr_set_level (old_level);

  if (new_priority < old_priority)
    thread_yield ();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
  int temp_nice;

  enum intr_level old_level = intr_disable ();
  temp_nice = thread_current ()->nice;
  intr_set_level (old_level);

  return temp_nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  fixed_point_t temp;

  enum intr_level old_level = intr_disable ();
  temp = load_avg;
  intr_set_level (old_level);
  temp = fixed_int_mult (load_avg, 100);

  int load = fixed_to_int (temp, 1);

  return load;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  fixed_point_t temp;

  enum intr_level old_level = intr_disable ();
  temp = fixed_int_mult (thread_current ()->recent_cpu, 100);
  intr_set_level (old_level);

  return fixed_to_int (temp, 1);
}


/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
