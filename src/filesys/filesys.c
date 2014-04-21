#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

#define ASCII_SLASH 47

struct dir* get_this_dir (const char* name);

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}



/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool isDirectory) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = get_this_dir(name);
  char* file_name = get_file_name(name);
  bool success = false;
  if(strcmp(file_name, ".") != 0 && strcmp(file_name, "..") != 0){	
	success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, isDirectory)
                  && dir_add (dir, file_name, inode_sector));
  }
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free(file_name);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
    //if the name has no length then return null
    if(strlen(name) == 0)
	return NULL;
    struct dir *dir = get_this_dir(name);
    char* file_name = get_file_name(name); //grab the filename;
    struct inode *inode = NULL;
    //if not a directory
    if (dir != NULL)
    {
	//if the filename is ".."
	if(strcmp(file_name, "..") == 0)
	{
	    //and the inode has no parent then free up and return null
	    if(!dir_get_parent(dir, &inode))
	    {
		free(file_name);
		return NULL;
	    }
	}
	//if the dir is a root dir and the length of the name is 0
	//or if the filename is "."
	else if((is_root_dir(dir) && strlen(file_name) == 0)||
		strcmp(file_name, ".") == 0)
	{
	    //then free filename and return the dir as a file
	    free(file_name);
	    return (struct file *) dir;
	}
	//otherwise lookup the dir
	else
    	    dir_lookup (dir, file_name, &inode);
    }  	
    dir_close (dir);
    free(file_name);
    //if inode is null then return null
    if(!inode)
	return NULL;
    //if the inode is a directory then open the directory and return it as a file
    if(inode_is_dir(inode))
	return (struct file *) dir_open(inode);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = get_this_dir (name);
  char* file_name = get_file_name(name);
  bool success = dir != NULL && dir_remove (dir, file_name);
  dir_close (dir); 
  free(file_name);

  return success;
}

bool filesys_chdir (const char* name)
{
    struct dir* dir = get_this_dir(name);
    char* file_name = get_file_name(name);
    struct inode *inode = NULL;
    struct thread *cur = thread_current();
    
    if(dir != NULL)
    {
	if(strcmp(file_name, "..") == 0)
	{
	    if(!dir_get_parent(dir, &inode))
	    {
		free(file_name);
		return false;
	    }
	}
	else if((is_root_dir(dir) && strlen(file_name) == 0) || strcmp(file_name, ".") == 0)
	{
	    cur->pwd = dir;
	    free(file_name);
	    return true;
	}
	else
	    dir_lookup(dir, file_name, &inode);
    }
    
    dir_close(dir);
    free(file_name);

    dir = dir_open (inode);
    if(dir)
    {
	dir_close(cur->pwd);
	cur->pwd=dir;
	return true;
    }
    return false;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

struct dir* get_this_dir (const char* name)
{
  char string[strlen(name) + 1];
  memcpy(string, name, strlen(name) + 1);
  char *save_ptr, *next_token = NULL;
  char *token = strtok_r(string, "/", &save_ptr);
  struct dir* dir;
  struct thread *cur = thread_current();
  if(string[0] == ASCII_SLASH || !thread_current()->pwd)	
	dir = dir_open_root();
  else
	dir = dir_reopen(thread_current()->pwd);
  if(token)
	next_token = strtok_r (NULL, "/", &save_ptr);
  while (next_token != NULL)
  {
	if(strcmp(token, ".") != 0)
	{
	    struct inode *inode;
	    if(strcmp(token, "..") == 0)
	    {
		if(!dir_get_parent(dir, &inode)){
		    return NULL;
		}
	    }
	    else
	    {
		if(!dir_lookup(dir, token, &inode)){
		    return NULL;
		}
	    }
	    if (inode_is_dir(inode))
	    {
		dir_close(dir);
		dir = dir_open(inode);
	    }
	    else
	       	inode_close(inode);
	}
	token = next_token;
	next_token = strtok_r(NULL, "/", &save_ptr);
  }
  return dir;
}

char* get_file_name (const char* name)
{
  char s[strlen(name) + 1];
  memcpy(s, name, strlen(name) + 1);

  char *token, *save_ptr, *prev_token = "";
  for (token = strtok_r(s, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
      prev_token = token;
  char *file_name = malloc(strlen(prev_token) + 1);
  memcpy(file_name, prev_token, strlen(prev_token) + 1);
  return file_name;
}
