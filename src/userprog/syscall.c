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
syscall_handler (struct intr_frame *f) 
{
  //added
  if(!verify_pointer(f->esp)){ //add arg
  	//free resources
  	//terminate thread
  	thread_exit();
  } 

  //dereference esp to get syscall number
  int sys = *esp; 

  int args[3]; 
  copy_in(args, (uint32_t *)f->esp+1, (sizeof *args)*3); 
  switch(sys){ //add all args in 
    case 0:
      halt();
      break; 
    case 1:
      exit(args[0]);
      break;  
    case 2: 
      exec(args[0]); 
      break; 
    case 3: 
      wait(args[0]); 
      break; 
    case 4: 
      create(args[0], args[1]); 
      break; 
    case 5:
      remove(args[0]);
      break;
    case 6: 
      open(args[0]);
      break; 
    case 7:  
      filesize(args[0]); 
      break; 
    case 8: 
      read(args[0],args[1],args[2]);
      break; 
    case 9: 
      write(args[0],args[1],args[2]); 
      break; 
    case 10: 
      seek(args[0],args[1]); 
      break; 
    case 11: 
      tell(args[0]); 
      break; 
    case 12:
      close(args[0]); 
      break; 
    default: 
      printf("Error in system call number %d. Exiting.", sys); 
      thread_exit(); 
  }
  printf ("system call!\n");
  
  thread_exit ();
}

//added
/*verifies if user address requested is in user space and is not mapped to NULL*/
static bool
verify_pointer( const void* addr){

	return( addr < PHYS_BASE && pagedir_get_page(thread_current()->pagedir, addr) !=NULL);
}




/*Terminates Pintos by calling shutdown_power_off() (declared in
devices/shutdown.h This should be seldom used, because you lose some information
about possilbe deadlock situations, etc.) */
void halt (void){
  shutdown_power_off(); 
} 

/*Terminates the current user program returning status to kernel. 
If the process's parent waits (see below), this is the status that
will be returned. Conventionally, a status of 0 indicates
success and nonzero values indicate errors.*/
void exit (int status){
NO_RETURN;

} 

/*Runs the executable whose name is given in cmd_line, 
passing any arguments and returns the new process's program id. 
Must return pid-1 which otherwise
should not be a valid pid, if the program cannot load or run for any reason. Thus,
the parent process cannot return from the exec until it knows whether the child
process successfully loaded its executable. You must use appropriate synchronization
to ensure this.
*/
pid_t exec (const char *cmd_line);

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
int wait (pid_t);


/*Creates a new file called file initially initial size bytes in size. Returns true if suc-
cessful, false otherwise. Creating a new file does not open it: opening the new file is
a separate operation which would require a open system call */
bool create (const char *file, unsigned initial_size);

/*Deletes the file called file. Returns true if successful, false otherwise. A file may be
removed regardless of whether it is open or closed, and removing an open file does
not close it. See [Removing an Open File], page 34, for details.*/
bool remove (const char *file);

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
int open (const char *file);

/*Returns the size in bytes of the file open as fd*/
int filesize (int fd);

/*Reads size bytes from the file open as fd into buffer. Returns the number of bytes
actually read (0 at end of file), or -1 if the file could not be read (due to a condition
other than end of file). fd 0 reads from the keyboard using input_getc().*/
int read (int fd, void *buffer, unsigned length);

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
int write (int fd, const void *buffer, unsigned length);

/*Changes the next byte to be read or written in open file fd to position, expressed in
bytes from the beginning of the file. (Thus, a position of 0 is the file’s start.)
A seek past the current end of a file is not an error. A later read obtains 0 bytes,
indicating end of file. A later write extends the file, filling any unwritten gap with
zeros. (However, in Pintos, files will have a fixed length until project 4 is complete,
so writes past end of file will return an error.) These semantics are implemented in
the file system and do not require any special effort in system call implementation.*/
void seek (int fd, unsigned position);

/*Returns the position of the next byte to be read or written in open file fd, expressed
in bytes from the beginning of the file. */
unsigned tell (int fd);

/*Closes file descriptor fd. Exiting or terminating a process implicitly closes all its open
file descriptors, as if by calling this function for each one.*/
void close (int fd);
