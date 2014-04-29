#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>

#include "threads/malloc.h"

#define DIRECT_BLOCKS 10 //added 
#define INDIRECT_BLOCKS 128 //added 
#define DOUBLY_INDIRECT_BLOCKS 128 //added

struct bitmap;


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk{
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    bool isDirectory;           //for subdirectories
    block_sector_t parent_inode;    
    block_sector_t doubly_indirect; /*added. block where doubly indirect struct is stored*/
    uint32_t unused[123];
};

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

/*Added. Each indirect block is an array of INDIRECT_BLOCKS direct blocks. 
Struct also contains number of data blocks used */
struct indirect_block{
    block_sector_t blocks[INDIRECT_BLOCKS];
};


static inline size_t bytes_to_sectors (off_t size);
void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
bool extend(struct inode *);
//bool allocate_indirect(struct indirect_block *dir, int index, size_t sectors)
block_sector_t byte_to_inode_block(struct inode *, off_t pos, bool read);
void inode_lock (const struct inode *inode);
void inode_unlock (const struct inode *inode);
block_sector_t inode_get_parent (const struct inode *inode);
bool inode_add_parent (block_sector_t parent_inode, block_sector_t child_inode);

#endif /* filesys/inode.h */
