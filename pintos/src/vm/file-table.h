#include <stdio.h>
#include "filesys/off_t.h"
#include <list.h>
#include <stdint.h>

struct fte {
	struct list_elem elem;			/* List element */
	void * vaddr;					/* virtual address */
	off_t ofs;						/* Offset */
	uint32_t size;					/* read size */
	bool writable;					/* writable */
};


void free_mapping(struct list *);


void is_written(struct list * , int);
void file_insert(struct list *, void *, off_t, uint32_t, bool);
struct fte * find_fte(struct list *, void *);
bool is_valid_mapping_load(struct list *, void *);
