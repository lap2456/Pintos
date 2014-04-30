/*
* Basic thread support. Defines struct thread, which is likely
* to be modified in all four projects. 
*/


#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"



/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/*Added. List of processes in THREAD_WAITING state, meaning
  they are placed on the sleep_list */
static struct list sleep_list; 


/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING,        /* About to be destroyed. */
    THREAD_SLEEPING    /*KG added; thread put to sleep*/

  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion.  (So don't add elements below 
   THREAD_MAGIC.)
*/
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    uint64_t priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
    
    /*Added*/
    int64_t sleep_ticks; /*Added. Number of ticks to sleep in timer_sleep()*/
    int64_t original_priority; //original priority (non donated) of thread
    int numDonations; //number of donations that have not been recalled   
    struct list donations; //list of threads that have donated to this lock 
    struct list_elem donationElem;
    struct list_elem sleepElem; 
    struct lock *waitingLock; //the lock the thread is waiting for (or NULL if thread not waiting on a lock)

    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */

    //shared between userprog/process.c and thread.c
    struct file * file_to_run; //file to run (executable)


    struct list *fds; //list of file descriptors 
    int next_handle; //next handle value (???)

    struct progress *progress;  //this process's completion status
    struct list children; //list of children's progresses

    struct dir *pwd;
    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
   
  };


  /*added*/ 
  /*This struct keeps track of a process's completion. 
  Held by both the parent (int its children list)
  and by the child (in its status pointer)*/
  struct progress 
  { 
    struct list_elem elem; //child list element 
    struct lock lock; //protects reference count
    int ref; //0 = child and parent both dead, 1 = one of the two alive, 2 = both alive
    tid_t tid; //child thread id
    int exit_status; //child exit code (if dead)
    struct semaphore dead; //1 if child alive, 0 if child dead 
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);
struct thread *running_thread (void);
struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (uint64_t);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void go_to_sleep(int64_t ticks); /*added*/ 



/*moAdded - true if thread 'a' has HIGHER priority than 'b'*/
static bool
priority_greater (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  struct thread *a = list_entry (a_, struct thread, elem);
  struct thread *b = list_entry (b_, struct thread, elem);
  
  return (a->priority > b->priority);
}

/*moAdded - true if thread 'a' has LOWER priority than 'b'*/
static bool
priority_less (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  struct thread *a = list_entry (a_, struct thread, elem);
  struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->priority < b->priority;
}

#endif /* threads/thread.h */
