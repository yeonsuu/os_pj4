#include <stdio.h>
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/process.h"

/* 
1. page fault 이 발생했을 시 -> kernel이 s_pt에서 fault가 일어난 virtual page를 찾아,
what data should be there을 검색

2. process가 terminate 할 때 -> kernel이 s_pt에서 ,
what resources to free를 찾음 

*/

struct s_pte {
	struct list_elem elem;			/* List element */
	void *vaddr;					/* virtual address */
	void *paddr;					/* physical address */
	tid_t pid;						/* process's pid that allocate this frame */
	bool is_exec;					/* true : executable file */
	int mmap_id;					/* 0 : lazy loading, 1~ : mmap , -1 : normal case */
	/* 이 data가 file system / swap slot / all-zero page 중 어디에 있는지 알려줘야함 */
};


struct list s_page_table;

void init_s_page_table(void);
void s_pte_insert(void *, void *, tid_t, int);
void s_pte_clear(void *, tid_t);
void *find_s_pte(void *, tid_t);
struct s_pte * get_victim(void);
struct s_pte * find_entry(void *, tid_t);
void free_s_pte_process(tid_t);

