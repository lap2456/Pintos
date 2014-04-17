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

#define MAX_FSIZE 8069120 //added - max amount of actual data 
#define DIRECT_BLOCKS 10 //added 
#define INDIRECT_BLOCKS 125 //added 
#define DOUBLY_INDIRECT_BLOCKS 125 //added
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))


/*Added. Each indirect block is an array of INDIRECT_BLOCKS direct blocks. 
Struct also contains number of data blocks used */
struct indirect_block{
  block_sector_t owner; //the inode_disk that this indirect block belongs to 
  block_sector_t sector; //the on disk sector containing this struct 
  off_t used; //number of data blocks used 
  block_sector_t blocks[INDIRECT_BLOCKS];
};

/*Added. Each doubly indirect block is an array of 
DOUBLY_INDIRECT_BLOCKS indirect blocks.Each of the 
128 indirect blocks is an array of INDIRECT_BLOCKS direct blocks.*/
struct doubly_indirect_block{
  block_sector_t owner; //the inode_disk that this struct belongs to
  block_sector_t sector; //the on disk sector containing this struct 
  off_t used; //number indirect blocks used 
  struct indirect_block indirects[DOUBLY_INDIRECT_BLOCKS];
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /*First data sector*/
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
   
    /*added */
    block_sector_t direct[DIRECT_BLOCKS]; /*Direct blocks */                
    block_sector_t indirect;        /*added. block where indirect struct is stored*/
    block_sector_t doubly_indirect; /*added. block where doubly indirect struct is stored*/
    
    uint32_t unused[113];
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
    block_sector_t sector;              /* Sector number of disk location. Gives the information needed
                                        to find inode on disk  */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */

    off_t total_length;                 /*added.used for read race condition after EOF*/
    off_t data_length;                  /*added. total number of data bytes stored*/
    struct indirect_block ind_data;     /*added. */
    struct doubly_indirect_block dbl_ind_data; /*added. */

  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT(inode != NULL); 
  if(pos<inode->data.length){
    return inode->data.start + pos/BLOCK_SECTOR_SIZE; 
  } else return -1; 
}


/*Added. Reads data from disk into cache entry*/
void copy_to_inode(struct block * block, block_sector_t sector, void * buffer){  
  block_read(block, sector, buffer); 
  //memcpy(buffer, buf->data, BLOCK_SECTOR_SIZE); 
}


/*Added. Returns the block within INODE that corresponds to the 
byte offset POS.*/
block_sector_t byte_to_inode_block(struct inode *inode, off_t pos, bool read){ 
  ASSERT (inode != NULL);
  
  size_t block = pos/BLOCK_SECTOR_SIZE; //gets the block 

  if(pos < inode->data.length){ 
    if(block < DIRECT_BLOCKS){ 
      return inode->data.direct[block]; 
    } else if (block < DIRECT_BLOCKS + INDIRECT_BLOCKS) {
      block -= DIRECT_BLOCKS; //get past direct blocks 
      copy_to_inode(fs_device, inode->data.indirect, &inode->ind_data); //put information into ind_data
      return inode->ind_data.blocks[block]; 
    } else if (block < DIRECT_BLOCKS + INDIRECT_BLOCKS + DOUBLY_INDIRECT_BLOCKS){ 
      block -= (DIRECT_BLOCKS + INDIRECT_BLOCKS); //get past direct and indirect blocks
      /*First get indirect block*/
      off_t index = block/INDIRECT_BLOCKS; 
      copy_to_inode(fs_device, inode->data.doubly_indirect, &inode->dbl_ind_data); 

      /*Get the data block*/
      off_t ind_index = block%INDIRECT_BLOCKS; 
      struct indirect_block *ind = NULL; 
      ind = calloc(1, sizeof(struct indirect_block)); 
      ind = &inode->dbl_ind_data.indirects[index];  
      block_sector_t ret = ind->blocks[ind_index]; 
      free(ind); 
      return ret; 
    }
  } else { //past EOF
    //ASSERT(1==0); //FAILS HERE 
    if (true) return 0; //cannot read past EOF
    else return extend(inode, pos); 
  }
  return -1;
}

