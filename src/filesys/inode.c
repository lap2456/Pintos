#include "filesys/inode.h"



/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define MAX_FSIZE 8322048 //added - max amount of actual data 

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))




/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
    return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}



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

/*An indirect block can contain up to 128 points to direct data blocks, each of which
are BLOCK_SECTOR_SIZE big. 
DIRECTS should be how many data block sectors we want to allocate.*/
bool allocate_indirect(block_sector_t *ind, int index, size_t directs){
    int i = index;
    int count = directs;
    //printf("allocating new indirect. sectors = %d \n", sectors);
    static char zeros[BLOCK_SECTOR_SIZE];
    while(count>0){
        if(free_map_allocate(1, &ind[i])){
            block_write(fs_device, ind[i], zeros);
            i++;
        }
        else
            return false;
        count-=1;
    }
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
      //printf("length over max size\n");
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
    //printf("disk inode null\n");
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
            free_map_release (inode->sector, 1);
            inode_deallocate (inode); 
        }
        else
	    block_write(fs_device, inode->sector, &inode->data);
   	

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
  uint8_t *bounce = NULL;

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
  free (bounce);

  return bytes_read;
}

/*Length is the total length of the file after write. It is 
equal to offset (point to start writing) + size of write.*/
bool file_extend(struct inode *inode, off_t length){
 // printf("Extending file... \n"); 
  bool perfect = false; 
  size_t added = 0; //number of sectors we've added 
  off_t old_sectors = inode->total_length; //how many sectors we used to fill up
  size_t indirects_to_add = 0; //how many indirect blocks we need to add 
  if(old_sectors % INDIRECT_BLOCKS == 0) { //if we used to perfectly fill up a certain amount of ind. blocks 
    indirects_to_add += 1; //we need to start a new indirect block
    perfect = true;  
  }
  off_t new_sectors = bytes_to_sectors(length); 


  //get the number of blocks in the file
  off_t add_sectors = new_sectors - old_sectors; //how many additional sectors we need to add

  if(add_sectors == 0) return true; 
  
  bool success = false; 

  struct indirect_block* temp; 
  temp = malloc(sizeof(*temp)); 
  block_sector_t * dbl_block;
  dbl_block = malloc(128 * sizeof(block_sector_t));  
  block_read(fs_device, inode->data.doubly_indirect, dbl_block);
  static char zeros[BLOCK_SECTOR_SIZE];
  

  size_t indirect_index = old_sectors/(INDIRECT_BLOCKS*BLOCK_SECTOR_SIZE); //how many indirects we have filled or partially filled up
  size_t dir_index = old_sectors%INDIRECT_BLOCKS; //how many data sectors this indirect block has filled up
  size_t add_directs = 0;
  //if you have a partially filled indirect block and need to fill it up
  if(!perfect) {   
    //read that indirect block (disk) into temp ind. block struct
    block_read(fs_device, dbl_block[indirect_index], temp);
    

    size_t left = INDIRECT_BLOCKS - dir_index; //how many sectors are left in this indirect block

    add_directs = MIN(left, add_sectors); 
    allocate_indirect(temp->blocks, dir_index, add_directs); 
    indirect_index+=1; 
    add_sectors -= add_directs; //how many sectors do we still need to add?
    added += add_directs; 
  }
  
  //are we done adding stuff?
  if(add_sectors ==0)
    return true; 

  //each indirect block can hold 128 data sectors 
  indirects_to_add += add_sectors/INDIRECT_BLOCKS; //total number of indirects we need to add



  /* allocate all of the indirect blocks that will be needed
  if it fails then free up the structs made and return false */


  block_write(fs_device, inode->data.doubly_indirect, dbl_block);
  
  //if(!allocate_indirect(dbl_block, indirect_index, indirects_to_add)){
  //  printf("failed adding indirect blocks in extend\n");
  //  return false;
  //}
  /*Now go allocate sectors for each indirect block*/
  while(add_sectors > 0) {
    block_read(fs_device, dbl_block[indirect_index], temp);
    add_directs = MIN(INDIRECT_BLOCKS, add_sectors); 
    allocate_indirect(temp->blocks, 0, add_directs); 
    indirect_index+=1; 
    add_sectors -= add_directs; 
    added += add_directs; 
    block_write(fs_device, dbl_block[indirect_index], temp); 
  }
    
  //write back the double indirect block IFF new indirects were added to it
  if(indirects_to_add>0){
    block_write(fs_device, inode->data.doubly_indirect, &dbl_block);
  }

  block_write(fs_device, inode->sector, &inode->data); //write to inode disk data
  return true; 
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   File growth is implemented */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  //added for debugging 
  //printf("byte offset we are writing at is %d, we are writing %d bytes and the length is %d\n", offset, size, inode_length(inode));
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  bool got_lock = false; 

  if (inode->deny_write_cnt){
      //added print for debugigng 
    	//printf("write count is too high!!!\n");
      return 0;
  }


  //check if it is necessary to extend the file
  off_t old = bytes_to_sectors(inode->data.length);
  //printf("File size in bytes: %d \n", inode->data.length);
  if((offset+size)/BLOCK_SECTOR_SIZE > old){
    //acquire the lock 
    if(!inode->data.isDirectory){
      lock_acquire(&inode->inode_lock); 
      got_lock = true; 
    }
    //printf("Extending File"); 
    if(!file_extend(inode, offset+size)){
      //printf("error extending file"); 
      return 0; 
      inode->data.length = offset+size; //EOF has been moved 
    }  
  }

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
    int chunk_size = MIN(size, min_left);
    if (chunk_size <= 0)
      break;

    else if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
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

  //inode->data.length += bytes_written; 
  //if we got lock, release
  if(got_lock){
    lock_release(&inode->inode_lock);
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
  struct indirect_block ind;
  block_read(fs_device, inode->data.doubly_indirect, &ind);
  size_t num_indirects = sectors/128;
  if(sectors%128 > 0)
	num_indirects++;
  size_t how_many = 0;
  for(i=0; i<num_indirects; i++){
    how_many = MIN(sectors, 128);
    sectors -= how_many;
    inode_deallocate_indirect(&ind.blocks[i], how_many);
  }
  free_map_release(inode->data.doubly_indirect, 1);	 
}

void inode_deallocate_indirect (block_sector_t *sector, size_t data_ptr)
{
  size_t i;
  struct indirect_block ind;
  block_read(fs_device, *sector, &ind);
  for(i=0; i<data_ptr; i++)
    free_map_release(ind.blocks[i], 1);
  free_map_release(*sector, 1);
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


