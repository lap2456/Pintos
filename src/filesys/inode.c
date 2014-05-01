#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"


/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define MAX_FSIZE 8322048 //added - max amount of actual data 
#define DIRECT_BLOCKS 10 //added 
#define INDIRECT_BLOCKS 128 //added 
#define DOUBLY_INDIRECT_BLOCKS 128 //added
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))


/*Added. Each indirect block is an array of INDIRECT_BLOCKS direct blocks. 
Struct also contains number of data blocks used */
struct indirect_block{
    block_sector_t blocks[INDIRECT_BLOCKS];
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk{
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    bool isDirectory;			//for subdirectories
    block_sector_t parent_inode;	
    block_sector_t doubly_indirect; /*added. block where doubly indirect struct is stored*/
    uint32_t unused[123];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
    return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
{
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. Gives the information 							needed to find inode on disk  */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock inode_lock;		//for when we need to use synchronizations
    off_t total_length;                 	/*added.used for read race condition after EOF*/
};

/*Added. Returns the block within INODE that corresponds to the 
byte offset POS.*/
block_sector_t byte_to_inode_block(struct inode *inode, off_t pos, bool read){ 
  ASSERT (inode != NULL);
  if(pos < inode->data.length) { 
    /*First get indirect block*/
    off_t ind_index = (pos / BLOCK_SECTOR_SIZE) / INDIRECT_BLOCKS; 
    block_sector_t indirect_block[128];
    block_read(fs_device, inode->data.doubly_indirect, &indirect_block);

    /*Get the data block*/
    off_t block_index = (pos / BLOCK_SECTOR_SIZE) % INDIRECT_BLOCKS; 
    block_sector_t direct_block[128];
    block_read(fs_device, indirect_block[ind_index], &direct_block);
    block_sector_t ret = direct_block[block_index]; 
    return ret; 
  } 
  else { 
    return -1;
  }
}

bool allocate_indirect(block_sector_t *ind, int index, size_t sectors){
    int i = index;
    int count = sectors;
    static char zeros[BLOCK_SECTOR_SIZE];
    memset(zeros, 0, BLOCK_SECTOR_SIZE);
    while(count>0){
      if(free_map_allocate(1, &ind[i])){
        if(ind[i] == 4096)
          return false;
        block_write(fs_device, ind[i], zeros);
        i++;
      }
      else
        return false;
      count-=1;
    }
    return true;
}

bool extend(struct inode *inode, off_t offset){ 
  ASSERT(inode != NULL); 

  size_t old_sectors = bytes_to_sectors(inode->data.length);
  size_t sectors_to_add = bytes_to_sectors(offset) - old_sectors;

  size_t sectors_left = old_sectors%128;
  if(sectors_left != 0)
    sectors_left = 128 - sectors_left;  //temp = remaining sectors in partial ind block
  size_t indirects_to_add = 0;
  size_t temp = 0;
  if(sectors_left < sectors_to_add){      //if we need to add more sectors than what is in temp
    temp = sectors_to_add - sectors_left; //temp = sectors to add after filling current ind block
    indirects_to_add = temp/128;
    if(temp%128 > 0)
      indirects_to_add += 1;
  }

  block_sector_t indirect_block[128];
  block_sector_t dbl_block[128];
  block_read(fs_device, inode->data.doubly_indirect, &dbl_block);
  size_t indirect_index = old_sectors/128;
  temp = 0;
  static char zeros[BLOCK_SECTOR_SIZE];
  memset (zeros, 0, BLOCK_SECTOR_SIZE);
  while(temp < indirects_to_add){
    free_map_allocate(1, &dbl_block[indirect_index + temp]);
    block_write(fs_device, dbl_block[indirect_index + temp], zeros);
    temp += 1;
  }
  size_t how_many = 0;
  size_t index = 0;
  //if we need to fill a partially filled indirect block
  if(sectors_left > 0){
    how_many = MIN(sectors_left, sectors_to_add);
    block_read(fs_device, dbl_block[indirect_index], indirect_block);
    index = old_sectors%128;
    if(!allocate_indirect(indirect_block, index, how_many)){
      return false;
    }
    sectors_to_add -= how_many;
    block_write(fs_device, dbl_block[indirect_index], indirect_block);
    indirect_index += 1;
  }

  while(sectors_to_add > 0){
    how_many = MIN(sectors_to_add, 128);
    block_read(fs_device, dbl_block[indirect_index], indirect_block);
    if(!allocate_indirect(indirect_block, 0, how_many)){
      return false;
    }
    sectors_to_add -= how_many;
    block_write(fs_device, dbl_block[indirect_index], indirect_block);
    indirect_index += 1;
  }
    
  //write back the double indirect block IFF new indirects were added to it
  if(indirects_to_add > 0)
    block_write(fs_device, inode->data.doubly_indirect, &dbl_block);
    
  inode->data.length = offset;
  block_write(fs_device, inode->sector, &inode->data);
  return true; 
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
    list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isDirectory)
{
    struct inode_disk *disk_inode = NULL;

    ASSERT (length >= 0);
    if(length > MAX_FSIZE){
  //    printf("length over max size\n");
      return false;
    }

    /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
    ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    if (disk_inode != NULL)
    {
      disk_inode->length = length;
    	disk_inode->magic = INODE_MAGIC;
    	disk_inode->isDirectory = isDirectory;
      disk_inode->parent_inode = ROOT_DIR_SECTOR;
    	size_t sectors = bytes_to_sectors (length);  

      size_t num_indirects = sectors/INDIRECT_BLOCKS;
      //if there is a remainder of sectors then we will have a partially filled indirect block
      if(sectors%INDIRECT_BLOCKS > 0)
        num_indirects++;

      /* allocate a block for the doubly indirect block
      if it fails then free up the structs made and return false */
      if(!free_map_allocate(1, &disk_inode->doubly_indirect)){
        free(disk_inode);
        //printf("failed allocation of dbl block\n");
        return false;
      }

      block_sector_t dbl_block[INDIRECT_BLOCKS];

      /* allocate all of the indirect blocks that will be needed
	    if it fails then free up the structs made and return false */
      if(!allocate_indirect(dbl_block, 0, num_indirects)){
        free(disk_inode);
        //printf("failed allocation of ind blocks\n");
        return false;
      }

      block_write(fs_device, disk_inode->doubly_indirect, dbl_block);
      /*now go through those indirect blocks and allocate sectors for their direct blocks
      if it fails then free up the structs made and return false */
      size_t index = 0;
      size_t how_many = 0;
      block_sector_t indirect_block[INDIRECT_BLOCKS];
      while(sectors > 0)
        {
            block_read(fs_device, dbl_block[index], indirect_block);
            how_many = MIN (sectors, INDIRECT_BLOCKS);
            if(!allocate_indirect(indirect_block, 0, how_many))
            {	
              //printf("failed allocation of %d data blocks for ind block %d\n", how_many, index);
              free(disk_inode);
              return false;
            }
            sectors -= how_many;
            block_write(fs_device, dbl_block[index], indirect_block);
            index++;
    	}
      block_write (fs_device, sector, disk_inode);
      free(disk_inode);
      //printf("inode create worked\n");
      return true;
    }
   // printf("disk inode null\n");
    return false;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
    struct list_elem *e;
    struct inode *inode;

    /* Check whether this inode is already open. */
    for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      	inode = list_entry (e, struct inode, elem);
      	if (inode->sector == sector) 
        {
            inode_reopen (inode);
            return inode; 
        }
    }

    /* Allocate memory. */
    inode = calloc (1, sizeof *inode);
    if (inode == NULL){
    	return NULL;
    }

    /* Initialize. */
    list_push_front (&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    block_read (fs_device, inode->sector, &inode->data);
    lock_init(&inode->inode_lock);
    ASSERT(inode!=NULL);
    return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
    if (inode != NULL)
    	inode->open_cnt++;
    //ASSERT(1==0);
    return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
    return inode->sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
    
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
  {
    
    /* Remove from inode list and release lock. */
    //added: RELEASE LOCK
    list_remove (&inode->elem);
      	
    /* Deallocate blocks if removed. */
    if (inode->removed) 
    {
      inode_deallocate (inode); 
      free_map_release (inode->sector, 1);
    }
    else{
      block_write(fs_device, inode->sector, &inode->data);
    }
    free (inode); 
  }
  //else
    //printf("open count too high to close\n");
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
    ASSERT (inode != NULL);
    inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;


  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_inode_block (inode, offset, true);
      if(sector_idx == -1)
        break;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  if(bytes_read != 0)
    free (bounce);

  return bytes_read;
}


/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  if (inode->deny_write_cnt > 0){
    	//printf("write count is too high!!!\n");
      return 0;
  }

  while (size > 0) 
  {
    if(inode->data.length < (offset + size)){
      if(!inode_is_dir(inode))
        inode_lock(inode);
      //once you have the lock do the check again in case someone else already extended while you were waiting on the lock
      if(inode->data.length < (offset + size)){
        extend(inode, offset + size);
      }
      if(!inode_is_dir(inode))  
        inode_unlock(inode);
    }
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_inode_block(inode, offset, false);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = MIN(size, min_left);
    if (chunk_size <= 0)
      break;
	  if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
    {
      /* Write full sector directly to disk. */
      block_write (fs_device, sector_idx, buffer + bytes_written);
    }
    else 
    {
    /* We need a bounce buffer. */
      if (bounce == NULL) 
      {
        bounce = malloc (BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      /* If the sector contains data before or after the chunk
      we're writing, then we need to read in the sector
      first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left) 
        block_read (fs_device, sector_idx, bounce);
      else
        memset (bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      block_write (fs_device, sector_idx, bounce);
    }
    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free (bounce);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

void inode_deallocate (struct inode *inode)
{
  size_t sectors = bytes_to_sectors (inode->data.length);
  int i;
  block_sector_t dbl_block[128];
  block_sector_t ind_block[128];
  block_read(fs_device, inode->data.doubly_indirect, dbl_block);
  size_t how_many = 0;
  size_t index = 0;
  while(sectors > 0){
    how_many = MIN(sectors, 128);
    block_read(fs_device, dbl_block[index], ind_block);
    inode_deallocate_indirect(ind_block, how_many);
    sectors -= how_many;
    free_map_release(dbl_block[index], 1);
    index += 1;
  }
  free_map_release(inode->data.doubly_indirect, 1);	 
}

void inode_deallocate_indirect (block_sector_t *ind, size_t data_ptr)
{
  size_t i;
  for(i=0; i<data_ptr; i++)
    free_map_release(ind[i], 1);
}

block_sector_t inode_return_parent (const struct inode *inode)
{
  return inode->data.parent_inode;
}

int inode_return_open_cnt (const struct inode *inode)
{
  return inode->open_cnt;
}

bool inode_add_parent (block_sector_t parent_inode, block_sector_t child_inode)
{
  struct inode* inode = inode_open(child_inode);
  if(inode==NULL)
    return false;
  inode->data.parent_inode = parent_inode;
  inode_close(inode);
  return true;
}

bool inode_is_dir (const struct inode *inode)
{
  return inode->data.isDirectory;
}

void inode_lock (const struct inode *inode)
{
  lock_acquire(&((struct inode *)inode)->inode_lock);
}

void inode_unlock (const struct inode *inode)
{
  lock_release(&((struct inode *) inode)->inode_lock);
}