block_sector_t extend(struct inode * inode, off_t pos){ 
  ASSERT(inode != NULL); 
  size_t old_dir = 0, old_ind = 0, old_dbl = 0; 
  size_t new_dir = 0, new_ind = 0, new_dbl = 0; 
  block_sector_t new = 0;
  size_t old_sectors = bytes_to_sectors(inode->data.length); 
  size_t new_sectors = bytes_to_sectors(pos); 

  int old_block = old_dbl/INDIRECT_BLOCKS;

  int old_rem = old_dbl%INDIRECT_BLOCKS; 

  int new_block = new_dbl/INDIRECT_BLOCKS; 
  int new_rem = new_dbl%INDIRECT_BLOCKS; 


  allocate_sectors(old_sectors, &old_dir, &old_ind, &old_dbl); 
  allocate_sectors(new_sectors, &new_dir, &new_ind, &new_dbl); 

  if(new_dir > old_dir){ 
    allocate_direct(inode->data.direct, old_dir, new_dir-old_dir);
    new = inode->data.direct[new_dir -1];
  } 

  if(new_ind > old_ind){
     struct indirect_block *ind = NULL;
     ind = calloc (1, sizeof *ind);
     if (!old_ind){
      if(free_map_allocate(1, &inode->data.indirect)){
        allocate_indirect (ind->blocks, 0, new_ind);
        block_write (fs_device, inode->data.indirect, ind);
      }
      else
          return -1;
     }
     block_read(fs_device, inode->data.indirect, ind);
     allocate_indirect (ind->blocks, old_ind, new_ind - old_ind);
     block_write(fs_device, inode->data.indirect, ind);
     free(ind);
     block_read(fs_device, inode->data.indirect, &inode->ind_data);
     new = inode->ind_data.blocks[new_ind - 1];
  }

  if(new_dbl > old_dbl){
    struct doubly_indirect_block *doubly = NULL;
    doubly = calloc (1, sizeof *doubly);
    if (!old_dbl){
      if(free_map_allocate(1, &inode->data.doubly_indirect)){
        allocate_doubly (doubly->indirects, 0, new_dbl);
        block_write (fs_device, inode->data.doubly_indirect, doubly);
      }
      else
          return -1;  
    }
    block_read (fs_device, inode->data.doubly_indirect, doubly);
    struct indirect_block *ind = NULL;
    ind = calloc (1, sizeof *ind);
    block_read (fs_device, &doubly->indirects[old_dbl], ind);
    allocate_indirect(doubly->indirects, old_rem, INDIRECT_BLOCKS-old_rem);
    free(ind);
    allocate_doubly(doubly->indirects, old_dbl, new_dbl - old_dbl);
    block_write (fs_device, inode->data.doubly_indirect, doubly);
    free(doubly);
    block_read(fs_device, inode->data.doubly_indirect, &inode->dbl_ind_data);
    struct indirect_block *i = NULL;
    i = calloc (1, sizeof *i);
    i = &inode->dbl_ind_data.indirects[new_block];
    new = &i[new_rem-1];
  }
  inode->data.length = pos;
  block_write (fs_device, inode->sector, &inode->data);
  return new; 
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

bool allocate_sectors(size_t sectors, size_t *direct, size_t *indirect, 
  size_t *doubly){ 
  size_t d = 0, i = 0, dbl = 0; 

  d = MIN(sectors, DIRECT_BLOCKS); 
  sectors -= d; 

  i = MIN(sectors, INDIRECT_BLOCKS); 
  sectors -= i; 

  dbl = MIN(sectors/DOUBLY_INDIRECT_BLOCKS, DOUBLY_INDIRECT_BLOCKS);
  sectors -= dbl*INDIRECT_BLOCKS; 

  direct = d; 
  indirect = i; 
  doubly = dbl; 

  //if(sectors){
  //  return false; 
  //} 
  return true; 
}

size_t allocate_direct(block_sector_t *dir, int index, size_t sectors){ 
  int i = index; 
  int count = sectors; 

  static char zeros[BLOCK_SECTOR_SIZE]; 
  while(count>0){
    if(free_map_allocate(1, &dir[i])){
      block_write(fs_device, dir[i], zeros); 
      i++;
    }
    count-=1; 
  }

  return i;
}

size_t allocate_indirect(block_sector_t *ind, int index, size_t sectors){ 
  allocate_direct(ind, index, sectors); 
  return 0; 
}

size_t allocate_doubly(block_sector_t *dbl, int index, size_t sectors){ 
  int i = index; 
  int count = sectors/INDIRECT_BLOCKS;
  if(sectors%INDIRECT_BLOCKS !=0)
    count++; 

  int rem; 
  rem = sectors%INDIRECT_BLOCKS; //number of direct blocks in last IB
  //figure out how many indirect blocks to allocate

  while(count>0){ 
    int num_indirects = INDIRECT_BLOCKS; 
    struct indirect_block *ind_block = NULL; 
    ind_block = calloc(1, sizeof( *ind_block)); 
    if(!free_map_allocate(1, &dbl[i])){
      return 0; 
    } 
    if(count==1 && rem!= 0){
      num_indirects = rem; 
    }
      
    allocate_indirect(ind_block->blocks, 0, num_indirects);

    block_write(fs_device, dbl[i], ind_block); 
    free(ind_block); 
    i++; 
    count-=1; 
  }
  return i; 
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  struct indirect_block *indirect = NULL; //added 
  struct doubly_indirect_block *doubly = NULL; //added 
  size_t dir = 0, ind = 0, dbl = 0;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);


  disk_inode = calloc (1, sizeof *disk_inode);
  indirect = calloc(1, sizeof *indirect); 
  doubly = calloc(1, sizeof *doubly);
  disk_inode->length = length;
  disk_inode->magic = INODE_MAGIC;
  if (disk_inode != NULL && indirect != NULL && doubly != NULL)
    {
      size_t sectors = bytes_to_sectors (length);



      /*added. allocate sectors*/ 
      success = allocate_sectors(sectors, &dir, &ind, &dbl);

      if(!success){ 
        //free everything
        return false; 
      }
     
      

      if(dir){
          allocate_direct(disk_inode->direct, 0, dir); 
      }
      
      if (ind){
          success = free_map_allocate(1, &disk_inode->indirect); 
          if(success){
              allocate_indirect(indirect->blocks, 0, ind); 
              block_write(fs_device, disk_inode->indirect, indirect); 
          }
      } 

      if(dbl) {
        success = free_map_allocate(1, &disk_inode->doubly_indirect); 
        if(success){
          allocate_doubly(doubly->indirects, 0, dbl); 
          block_write(fs_device, disk_inode->doubly_indirect, doubly);
        }
      }

      if(success)
        block_write (fs_device, sector, disk_inode); 

      free(disk_inode);
      free(indirect); 
      free(doubly); 
    }
  return success;
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
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  inode->total_length = inode->data.length; //added
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
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
      //added: WRITE TO DISK HERE
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
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

 
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_inode_block (inode, offset, true);
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
      else if(bytes_read == 0 && sector_ofs!=0)
      {
        block_read (fs_device, sector_idx, buffer);
        memcpy (buffer, buffer + sector_ofs, chunk_size);
      }
      else if(sector_ofs == 0 && chunk_size != BLOCK_SECTOR_SIZE)
      {
        block_read (fs_device, sector_idx, buffer+bytes_read);
        char zeros[BLOCK_SECTOR_SIZE-chunk_size];
        memcpy (buffer+bytes_read+chunk_size, zeros, BLOCK_SECTOR_SIZE-chunk_size);
      }
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
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

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_inode_block(inode, offset, false);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
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
