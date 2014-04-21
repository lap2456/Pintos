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
#include "filesys/inode.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/pagedir.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
static void copy_in (void *, const void *, size_t);
static struct lock file_sys_lock;//added

static int sys_halt (void);
static int sys_exit (int status);
static int sys_exec (const char *file);
static int sys_wait (tid_t);
static int sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int handle);
static int sys_read (int handle, void *buffer, unsigned size);
static int sys_write (int handle, void *buffer, unsigned size);
static int sys_seek (int handle, unsigned position);
static int sys_tell (int handle);
static int sys_close (int handle);
static bool sys_chdir (const char* dir);
static bool sys_mkdir (const char* dir);
static bool sys_readdir (int fd, char* name);
static bool sys_isdir (int fd);
static int sys_inumber (int fd);

static bool verify_pointer(const void*);

struct file_descriptor{
	struct list_elem elem; 
	struct file *file; 
	int handle; //file handle
};
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_sys_lock); 
}

static void
syscall_handler (struct intr_frame *f) 
{
  //dereference esp to get syscall number
  int *sys = (int*)(f->esp); 
  if(!verify_pointer(sys))
	sys_exit(-1);
  int args[3]; 
  copy_in(args, (uint32_t *)f->esp+1, (sizeof *args)*3); 
  int result;
  switch(*sys){ //add all args in 
    case SYS_HALT:
      result = sys_halt();
      break; 
    case SYS_EXIT:
      result = sys_exit(args[0]);
      break;  
    case SYS_EXEC: 
      result = sys_exec((const char *)args[0]); 
      break; 
    case SYS_WAIT: 
      result = sys_wait((tid_t)args[0]); 
      break; 
    case SYS_CREATE: 
      result = sys_create((const char*)args[0], (unsigned)args[1]); 
      break; 
    case SYS_REMOVE:
      result = sys_remove((const char*)args[0]);
      break;
    case SYS_OPEN: 
      result = sys_open((const char*)args[0]);
      break; 
    case SYS_FILESIZE:  
      result = sys_filesize(args[0]); 
      break; 
    case SYS_READ: 
      result = sys_read(args[0], (char *)args[1], (unsigned)args[2]);
      break; 
    case SYS_WRITE: 
      result = sys_write(args[0], (const void*)args[1], (unsigned)args[2]); 
      break; 
    case SYS_SEEK: 
      result = sys_seek(args[0], (unsigned)args[1]); 
      break; 
    case SYS_TELL: 
      result = sys_tell(args[0]); 
      break; 
    case SYS_CLOSE:
      result = sys_close(args[0]); 
      break; 
    case SYS_CHDIR:
      result = sys_chdir(args[0]);
      break;
    case SYS_MKDIR:
      result = sys_mkdir(args[0]);
      break;
    case SYS_READDIR:
      result = sys_readdir(args[0], args[1]);
      break;
    case SYS_ISDIR:
      result = sys_isdir(args[0]);
      break;
    case SYS_INUMBER:
      result = sys_inumber(args[0]);
      break;
    default: 
      printf("Error in system call number %d. Exiting.", *sys); 
      sys_halt(); 
  }
  f->eax = result;
}

//added
/*verifies if user address requested is in 
user space and is not mapped to NULL*/
//Returns true if UADDR is a valid, mapped user address 
static bool
verify_pointer (const void *uaddr)
{
  return (uaddr < PHYS_BASE
          && pagedir_get_page (thread_current ()->pagedir, uaddr) != NULL);
}


/*Copies a byte from user address source (usrc) to 
kernel destination (kdst) if usrc is below
PHYS_BASE. Returns true upon success. False indicates
SEGFAULT occurred */
static inline bool
get_user (uint8_t *kdst, const uint8_t *usrc)
{
  int eax=0;
  if(kdst >= (uint8_t *)PHYS_BASE){
    	asm ("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
       : "=m" (*kdst), "=&a" (eax) : "m" (*usrc));
  } 
  return eax != 0;
}

/*Copies size bytes from user address source (usrc) to 
kernal address kdst. thread_exit is called if any user
accesses are invalid. */
static void
copy_in (void *kdst_, const void *usrc_, size_t size) 
{
  uint8_t *kdst = kdst_;
  const uint8_t *usrc = usrc_;
 
  for (; size > 0; size--, kdst++, usrc++) 
    if (usrc >= (uint8_t *) PHYS_BASE || !get_user (kdst, usrc)) 
      thread_exit ();
}


/*Terminates Pintos by calling shutdown_power_off() (declared in
devices/shutdown.h This should be seldom used, because you lose some information
about possilbe deadlock situations, etc.) */
static int sys_halt (void){
  shutdown_power_off(); 
  NOT_REACHED();
} 

