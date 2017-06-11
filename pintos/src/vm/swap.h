#include <stdio.h>
#include <hash.h>
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/disk.h"


/* 
TRACKS IN-USE AND FREE SWAP SLOTS

1. "Picking an unused swap slot" for evicting a page from its frame to the swap partition
2. "Freeing a swap slot" when its page is read back into frame / page가 swapped 된 process가 terminate

*/

struct swap_entry {
	struct hash_elem hash_elem;			/* Hash table element */
	disk_sector_t disksector;			/* disk sector num */
	void *vaddr;						/* if not swapped : vaddr = NULL */
	tid_t pid;							/* process's pid that allocate this frame */

//	uint32_t paddr;						/* physical address */
	/* 이 data가 file system / swap slot / all-zero page 중 어디에 있는지 알려줘야함 */

};


struct hash swap_table;


void swap_init(void);
unsigned swap_hash (const struct hash_elem *, void * UNUSED);
bool swap_less (const struct hash_elem *, const struct hash_elem *, void * UNUSED);
void swap_in(void *, void *, tid_t);
void swap_out(void *, void *, tid_t);
void free_swap_slot_process(tid_t);
struct swap_entry * swap_lookup_vaddr(void *, tid_t);
