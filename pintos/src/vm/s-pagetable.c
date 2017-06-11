#include "vm/s-pagetable.h"
#include <stdio.h>
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include <list.h>
#include "vm/swap.h"
#include "vm/frame.h"
#include "vm/file-table.h"
#include "filesys/file.h"
#include "vm/mmap-table.h"


struct list s_page_table;
struct lock s_pt_lock;
struct lock swapin_lock;
/*
  1. page fault가 난 page를 supplemental page table에 위치
  memory reference가 valid 이면 -> supplemental page table entry 를 이용하여 
  "file system" 혹은 "swap slot" 에 있을, 혹은 그냥 "all-zero page"를 locate
  (entry의 paddr == NULL 이면 
  메모리에 남은 자리가 있는지 보고 uint32_t free_frame = get_free_frame() 
  ( 이 함수 안에서 free frame없으면 알아서 swap out까지 해서 빈 프레임 넘겨줌 )
  위 함수 통과하면 free_frame은 빈 주소
	
  swap in(free_frame, va, pid)해야해 -> swaptable이 va, pid가지고 해당 disksector찾아)
  (if you implement sharing, page's data 는 이미 page frame에 있으나 page table에 없을 수 있다)

  2. page를 저장할 frame을 가져온다. (4.1.5 Managing Frame Table)
  (if you implement sharing, 우리가 필요한 data는 이미 frame에 있을 수 있다 -> you must be able to locate that frame)

  3. Fetch data into frame <- file system에서 가져오거나, swap하거나, zeroing it ...
  (if you implement sharing, page you need는 이미 frame에 있을 수 있다 -> no action is necessary)

  4. fault가 발생한 page table entry의 fulting virtul address-> physical page 이도록 만들어라 (userprog/pagedir.c) 
*/

void 
init_s_page_table(void)
{
	list_init(&s_page_table);
	lock_init(&s_pt_lock);
	lock_init(&swapin_lock);
}




/* 
1. page fault가 발생했을 시 -> kernel이 s_pt에서 fault가 일어난 virtual page를 찾아,
what data should be there을 검색

2 process가 terminate 할 때 -> kernel이 s_pt에서 ,
what resources to free를 찾음 

*/

/*
	void s_pte_insert(void *va, void *pa, tid_t pid)
	from pagedir_set_page()
	
	1. when need add mapping, add mapping to supplemental page table also.
	2. latest one is always on the front, oldest one is always on the back.
	3. (va, pid) combination is the key.
	4. always add mapping to frame table too. (두 테이블을 맞게 유지해주기위해)

*/
void
s_pte_insert(void *va, void *pa, tid_t pid, int mmap_id)
{
	//table entry에 va, pa, pid를 넣어서 table에 추가해준다.
	//printf("s_pte_insert paddr %p, vaddr %p, pid %d\n", pa, va, pid);
	uint32_t vaddr = va;
	//uint32_t paddr = pa;
	struct s_pte *pte;
	struct s_pte *find_pte;
	pte = malloc(sizeof *pte);

	pte->vaddr = vaddr;
	pte->paddr = pa;
	pte->pid = pid;
	pte->mmap_id = mmap_id;
	
	// (LAZY LOADING)
	if (pa == NULL)
		pte->is_exec = true;
	else
		pte->is_exec = false;
		
	lock_acquire(&s_pt_lock);
	find_pte = find_entry((void *)vaddr, pid);
	
	
	if (find_pte !=NULL){
		//pte->is_exec = find_pte->is_exec;
		list_remove(&find_pte->elem);
		free(find_pte);
	}
	list_push_front(&s_page_table, &pte->elem);
	lock_release(&s_pt_lock);
	if (pa !=NULL)			//(LAZY LOADING)
		add_frame_entry(pa, va, pid);
}

