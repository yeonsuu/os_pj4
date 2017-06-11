#include "vm/file-table.h"
#include "filesys/off_t.h"
#include <stdint.h>
#include <list.h>
#include "threads/malloc.h"
#include "vm/s-pagetable.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "threads/vaddr.h"



void
file_insert(struct list *file_table, void * vaddr, off_t ofs, uint32_t size, bool writable){
	//printf("file_insert : vaddr : %p, ofs : %d, size : %d, writable : %d\n", vaddr, ofs, size, writable);
	struct fte *fte;
	fte = malloc(sizeof *fte);
	
	fte -> vaddr = vaddr;
	fte -> ofs = ofs;
	fte -> size = size;
	fte -> writable = writable;
	list_push_back(file_table, &fte->elem);
}

struct fte *
find_fte(struct list *file_table, void *vaddr){
	struct list_elem *e;
	struct fte *fte; 

	for(e = list_begin(file_table); e != list_end(file_table); e= list_next(e)){
		fte = list_entry(e, struct fte, elem);
		if(fte->vaddr == vaddr){
			return fte;
		}
	}
	return NULL;
}


void 
free_mapping(struct list * file_table)
{
	struct list_elem *e;
	struct fte *fte; 
	struct thread * t = thread_current();
	for(e = list_begin(file_table); e != list_end(file_table); e= list_next(e)){
		fte = list_entry(e, struct fte, elem);
		uint32_t kpage = ptov(frame_lookup_vaddr(fte->vaddr, t->tid)->paddr);
		free_frame_entry(fte->vaddr, t->tid);
		s_pte_clear(fte->vaddr, t->tid); 
		pagedir_clear_page (t->pagedir, fte->vaddr, t->tid);

		palloc_free_page(kpage);

	}
}

void
is_written(struct list * file_table, int fd)
{
	struct list_elem *e;
	struct fte *fte; 
	struct thread * t = thread_current();
	lock_acquire(&filesys_lock);
	if(find_file(fd) != NULL){
		for(e = list_begin(file_table); e != list_end(file_table); e= list_next(e)){
			fte = list_entry(e, struct fte, elem);
			if(pagedir_is_dirty (t->pagedir, fte->vaddr)){
				//printf("file_insert : vaddr : %p, ofs : %d, size : %d, writable : %d\n", fte->vaddr, fte->ofs, fte->size, fte->writable);
				file_write_at(find_file(fd)->file, fte->vaddr, fte->size, fte->ofs);
			}
		}
	}
	lock_release(&filesys_lock);

}

bool is_valid_mapping_load(struct list* file_table, void * addr)
{
	struct fte *fte;
	struct list_elem *e;

	fte = list_entry(list_begin(file_table), struct fte, elem);
	if (addr < fte->vaddr)
		return true;

	fte = list_entry(list_rbegin(file_table), struct fte, elem);
	if (addr > fte->vaddr)
		return true;

	return false;
}



