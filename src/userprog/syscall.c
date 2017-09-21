#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static int get_syscall_num_from_frame (struct intr_frame *);
static void halt (void);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  printf ("system call!\n");

  int syscall_num = get_syscall_num_from_frame (f);

  switch (syscall_num) 
  {
  	case SYS_HALT:
  		halt ();
  		return;
  }

  thread_exit ();
}

static int get_syscall_num_from_frame (struct intr_frame *f)
{
	return 0;
}

static void halt (void)
{
	shutdown_power_off();
}
