#include <stdio.h>
#include <list.h>
#include <stdint.h>


struct mapping {
	struct list_elem elem;			/* List element */
	void * start;					/* virtual address */
	uint32_t size;					/* page size */
	int id;						/* mapping ID */
	struct file *file;				/* mapped file */
	int fd;
	struct list file_table;			/* File table */
};


struct mapping * find_mapping_vaddr(struct list *, void *);
struct mapping * find_mapping_id(struct list * , int );
