#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"
#include <list.h>

struct bitmap;




struct cache_entry{
    struct list_elem elem;
    //int sector_ofs;
    disk_sector_t sector_idx;
    uint8_t *data;
    //write_behind에 필요한 정보
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
bool sector_allocate(size_t, struct inode_disk * );
int min(int, int);
void indirect_release(size_t , disk_sector_t );
<<<<<<< HEAD
void indirect_release_temp(size_t, disk_sector_t);
bool inode_indirect(size_t , disk_sector_t);
bool inode_doubly(size_t, struct inode_disk *);


bool direct_allocate(int, int , struct inode_disk *);
bool indirect_allocate(int, int, disk_sector_t);
bool doubly_allocate(int, int, disk_sector_t);



=======

bool inode_indirect(size_t , disk_sector_t);
bool inode_doubly(size_t, struct inode_disk *);

>>>>>>> 8bfb090a4d7031add0f02cb7cc6a27caecdbb7f3
#endif /* filesys/inode.h */