/*Terminates the current user program returning status to kernel. 
If the process's parent waits (see below), this is the status that
will be returned. Conventionally, a status of 0 indicates
success and nonzero values indicate errors.*/
static int sys_exit (int status){
  thread_current()->progress->exit_status = status;
  //printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();
  NOT_REACHED(); 
} 

/* Creates a copy of user string US in kernel memory
and returns it as a page that must be freed with
palloc_free_page().
Truncates the string at PGSIZE bytes in size.
Call thread_exit() if any of the user accesses are invalid. */
static char *
copy_in_string (const char *us)
{
  char *ks;
  size_t length;
 
  ks = palloc_get_page (0);
  if (ks == NULL)
    thread_exit ();
 
  for (length = 0; length < PGSIZE; length++)
    {
      if (us >= (char *) PHYS_BASE || !get_user (ks + length, us++))
        {
          palloc_free_page (ks);
          thread_exit ();
        }
       
      if (ks[length] == '\0')
        return ks;
    }
  ks[PGSIZE - 1] = '\0';
  return ks;
}

/*Runs the executable whose name is given in cmd_line, 
passing any arguments and returns the new process's program id. 
Must return pid-1 which otherwise
should not be a valid pid, if the program cannot load or run for any reason. Thus,
the parent process cannot return from the exec until it knows whether the child
process successfully loaded its executable. You must use appropriate synchronization
to ensure this.
*/

