#include <stdio.h>
#include <hash.h>
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/process.h"

struct frame {
	struct list_elem elem;			/* List element */
	void *paddr;					/* physical address */
	void *vaddr;					/* points virtual address page */
	tid_t pid;						/* process's pid that allocate this frame */		
};


struct list frame_table;
struct lock evict_lock;






void frametable_init(void);
void add_frame_entry(void *, void *, tid_t);
void free_frame_entry(void *, tid_t);
struct frame * frame_lookup_paddr(void*);
struct frame * frame_lookup_vaddr(const void *, tid_t);
void * get_free_frame(void);
void free_frame_process(tid_t);

