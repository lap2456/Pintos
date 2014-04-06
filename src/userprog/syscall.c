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
#include "filesys/directory.h"
#include "userprog/process.h"
#include "userprog/fd.c"
#include "userprog/exception.h"
#include "userprog/pagedir.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
static void copy_in (void *, const void *, size_t);
static struct lock file_sys_lock;//added

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_sys_lock); 
}

static void
syscall_handler (struct intr_frame *f) 
{
  //added
  if(!verify_pointer(&f->esp)){ //add arg
  	//free resources
  	//terminate thread
  	thread_exit();
  } 

  //dereference esp to get syscall number
  int *sys = (int*)(f->esp); 

  int args[3]; 
  copy_in(args, (uint32_t *)f->esp+1, (sizeof *args)*3); 
  int result;
  switch(*sys){ //add all args in 
    case 0:
      result = halt();
      break; 
    case 1:
      result = exit(args[0]);
      break;  
    case 2: 
      result = exec((const char *)args[0]); 
      break; 
    case 3: 
      result = wait((tid_t)args[0]); 
      break; 
    case 4: 
      result = create((const char*)args[0], (unsigned)args[1]); 
      break; 
    case 5:
      result = remove((const char*)args[0]);
      break;
    case 6: 
      result = open((const char*)args[0]);
      break; 
    case 7:  
      result = filesize(args[0]); 
      break; 
    case 8: 
      result = read(args[0], (char *)args[1], (unsigned)args[2]);
      break; 
    case 9: 
      result = write(args[0], (const void*)args[1], (unsigned)args[2]); 
      break; 
    case 10: 
      result = seek(args[0], (unsigned)args[1]); 
      break; 
    case 11: 
      result = tell(args[0]); 
      break; 
    case 12:
      result = close(args[0]); 
      break; 
    default: 
      printf("Error in system call number %d. Exiting.", *sys); 
      halt(); 
  }
  f->eax = result;
}

//added
/*verifies if user address requested is in user space and is not mapped to NULL*/
int verify_pointer(const void* addr){
	return((addr != NULL) && (addr < PHYS_BASE) && (is_user_vaddr(addr)));
}

static inline bool
get_user (uint8_t *dst, const uint8_t *usrc)
{
  int eax;
  asm ("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
       : "=m" (*dst), "=&a" (eax) : "m" (*usrc));
  return eax != 0;
}

static void
copy_in (void *dst_, const void *usrc_, size_t size) 
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;
 
  for (; size > 0; size--, dst++, usrc++) 
    if (usrc >= (uint8_t *) PHYS_BASE || !get_user (dst, usrc)) 
      thread_exit ();
}


/*Terminates Pintos by calling shutdown_power_off() (declared in
devices/shutdown.h This should be seldom used, because you lose some information
about possilbe deadlock situations, etc.) */
int halt (void){
  shutdown_power_off(); 
} 

/*Terminates the current user program returning status to kernel. 
If the process's parent waits (see below), this is the status that
will be returned. Conventionally, a status of 0 indicates
success and nonzero values indicate errors.*/
static int exit (int status){
  thread_current()->exit_status = status;
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();
} 

/*Runs the executable whose name is given in cmd_line, 
passing any arguments and returns the new process's program id. 
Must return pid-1 which otherwise
should not be a valid pid, if the program cannot load or run for any reason. Thus,
the parent process cannot return from the exec until it knows whether the child
process successfully loaded its executable. You must use appropriate synchronization
to ensure this.
*/

static int exec (const char *cmd_line){
	lock_acquire (&file_sys_lock);
	int result = process_execute (cmd_line);
	lock_release (&file_sys_lock);
	return result;
}

/*Waits for a child process pid and retrieves the child’s exit status.
If pid is still alive, waits until it terminates. Then, returns the status that pid passed
to exit. If pid did not call exit(), but was terminated by the kernel (e.g. killed due
to an exception), wait(pid) must return -1. It is perfectly legal for a parent process
to wait for child processes that have already terminated by the time the parent calls
wait, but the kernel must still allow the parent to retrieve its child’s exit status or
learn that the child was terminated by the kernel.
wait must fail and return -1 immediately if any of the following conditions are true:
Chapter 3: Part 2: User Programs
29
• pid does not refer to a direct child of the calling process. pid is a direct child
of the calling process if and only if the calling process received pid as a return
value from a successful call to exec.
Note that children are not inherited: if A spawns child B and B spawns child
process C, then A cannot wait for C, even if B is dead. A call to wait(C) by
process A must fail. Similarly, orphaned processes are not assigned to a new
parent if their parent process exits before they do.
• The process that calls wait has already called wait on pid. That is, a process
may wait for any given child at most once.
Processes may spawn any number of children, wait for them in any order, and may
even exit without having waited for some or all of their children. Your design should
consider all the ways in which waits can occur. All of a process’s resources, including
its struct thread, must be freed whether its parent ever waits for it or not, and
regardless of whether the child exits before or after its parent.
You must ensure that Pintos does not terminate until the initial process exits.
The supplied Pintos code tries to do this by calling process_wait() (in
‘userprog/process.c’) from main() (in ‘threads/init.c’). We suggest that you
implement process_wait() according to the comment at the top of the function
and then implement the wait system call in terms of process_wait().
Implementing this system call requires considerably more work than any of the rest.
*/
static int wait (tid_t tid){
	int value = process_wait(tid);
	return value;
}

