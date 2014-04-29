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

  //ASSERT(1==0);
  free_map_open ();
  //ASSERT(1==0); 

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
  char* file_name = get_file_name(name);
  struct dir *dir = get_this_dir(file_name);

  bool success = false;
 
 
	success = (dir != NULL
    && free_map_allocate (1, &inode_sector)
    && inode_create (inode_sector, initial_size, isDirectory)
    && dir_add (dir, file_name, inode_sector));
  
  
 /*
  ASSERT(dir != NULL);
  ASSERT(free_map_allocate(1, &inode_sector));
  ASSERT(inode_create (inode_sector, initial_size, isDirectory));
  ASSERT(dir_add(dir, file_name, inode_sector));
  success = true;
*/
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  if(success && isDirectory){
    //add . and .. if it is a directory
    struct file * f = filesys_open(file_name); 
    struct dir * d = dir_open(file_get_inode(f)); 

    dir_add(d, ".", inode_sector); 
    struct inode * parent = dir_get_inode(d);  
    block_sector_t inode_sector_parent = inode_get_inumber(parent); 

    dir_add(d, "..", inode_sector_parent); 
    dir_close(d); 
    file_close(f); 
  }
  dir_close (dir);
  
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
  char * parse = get_file_name (name); 
  //get the correct dir
  struct thread *curr = thread_current();
  struct dir *dir = get_this_dir (parse);

  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, parse, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char* file_name = get_file_name(name);
  struct dir *dir = get_this_dir (name);
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
  //ASSERT(1==0); //debugging - we are reaching this
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  //ASSERT(1==0); //debugging - we are reaching this 
  
  //added
  //add . and .. for root
  struct dir * root = dir_open_root(); 
  dir_add(root, ".", ROOT_DIR_SECTOR); 

  dir_add(root, "..", ROOT_DIR_SECTOR); 
  dir_close(root); 

  //ASSERT(1==0);
  free_map_close ();
  //ASSERT(1==0);
  printf ("done.\n");
}

struct dir* get_this_dir (const char* file_name)
{
  //printf("lookup path is %s\n", file_name);
  //return root dir
  if (strcmp(file_name, "/") == 0) 
    return dir_open_root();

  int success = 0;
  char *token ,*save_ptr;
  char * temp = (char *)malloc(strlen(file_name) + 1 );
  strlcpy (temp, file_name, strlen(file_name) + 1); 

  //open root and begin and check to see if it is relative
  struct dir * curr_dir; 
  if ( file_name[0] != '/' ) {
    //if it is then make it absolute
    curr_dir = dir_reopen(thread_current()->pwd);
  } else {
    //otherwise open root dir
    curr_dir = dir_open_root();
  }
  
  //parse through directories on file_name and check to make sure they exist
  struct inode * nextdir = dir_get_inode(curr_dir);
  for (token = strtok_r (temp, "/", &save_ptr); 
    token != NULL; token = strtok_r (NULL, "/", &save_ptr))
  {
      //this should not happen 
      if (curr_dir == NULL ) 
        break;
      //last part of file_name should not exist since that is what we are making
      if (strlen(save_ptr) == 0) {
        success = 1;
        break; 
      }

      if(strlen(token) != 0) { 
      //check if this directory exists true if exists
      if (dir_lookup (curr_dir, token, &nextdir)) {
        //if it is a directory
        if(inode_is_dir(nextdir)) {
          //then open it
          dir_close(curr_dir);
          curr_dir = dir_open(nextdir);
        }
        //otherwise an error has occurred and should break
        else break;
      }
      //if directory does not exist then an error has occured
      else break;
    }
  }

  if(success) 
    return curr_dir; 
  else 
    return NULL;
}

char* get_file_name (const char* name)
{
  char *file_name = (char *) malloc(strlen(name) + 1); 
  char *token, *save_ptr; 
  char * temp = (char*)malloc(strlen(name)+1); 

  strlcpy(temp, name, strlen(name)+1 ); 

  //if the string starts with '/' it is an absolute path
  if(name[0]=='/'){
    memcpy(file_name, "/\0", 2);
  } else {
    //relative path
    memcpy(file_name, "\0", 1); 
  }

  //parse the original name to remove multiple slashes
  //parse the original name to remove multiple //
  for (token = strtok_r (temp, "/", &save_ptr); token != NULL; 
    token = strtok_r (NULL, "/", &save_ptr)) {
    if(strlen(token) != 0) {
      memcpy(file_name + strlen(file_name), token, strlen(token)+1);
      memcpy(file_name + strlen(file_name), "/\0", 2);
    }   
  }

  //if last one is / remove it
  if ( strlen(file_name) > 1 && strcmp( &(file_name[strlen(file_name)-1]), "/") == 0 ) 
    memcpy(file_name + strlen(file_name) - 1,"\0",1);

  return file_name;  
}