static int sys_exec (const char *cmd_line){
	if(verify_pointer(cmd_line)){
		const char *kfile = copy_in_string(cmd_line); 
		lock_acquire (&file_sys_lock);
		int result = process_execute (kfile);
		lock_release (&file_sys_lock);
		palloc_free_page(kfile); 
		return result;
	} else return false; 
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
static int sys_wait (tid_t tid){
	int value = process_wait(tid);
	return value;
}

/*Creates a new file called file initially initial size bytes in size. Returns true if suc-
cessful, false otherwise. Creating a new file does not open it: opening the new file is
a separate operation which would require a open system call */
static int sys_create (const char *file, unsigned initial_size){
	if (verify_pointer(file)){ 
	const char *kfile = copy_in_string(file); 
	lock_acquire (&file_sys_lock);
	int result = filesys_create (kfile, initial_size, false);
	lock_release (&file_sys_lock);
	palloc_free_page(kfile); 
	return result;
	}
	else thread_exit();  

}

/*Deletes the file called file. Returns true if successful, false otherwise. A file may be
removed regardless of whether it is open or closed, and removing an open file does
not close it. See [Removing an Open File], page 34, for details.*/
static bool sys_remove (const char *file){
	bool result = false; 
	if(verify_pointer(file)){
		const char *kfile = copy_in_string(file);
		lock_acquire (&file_sys_lock);
		result = filesys_remove (file);
		lock_release (&file_sys_lock);
		palloc_free_page(kfile);
	} 
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
static int sys_open (const char *file){
	if(verify_pointer(file)){
		char * kfile = copy_in_string(file); 
		struct file_descriptor *fd; 
		int handle = -1; 
		fd = malloc(sizeof *fd); 
		if(fd!=NULL){
			lock_acquire (&file_sys_lock);
			fd->file = filesys_open (kfile);
			if(fd->file != NULL){
				struct thread *t = thread_current(); 
				handle = fd->handle = t->next_handle++; 
				list_push_front(&t->fds, &fd->elem); 
			} else free(fd); 

		lock_release (&file_sys_lock);
	}
		palloc_free_page(kfile); 
		return handle; 
	} else {
		thread_exit(); 
	}
}

static struct file_descriptor * find_fd(int handle){
	struct list_elem *e; 
	struct list *s = &(thread_current()->fds); 
	struct file_descriptor *fd; 
	for(e = list_begin(s); e != list_end(s); e = list_next(e)) {
    	fd = list_entry(e, struct file_descriptor,elem);
    	if(fd->handle == handle)
    	{
     		return fd;
    	}
  	}
  thread_exit();
}
/*Returns the size in bytes of the file open as fd*/
static int sys_filesize (int handle){
	struct file_descriptor *fd = find_fd(handle); 
	return file_length(fd->file); 
}

/*Reads size bytes from the file open as fd into buffer. Returns the number of bytes
actually read (0 at end of file), or -1 if the file could not be read (due to a condition
other than end of file). fd 0 reads from the keyboard using input_getc().*/
static int sys_read (int handle, void *buffer, unsigned length){
  char* udst = (char*) buffer;
  unsigned bytes_read = 0;
  struct file_descriptor* fd = NULL;

  if (handle != STDIN_FILENO)
    fd = find_fd(handle);

  lock_acquire(&file_sys_lock);
  if (!verify_pointer(buffer))
  {
    lock_release(&file_sys_lock);
    thread_exit();
  }
  if (handle == STDIN_FILENO)
  {
    for(bytes_read = 0; bytes_read < length; bytes_read++)
    {
      *udst = (char) input_getc();
      udst++;
    }
  }
  else
  {
    bytes_read = file_read(fd->file, buffer, length);
  }

  lock_release(&file_sys_lock);
  return bytes_read;
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
static int sys_write (int handle, void *buffer, unsigned length){
	uint8_t *usrc = buffer;
  struct file_descriptor *fd = NULL;
  int bytes_written = 0;

  /* Lookup up file descriptor. */
  if (handle != STDOUT_FILENO)
    fd = find_fd (handle);

  lock_acquire (&file_sys_lock);
  while (length > 0)
    {
      /* How many bytes to write to this page??*/
      size_t page_left = PGSIZE - pg_ofs (usrc);
      size_t write_amt = length < page_left ? length : page_left;
      off_t retval;

      /* Check that we can touch this user page. */
      if (!verify_pointer (buffer))
        {
          lock_release (&file_sys_lock);
          thread_exit ();
        }

      /* Perform write. */
      if (handle == STDOUT_FILENO)
        {
          putbuf (usrc, write_amt);
          retval = write_amt;
        }
      else
        retval = file_write (fd->file, usrc, write_amt);
      if (retval < 0)
        {
          if (bytes_written == 0)
            bytes_written = -1;
          break;
        }
      bytes_written += retval;

      /* If it was a short write we're done. */
      if (retval != (off_t) write_amt)
        break;

      /* Advance. */
      usrc += retval;
      length -= retval;
    }
  lock_release (&file_sys_lock);
  return bytes_written;
}

/*Changes the next byte to be read or written in open file fd to position, expressed in
bytes from the beginning of the file. (Thus, a position of 0 is the file’s start.)
A seek past the current end of a file is not an error. A later read obtains 0 bytes,
indicating end of file. A later write extends the file, filling any unwritten gap with
zeros. (However, in Pintos, files will have a fixed length until project 4 is complete,
so writes past end of file will return an error.) These semantics are implemented in
the file system and do not require any special effort in system call implementation.*/
static int sys_seek (int handle, unsigned position){
  struct file_descriptor *fd;
  fd = find_fd(handle);
  if((off_t) position >= 0)
    file_seek(fd->file, position);
  return 0;
}

/*Returns the position of the next byte to be read or written in open file fd, expressed
in bytes from the beginning of the file. */
static int sys_tell (int handle){
  struct file_descriptor *fd;
  fd = find_fd(handle);
  return file_tell(fd->file);
}

/*Closes file descriptor fd. Exiting or terminating a process implicitly closes all its open
file descriptors, as if by calling this function for each one.*/
static int sys_close (int handle){
  struct file_descriptor *fd = find_fd(handle);
  lock_acquire(&file_sys_lock);
  file_close(fd->file); //file_close also allows writes
  lock_release(&file_sys_lock);
  list_remove(&fd->elem);
  free(fd);
  return 0;
}

/* On thread exit, close all open files. */
void
syscall_exit (void)
{
  struct thread *cur = thread_current();
  struct list *s = &(cur->fds);
  struct list_elem *e, *next;

  lock_acquire(&file_sys_lock);

  for(e = list_begin(s);e != list_end(s); e = next)
  {
    struct file_descriptor* fd = list_entry(e, struct file_descriptor, elem);
    file_close(fd->file); //file_close also allows writes 
    next = list_remove(e);
    free(fd);
  }

  lock_release(&file_sys_lock);
  return;
}

static bool
sys_chdir(const char *dir)
{
    lock_acquire(&file_sys_lock);
    if (!verify_pointer(dir))
    {
        lock_release(&file_sys_lock);
        thread_exit();
    }
    bool result = filesys_chdir(dir);
    lock_release(&file_sys_lock);
    return result;
}

static bool sys_mkdir (const char* dir){
    lock_acquire(&file_sys_lock);
    if (!verify_pointer(dir)){
	lock_release(&file_sys_lock);
	thread_exit();
    }
    bool result = filesys_create(dir, 0, true);
    lock_release(&file_sys_lock);
    return result;
}

static bool sys_readdir (int handle, char* name){
  struct file_descriptor *fd;
  fd = find_fd(handle);
  const struct inode *inode = file_get_inode(fd->file);  
  bool isDirectory = inode_is_dir(inode);
  if(!isDirectory)
	return false;
  struct dir *dir = dir_open(inode);
  if(!dir_readdir(dir, name))
	return false;
  return true;
}

static bool sys_isdir (int handle){
  struct file_descriptor *fd;
  fd = find_fd(handle);
  const struct inode *inode = file_get_inode(fd->file);
  bool isDirectory = inode_is_dir(inode);
  return isDirectory;
}

static int sys_inumber (int handle){
  struct file_descriptor *fd;
  fd = find_fd(handle);
  const struct inode *inode = file_get_inode(fd->file);
  int inumber = inode_get_inumber(inode);
  return inumber;
}