/*
	void s_pte_clear(void *va, tid_t pid)
	from pagedir_clear_page()
	
*/
void
s_pte_clear (void *va, tid_t pid)
{
	//table entry 중에서 va, pid를 가진 애를 delete 함
	
	//printf("s_pte_clear vaddr %p, pid %d\n", va, pid);
	lock_acquire(&s_pt_lock);
	struct s_pte *entry = find_entry(va, pid);
	ASSERT (entry != NULL);
	entry->paddr = NULL;

	

	list_remove(&entry->elem);
	list_push_front(&s_page_table, &entry->elem);

	lock_release(&s_pt_lock);
		//frame table에서 해당 frame에 대한 mapping제거

}
/* 
	
	from page_fault()
	
	1. find the page entry that is not in physical memory now
*/
void *
find_s_pte (void *vaddr, tid_t pid)
{
	
		/*
	va, pid를 가진 s_pagetable entry를 찾아서
	(entry의 paddr == NULL 이면 
  메모리에 남은 자리가 있는지 보고 uint32_t free_frame = get_free_frame() 
  ( 이 함수 안에서 free frame없으면 알아서 swap out까지 해서 빈 프레임 넘겨줌 )
  위 함수 통과하면 free_frame은 빈 주소
	
  swap in(free_frame, va, pid)해야해 -> swaptable이 va, pid가지고 해당 disksector찾아)

	*/
	//printf("find_s_pte\n");
	
	lock_acquire(&evict_lock);
	lock_acquire(&s_pt_lock);
	struct s_pte *entry = find_entry(vaddr, pid);
	if( entry == NULL)
	{
		lock_release(&s_pt_lock);
		lock_release(&evict_lock);
		return NULL;
	}
	
	if (entry->paddr == NULL)		//swap out된 경우
	{
		lock_release(&s_pt_lock);

		void *free_frame = get_free_frame();				//if memory full -> swap out
		struct process *p = find_process(pid);
		if (p == NULL || p->thread->pagedir ==NULL)
		{
			lock_release(&evict_lock);
			return NULL;
		}
		uint32_t *pd;
		pd = p->thread->pagedir;
		bool writable;

		/* lazy loading & mmap */
		
		if (entry->mmap_id >0)
		{
			//printf("mmap_id : %d\n", entry->mmap_id);
			struct mapping * map = find_mapping_id(&p->mapping_list, entry->mmap_id);
			ASSERT(map !=NULL);
			struct fte * fte_;
			fte_ = find_fte(&map->file_table, vaddr);
			ASSERT(fte_ != NULL);
			struct file * f = map->file;
			ASSERT(map->file != NULL);

			//printf("free frame %p, fte size : %d, ofs : %d\n", free_frame, fte_->size, fte_->ofs);
			
			int re =file_read_at(f, ptov(free_frame), fte_->size, fte_->ofs);
			//printf("re : %d\n", re);
				memset(ptov(free_frame) + fte_->size, 0, PGSIZE-fte_->size);
				writable = fte_->writable;
			
		}
		

		
		else if (entry->is_exec)		
		{
			
			struct fte * fte_;
			fte_ = find_fte(&p->load_file_table, entry->vaddr);
			struct file * f = p->exec_file;
			file_read_at(f, ptov(free_frame), fte_->size, fte_->ofs);
			memset(ptov(free_frame) + fte_->size, 0, PGSIZE-fte_->size);
			writable = fte_->writable;
		}
	
		else
		{
			//printf("swap in\n");
			swap_in(free_frame, vaddr, pid);
			writable = pagedir_is_writable(pd, vaddr);
		}


		
		
		pagedir_set_page (pd, vaddr, ptov(free_frame), writable, entry->mmap_id);	
		lock_release(&evict_lock);
		return vaddr;
	}
	else{
		printf(" find spte entry paddr : %p, vaddr : %p, pid :%d\n", entry->paddr, entry->vaddr, entry->pid);
		lock_release(&evict_lock);
		ASSERT(0);
		printf("find_s_pte FAIL\n");
		find_process(pid)->exit_status = -1;
      	thread_exit();

      	return NULL;
	}
	
	
}


/* find a victim frmae with FIFO algorithm. called within lock */
struct s_pte *
get_victim (void)
{
	//printf("get_victim\n");
	struct s_pte *pte;
	lock_acquire(&s_pt_lock);
	pte = list_entry(list_back(&s_page_table), struct s_pte, elem);
	/* page linear - don't swap out stack space */
	
	while (pte->paddr == NULL && find_process(pte->pid) != NULL){

		//ASSERT(find_process(pte->pid));
		//ASSERT(!find_process(pte->pid)->is_dead);

		list_remove(&pte->elem);
		list_push_front(&s_page_table, &pte->elem);
		pte = list_entry(list_back(&s_page_table), struct s_pte, elem);

	}
	lock_release(&s_pt_lock);

	//printf("victim vaddr : %p, paddr : %p, tid : %d\n", pte->vaddr, pte->paddr, pte->pid);

	return pte;
}


struct s_pte *
find_entry (void *vaddr, tid_t pid)
{
	
	struct list_elem *e;
  	struct s_pte* pte;
  	for(e = list_begin(&s_page_table); e!= list_end(&s_page_table); e = list_next(e)){
	    pte = list_entry(e, struct s_pte, elem);
	    if (pte->pid == pid && pte->vaddr == vaddr){

	    	return pte;
	    }
  	}
	return NULL;		// no entry found

}

void
free_s_pte_process(tid_t pid)
{
	struct list_elem *e;
  	lock_acquire(&s_pt_lock);
  	
  	e = list_begin(&s_page_table);
  	while (e!= list_end(&s_page_table))
  	{
  		struct s_pte* pte;
  		pte = list_entry(e, struct s_pte, elem);
  		ASSERT(e!= list_end(&s_page_table));
  		e = list_next(e);
	    if (pte->pid == pid){
	    	void * vaddr = pte->vaddr;
	    	list_remove(&pte->elem);
	    	
	    	free(pte);
	    	//free_frame_entry(vaddr, pid);
	    	
	    }
	   
	    	
  	}
  	lock_release(&s_pt_lock);

}

/*
	when process terminate -> free entries in s_pt too
*/