/*Creates a new file called file initially initial size bytes in size. Returns true if suc-
cessful, false otherwise. Creating a new file does not open it: opening the new file is
a separate operation which would require a open system call */
static int create (const char *file, unsigned initial_size){
	if (!verify_pointer (file))
		exit(-1);
	lock_acquire (&file_sys_lock);
	int result = filesys_create (file, initial_size);
	lock_release (&file_sys_lock);
	return result;
}

/*Deletes the file called file. Returns true if successful, false otherwise. A file may be
removed regardless of whether it is open or closed, and removing an open file does
not close it. See [Removing an Open File], page 34, for details.*/
static int remove (const char *file){
	if(!verify_pointer(file))
		exit(-1);
	lock_acquire (&file_sys_lock);
	int result = filesys_remove (file);
	lock_release (&file_sys_lock);
	return result;
}

/*Opens the file called file. Returns a nonnegative integer handle called a “file descrip-
tor” (fd) or -1 if the file could not be opened.
File descriptors numbered 0 and 1 are reserved for the console: fd 0 (STDIN_FILENO) is
standard input, fd 1 (STDOUT_FILENO) is standard output. The open system call will
never return either of these file descriptors, which are valid as system call arguments
only as explicitly described below.
Each process has an independent set of file descriptors. File descriptors are not
inherited by child processes.
When a single file is opened more than once, whether by a single process or different
processes, each open returns a new file descriptor. Different file descriptors for a single
file are closed independently in separate calls to close and they do not share a file
position.*/
static int open (const char *file){
	if(!verify_pointer(file))
		exit(-1);
	lock_acquire (&file_sys_lock);
	struct file *file_opened = filesys_open (file);
	if(file_opened == NULL){
		lock_release (&file_sys_lock);
		return -1;
	}
	int result = fd_acquire (file_opened);
	lock_release (&file_sys_lock);
	return result;
}

/*Returns the size in bytes of the file open as fd*/
static int filesize (int fd){
	if(!fd_is_valid(fd))
		exit(-1);
	lock_acquire(&file_sys_lock);
	int result = file_length (fd_get (fd));
	lock_release (&file_sys_lock);
	return result;
}

/*Reads size bytes from the file open as fd into buffer. Returns the number of bytes
actually read (0 at end of file), or -1 if the file could not be read (due to a condition
other than end of file). fd 0 reads from the keyboard using input_getc().*/
static int read (int fd, char *buffer, unsigned length){
	if(!verify_pointer(buffer) || !is_user_vaddr (buffer + length))
		exit(-1);
	lock_acquire (&file_sys_lock);
	if(fd == STDIN_FILENO){
		int i;
		for(i = 0; i<length; i++)
			buffer[i] = input_getc();
		lock_release (&file_sys_lock);
		return length;
	}
	if (!fd_is_valid (fd)){
		lock_release (&file_sys_lock);
		return -1;
	}
	struct file *fp = fd_get (fd);
	int result = file_read (fp, buffer, length);
	lock_release (&file_sys_lock);
	return result;
}

/*Writes size bytes from buffer to the open file fd. 
Returns the number of bytes actually  written, which may be 
less than size if some bytes could not be written.
Writing past end-of-file would normally extend the file, 
but file growth is not implemented by the basic file system. 
The expected behavior is to write as many bytes as
possible up to end-of-file and return the actual number written,
or 0 if no bytes could be written at all.

fd 1 writes to the console. Your code to write to the console should write all of buffer
in one call to putbuf(), at least as long as size is not bigger than a few hundred
bytes. (It is reasonable to break up larger buffers.) Otherwise, lines of text output
by different processes may end up interleaved on the console, confusing both human
readers and our grading scripts.*/
static int write (int fd, const void *buffer, unsigned length){
	if(!verify_pointer(buffer) || !is_user_vaddr (buffer + length))
		exit(-1);
	lock_acquire(&file_sys_lock);
	if (fd == STDOUT_FILENO)
	{
		putbuf(buffer, length);
		lock_release (&file_sys_lock);
		return length;
	}
	if(!fd_is_valid (fd)){
		lock_release (&file_sys_lock);
		return -1;
	}
	int result = file_write (fd_get (fd), buffer, length);
	lock_release (&file_sys_lock);
	return result;
}

/*Changes the next byte to be read or written in open file fd to position, expressed in
bytes from the beginning of the file. (Thus, a position of 0 is the file’s start.)
A seek past the current end of a file is not an error. A later read obtains 0 bytes,
indicating end of file. A later write extends the file, filling any unwritten gap with
zeros. (However, in Pintos, files will have a fixed length until project 4 is complete,
so writes past end of file will return an error.) These semantics are implemented in
the file system and do not require any special effort in system call implementation.*/
static int seek (int fd, unsigned position){
	if(!fd_is_valid(fd))
		return -1;
	lock_acquire (&file_sys_lock);
	file_seek (fd_get(fd), position);
	lock_release(&file_sys_lock);
	return 0;
}

/*Returns the position of the next byte to be read or written in open file fd, expressed
in bytes from the beginning of the file. */
static int tell (int fd){
	if(!fd_is_valid(fd))
		return -1;
	lock_acquire(&file_sys_lock);
	fd_release(fd);
	lock_release(&file_sys_lock);
	return 0;
}

/*Closes file descriptor fd. Exiting or terminating a process implicitly closes all its open
file descriptors, as if by calling this function for each one.*/
static int close (int fd){
	if(!fd_is_valid (fd))
		exit(-1);
	lock_acquire (&file_sys_lock);
	fd_release (fd);
	lock_release (&file_sys_lock);
	return 0;
}
