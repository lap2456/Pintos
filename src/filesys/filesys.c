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
  
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  if(success && isDirectory){
    //add . and .. if it is a directory
    struct file * f = filesys_open(file_name); 
    struct dir * d = dir_open(file_get_inode(f)); 

    dir_add(d, ".", inode_sector); 
    struct inode * parent = dir_get_inode(dir); 
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
    //if the name has no length then return null
  if(strlen(name) == 0)
	return NULL;
  char* file_name = get_file_name(name); //grab the filename;
  struct dir *dir = get_this_dir(file_name);
  
  struct inode *inode = NULL;


  if (dir != NULL)
  {
    //if the filename is ".."
    if(strcmp(file_name, "..") == 0) {
	    //and the inode has no parent then free up and return null
	    if(dir_get_parent(dir, &inode) == NULL) {
        free(file_name);
        return NULL;
	    }
    }
	//if the dir is a root dir and the length of the name is 0
	//or if the filename is "."
    else if((is_root_dir(dir) && strlen(file_name) == 0)||
      strcmp(file_name, ".") == 0){
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

struct dir* get_this_dir (const char* name)
{
  /*char string[strlen(name) + 1];
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
  return dir;*/

  //printf("lookup path is %s\n",name);

  //if the string starts with / it is an absolute path
  //root dir
  if (strcmp(name, "/") == 0) 
    return dir_open_root();

  //for the return value
  int success = 0;
  char *token ,*save_ptr;
  char * temp = (char *)malloc(strlen(name) + 1 );
  strlcpy (temp, name, strlen(name) + 1); 

  //open root and start 
  struct dir * current;

  //if it is relative make it absolute 
  if ( name[0] != '/' ) {
    current = dir_reopen(thread_current()->pwd);
  } else {
    current = dir_open_root();
  }
  
  struct inode * nextdir = dir_get_inode(current);

  //go through and check that the previous direcrtories exist
  for (token = strtok_r (temp, "/", &save_ptr); 
    token != NULL; token = strtok_r (NULL, "/", &save_ptr)) {
    //somethings wrong if this happens 
    if (current == NULL ) 
      break;
    //last round has to be not existing
    if (strlen(save_ptr) == 0) {
      success = 1;
      break; 
    }

    //goto next if token is empty in case of //a/
    if(strlen(token) !=  0) {
      //check if this directory exists true if exists
      if ( dir_lookup (current, token,&nextdir) ) {
        //check if it is a directory and then open it
        if(inode_is_dir(nextdir)) {
          dir_close(current);
          current = dir_open(nextdir);
        }
        else break;
      }
      else break;
    }
  }

  if(success) 
    return current; 
  else 
    return NULL;
}

char* get_file_name (const char* name)
{
  char *file_name = (char *) malloc(strlen(name) + 1); 
  char *token, *save_ptr; 
  char * parse = (char*)malloc(strlen(name)+1); 

  strlcpy(parse, name, strlen(name)+1 ); 

  //if the string starts with '/' it is an absolute path
  if(name[0]=='/'){
    memcpy(file_name, "/\0", 2);
  } else {
    //relative path
    memcpy(file_name, "\0", 1); 
  }

  //parse the original name to remove multiple slashes
  //parse the original name to remove multiple //
  for (token = strtok_r (parse, "/", &save_ptr); token != NULL; 
    token = strtok_r (NULL, "/", &save_ptr)) {
    if(strlen(token) != 0) {
    memcpy(file_name + strlen(file_name),token,strlen(token)+1);
    memcpy(file_name + strlen(file_name),"/\0",2);
  }   

  }

  //if last one is / remove it
  if ( strlen(file_name) > 1 && strcmp( &(file_name[strlen(file_name)-1]), "/") == 0 ) 
    memcpy(file_name + strlen(file_name) - 1,"\0",1);

  return file_name;  
}
