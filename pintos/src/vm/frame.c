#include "vm/frame.h"
#include <stdio.h>
#include "threads/synch.h"
#include "threads/palloc.h"
#include "userprog/syscall.h"
#include <hash.h>
#include "threads/vaddr.h"
#include "vm/s-pagetable.h"
#include "userprog/process.h"
#include "vm/swap.h"


struct list frame_table;
struct lock frame_lock;
struct lock evict_lock;

/* init frame table, each entry would be inserted or replaced 
	when s_page_table entry added */
void 
frametable_init(void)
{	
	list_init(&frame_table);
	lock_init(&frame_lock);
	lock_init(&evict_lock);
}

/*
	void add_frame_entry(void *paddr, void *vaddr, tid_t pid)
	from s_pte_insert()

	1. for every new mapping with frame addr & page addr, 
		add new entry to frame table too.
	2. use hash_replace that insert the entry
		or replace(if there is) (remove the entry and insert new again).
*/

void
add_frame_entry(void *paddr, void *vaddr, tid_t pid)
{
	//printf("add_frame_entry vaddr : %p, paddr : %p\n", vaddr, paddr);

	struct frame *f;
	
/*
	
	f->paddr = paddr;
	f->vaddr = vaddr;
	f->pid = pid;
	hash_replace(&frame_table, &f->hash_elem);
*/	
	lock_acquire(&frame_lock);
	f= frame_lookup_paddr (paddr);
	
	if (f!=NULL){
		f->vaddr = vaddr;
		f->pid = pid;
	}
	else{
		struct frame *new_f;
		new_f = malloc(sizeof *f);
		new_f->paddr = paddr;
		new_f->vaddr = vaddr;
		new_f->pid = pid;
		list_push_back(&frame_table, &new_f->elem);
	}
	lock_release(&frame_lock);
}


/* 
	find frame table entry with va, pid
	and then delete free entry 
*/
void
free_frame_entry(void *va, tid_t pid)
{
	//printf("free_frame_entry\n");
	struct frame* f;
	lock_acquire(&frame_lock);
	f = frame_lookup_vaddr(va, pid);
	if (f != NULL){
		list_remove (&f->elem);
	
		free(f);
	}
	lock_release(&frame_lock);

	/*
	if (f != NULL){
		f->vaddr = NULL;
		f->pid = NULL;
	}
	*/
	

}	


/* Returns the frame containing the given physical address (entry), 
	or a null pointer if no such frame exists */
struct frame *
frame_lookup_paddr (void* paddr)
{
	struct list_elem *e;
  	struct frame* f;
  	for(e = list_begin(&frame_table); e!= list_end(&frame_table); e = list_next(e)){
		f = list_entry (e, struct frame, elem);
		if (f->paddr == paddr){
			break;
		}
		f = NULL;
	}
	//lock_release(&frame_lock);

	return NULL;
}


/* Returns the frame containing the given virtual address and pid, 
	or a null pointer if no such frame exists */
struct frame *
frame_lookup_vaddr(const void *vaddr, tid_t pid)
{
	
	struct list_elem *e;
  	struct frame* f;
  	for(e = list_begin(&frame_table); e!= list_end(&frame_table); e = list_next(e)){
		f = list_entry (e, struct frame, elem);
		if (f->vaddr == vaddr && f->pid == pid){
			//lock_release(&frame_lock);
			break;
		}
		f = NULL;

	}
	//lock_release(&frame_lock);
	return f;

}

/* Get free frame from frame table and return uint32_t paddr */
void *
get_free_frame(void)
{
	//lock_acquire(&lock);
	//printf("get_free_frame\n");
	void *vaddr;
	
	vaddr = palloc_get_page(PAL_USER | PAL_ZERO); 		//find a empty entry.
	//case 1. if there are free frame
	if(vaddr != NULL){
		
		return (void *)vtop(vaddr);
	}
	//case 2. there are no free frame -> eviction
	else{
		/*
		1. choose frame to evict (page table의 accessed, dirty bits)
		2. remove references to the frame from any page table
		3. write the page to the file system / swap
		*/
		//get victim's paddr from s-pagetable

		struct s_pte *victim = get_victim();
		uint32_t paddr = (uint32_t) victim->paddr;
		uint32_t vaddr = (uint32_t) victim->vaddr;
		ASSERT((void *) paddr < PHYS_BASE);
		uint32_t *pd;
		struct process *p = find_process(victim->pid);

		free_frame_entry(vaddr, victim->pid);
		s_pte_clear(vaddr, victim->pid); 
		if (p!= NULL && p->thread->pagedir !=NULL)
			pagedir_clear_page (p->thread->pagedir, vaddr, victim->pid);


		if(!victim->is_exec)
			swap_out(paddr, vaddr, victim->pid);		//physical memory에 빈공간 만들어
		

		
		

		return (void *) paddr;
		//swap_in에서 victim_frame->vaddr로 넣어줌 
		
	}
	
}

void
free_frame_process(tid_t pid)
{
	struct list_elem *e;
  	lock_acquire(&frame_lock);
  	
  	e = list_begin(&frame_table);
  	while (e!= list_end(&frame_table))
  	{
  		struct frame* f;
  		f = list_entry(e, struct frame, elem);
  		ASSERT(e!= list_end(&frame_table));
  		e = list_next(e);
	    if (f->pid == pid){
	    	list_remove(&f->elem);
	    	
	    	free(f);
	    	//free_frame_entry(vaddr, pid);
	    	
	    }
	   
	    	
  	}
  	lock_release(&frame_lock);

}


