#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
int verify_pointer( const void* addr);

int halt (void);
static int exit (int status);
static int exec (const char *cmd_line);
static int wait (pid_t);
static int create (const char *file, unsigned initial_size);
static int remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, char *buffer, unsigned length);
static int write (int fd, const void *buffer, unsigned length);
static int seek (int fd, unsigned position);
static int tell (int fd);
static int close (int fd);

#endif /* userprog/syscall.h */
