#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

bool dir_is_empty (struct inode *inode);
char * get_name_only (const char* path);

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  //for(ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e) 
    //printf("name of this entry is: %s\n", e.name);
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  
  if(strcmp(name, "/") == 0 && inode_get_inumber(dir_get_inode(dir)) == ROOT_DIR_SECTOR)
      *inode = inode_reopen(dir_get_inode(dir));
  else{
    inode_lock(dir_get_inode((struct dir *) dir));
    name = get_name_only(name);
    if (lookup (dir, name, &e, NULL))
      *inode = inode_open (e.inode_sector);
    else
      *inode = NULL;
    inode_unlock(dir_get_inode((struct dir *) dir));
  }
  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  name = get_name_only(name);

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  
    /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX){
    //printf("invalid name\n");
    return false; 
  }

  inode_lock(dir_get_inode(dir));

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL)){
    //printf("name already in use\n");
    goto done;
  }

  //if(!inode_add_parent(inode_get_inumber(dir_get_inode(dir)), inode_sector))
    //goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
  //printf("success after write at is :%d\n", success);

 done:
  inode_unlock(dir_get_inode(dir));
  //free(name);
  return success;
}

/*
void
print_dir_entries (struct dir *dir){
  struct dir_entry e;
  off_t ofs;
  struct inode *inode = dir_get_inode(dir);
  printf("inode sector is: %d\n", inode_get_inumber(inode));
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e){
    if(e.in_use)
      printf("dir entry: %s\n", e.name);
  }
}
*/

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  //print_dir_entries(dir);
  inode_lock(dir_get_inode(dir));
  name = get_name_only(name);
  //struct inode * myinode = dir_get_inode(dir);
  //printf("directory's inode sector is: %d  and the root directory's sector is %d\n", inode_get_inumber(myinode) ,ROOT_DIR_SECTOR);
  //printf("name is %s\n", name);
  //printf("chdir part 1\n");
  //myinode = dir_get_inode(thread_current()->pwd);
  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs)){
    //printf("lookup failed\n");
    goto done;
  }
  //printf("chdir part 2\n");

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL){
    //printf("inode is null\n");
    goto done;
  }
  //printf("chdir part 3\n");

  if(inode_is_dir(inode)){
    if(!is_root_dir(dir))
      inode_close(inode);
    if(inode_return_open_cnt(inode) > 1){
        //printf("Inode sector is: %d\n", inode_get_inumber(inode));
        //printf("Inode is still open!\n");
        goto done;
      }
    if(!dir_is_empty(inode)){
      //printf("Dir is not empty\n");
      goto done;
    }
  }

  //printf("chdir part 4\n");

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e){
    goto done;
  }

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_unlock(dir_get_inode(dir));
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  while (inode_read_at(dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
  {
    dir->pos += sizeof e;
    //printf("pos is now: %d\n", dir->pos);
    if (e.in_use && strcmp(e.name, ".") != 0 && strcmp(e.name, "..") != 0)
    {
      strlcpy (name, e.name, NAME_MAX + 1);
      return true;
    }
  }
  return false;
}

bool dir_is_empty (struct inode *inode)
{
  struct dir_entry e;
  off_t pos = 40; //overcome the default "." and ".."
  while(inode_read_at (inode, &e, sizeof e, pos) == sizeof e){
    pos += sizeof e;
    if(e.in_use){
      return false;
    }
  }
  return true;
}

bool is_root_dir (struct dir* dir)
{
  //if not a directory then obviously return false
  if(!dir)
    return false;
  //if this dir's inode's sector is the root dir sector then return true
  if(inode_get_inumber(dir_get_inode(dir)) == ROOT_DIR_SECTOR)
    return true;
  return false;
}

bool dir_get_parent (struct dir* dir, struct inode **inode)
{
    block_sector_t sector = inode_return_parent(dir_get_inode(dir));
    *inode = inode_open (sector);
    return *inode != NULL;
}

char * get_name_only (const char* path)
{
  char *parse = (char *) malloc(strlen(path) + 1);
  strlcpy (parse, path, strlen(path) + 1);
  //if we have no file name and just a slash
  if(strcmp(parse, "/") == 0)
      //then return that slash
      return parse;
  char *token, *save_ptr;
  //parse through the path until you reach the last token to grab which is the file name
  for(token = strtok_r (parse, "/", &save_ptr); token != NULL; token = strtok_r (NULL, "/", &save_ptr)){
    if(strlen(save_ptr) == 0)
        break;
  }
  return token;
}
