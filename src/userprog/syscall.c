#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#

static void syscall_handler (struct intr_frame *);

static struct lock file_sys_lock;//added

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_sys_lock); 
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //added
  if(!verify_pointer()){ //add arg
  	//free resources
  	//terminate thread
  	thread_exit();
  } 
  //dereference

  printf ("system call!\n");
  thread_exit ();
}

//added
/*verifies if user address requested is in user space and is not mapped to NULL*/
static bool
verify_pointer( const void* addr){

	return( addr < PHYS_BASE && pagedir_get_page(thread_current()->pagedir, addr) !=NULL);
}