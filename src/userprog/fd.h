#ifndef USERPROG_FD_H
#define USERPROG_FD_H

#include "threads/thread.h"
#include "filesys/file.h"

#define FDSIZE 500

void fd_init (void);
bool fd_is_valid (int fd_id);
int fd_acquire (struct file *file);
void fd_release (int fd_id);
void fd_process_exit (struct thread *t);

struct file *fd_get (int fd_id);

#endif /* userprog/fd.h */
