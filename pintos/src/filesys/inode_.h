#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"
#include <list.h>

struct bitmap;


struct list cache_list;


struct cache_entry{
	struct list_elem elem;
	//int sector_ofs;
	disk_sector_t sector_idx;
	uint8_t *data;
	//write_behind에 필요한 정보
};


struct inode_disk
  {
    disk_sector_t start;              /* First data sector. */
    struct list disk_block_list;        /* block list of this inode */
    disk_sector_t indirect1;
    disk_sector_t indirect2;
    disk_sector_t doubly;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[118];               /* Not used. */
  };



void inode_init (void);
bool inode_create (disk_sector_t, off_t);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
struct cache_entry * is_hit(disk_sector_t);
struct cache_entry * new_entry(void);

void inode_direct(size_t, struct inode_disk *);
void inode_indirect(size_t, disk_sector_t);
void inode_indirect_close(size_t, disk_sector_t);
void inode_doubly(size_t, disk_sector_t);
void inode_doubly_close(size_t, disk_sector_t);
int min(int , int );


#endif /* filesys/inode.h */
