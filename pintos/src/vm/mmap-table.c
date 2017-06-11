#include "vm/mmap-table.h"

#include "vm/file-table.h"
#include <list.h>



struct mapping *
find_mapping_vaddr(struct list * mapping_list, void *vaddr)
{
	struct list_elem *e;
	struct mapping *mapping;
	
	for(e = list_begin(mapping_list); e != list_end(mapping_list); e = list_next(e)){
		mapping = list_entry(e, struct mapping, elem);
		if(mapping -> start <= vaddr &&  vaddr < (mapping->start)+(mapping->size)){
			return mapping;
		}
	}
	return NULL;
}

struct mapping *
find_mapping_id(struct list * mapping_list, int id)
{
	struct list_elem *e;
	struct mapping *mapping;
	
	for(e = list_begin(mapping_list); e != list_end(mapping_list); e = list_next(e)){
		mapping = list_entry(e, struct mapping, elem);
		if(mapping -> id == id){
			return mapping;
		}
	}
	return NULL;
}

