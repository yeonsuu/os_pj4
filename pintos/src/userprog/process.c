#include "userprog/process.h"
#include "userprog/syscall.h"

#include <debug.h>
#include <list.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threads/malloc.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"


static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);


struct list process_list;

void
process_init (void)
{
  list_init(&process_list);

  struct process *initial_process;
  initial_process = malloc(sizeof *initial_process);
  initial_process -> pid = thread_current() -> tid;
  initial_process -> is_dead = false;
  initial_process -> load_success = false;
  initial_process -> fd_cnt = 2;
  list_init(&initial_process -> file_list);
  list_init(&initial_process -> children_pids);

  //printf("MALLOC struct process / process pid : %d ", thread_current() -> tid);
  
  list_push_back(&process_list, &initial_process->elem);

}
/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  char *file_name_copy;

  tid_t tid;

  struct process *curr_p;
  curr_p = find_process(thread_current()->tid);
  sema_init(&curr_p->sema_pexec, 0);
  sema_init(&curr_p->sema_pwait, 0);

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  file_name_copy = palloc_get_page (0);
  if (file_name_copy == NULL)
    return TID_ERROR;
  strlcpy (file_name_copy, file_name, PGSIZE);

  //1. filename arg1 arg2 .. -> filename
  char *t_name;
  char *save_ptr;
  t_name = strtok_r (file_name_copy, " ", &save_ptr);  

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (t_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR){
    palloc_free_page (fn_copy); 
    palloc_free_page (file_name_copy);
    free(t_name);
  }
  //2. If thread_create(child) success -> add to process_list
  else{
    struct process *child;
    child = malloc(sizeof *child);   
    child->pid = tid;
    child->parent_pid = thread_current()->tid;
    list_init(&child->children_pids);
    child->is_dead = false;
    child->load_success = false;
    list_init(&child->file_list);
    child->fd_cnt = 2;
    
    list_push_back(&process_list, &child->elem);

    //3. Wait until child exec
    sema_down(&curr_p->sema_pexec);

    //4. If load(child) !success -> remove from process_list
    if(!child->load_success){
      list_remove(&child->elem);
      free(child);
      tid = -1;
    }
    //5. If load(child) success -> push it to curr_p's children_pids list
    else{
      struct childpid_elem *child_elem;
      child_elem = malloc (sizeof *child_elem); 
      child_elem->childpid = tid;
      list_push_back(&curr_p->children_pids, &child_elem->elem);
    }
    palloc_free_page (file_name_copy);
  }
  return tid;   
}

/* A thread function that loads a user process and makes it start
   running. */
static void
start_process (void *f_name)
{
  char *file_name = f_name;
  struct intr_frame if_;
  bool success;
  char *token, *save_ptr;
  token = strtok_r (file_name, " ", &save_ptr); 

  /* Initialize interrupt frame and load executable. */  
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  //1. File_deny_write while this file's process is executing
  struct file* file = filesys_open(token);
  if (file != NULL) file_deny_write(file);

  success = load (token, &if_.eip, &if_.esp);

  //2. Hand over to parent process that whether child success or not 
  struct process *curr_p;
  curr_p = find_process(thread_current()->tid);
  curr_p->load_success = success;

  struct process *parent_p;
  parent_p = find_process(curr_p->parent_pid);

  //3. If parent call sema_down -> sema_up
  if(!list_empty(&parent_p -> sema_pexec.waiters))
    sema_up(&parent_p -> sema_pexec);
  
  //4. If load is not success : FREE & sys_exit(-1)
  if (!success) {
    palloc_free_page (file_name);
    file_close(file);
    sys_exit (-1);
  }
  //5. success : Add this file to process's exec_file
  else{
    curr_p->exec_file = file;
  }

  /* Argument Passing */
  char **argv;
  int argc = 0;
  argv = (char **) malloc(100 * sizeof(char *));

  //push argv[n][...]
  while(token != NULL){
    if_.esp -= strlen(token) + 1;
    memcpy(if_.esp, token, strlen(token)+1);
    argv[argc] = (char *) malloc(sizeof(char *));
    argv[argc] = if_.esp;
    argc++;
    token = strtok_r (NULL, " ", &save_ptr);
  }

  //push word-align
  if_.esp -= (uint32_t) if_.esp % 4;

  //push argv[argc]
  if_.esp -= 4;
  *(int *)if_.esp = 0;
  
  //push argv[n]
  int i;
  for (i=argc-1; i>=0; i--){
    if_.esp -= 4;
    *(void **) if_.esp = argv[i];
  }

  //push argv
  if_.esp -= 4;
  *(void **) if_.esp = if_.esp +4;       
  
  //push argc
  if_.esp -= 4;
  *(int *) if_.esp = argc;    
  
  //push return address
  if_.esp -= 4;
  *(int *) if_.esp = 0;       

  //FINAL : free everything
  palloc_free_page (file_name);
  free(argv);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.
   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct process *curr_p;
  struct process *child_p;
  curr_p = find_process(thread_current()->tid);

  //CASE 0: Unvalid child process pid
  if (find_process(child_tid) == NULL){
    return -1;
  }
  //CASE 0: This is not a child of current process 
  if (curr_p->pid != find_process(child_tid)->parent_pid){
    return -1;
  }
  else{
    //CASE 1: child is already dead
    if (find_process(child_tid)->is_dead){
      int exit_status = get_exitstatus(child_tid);
      list_remove(&find_process(child_tid)->elem);
      free(find_process(child_tid));
      return exit_status;
    }
    //CASE 2: child is not dead -> wait for child to exit
    else{
      sema_down(&curr_p->sema_pwait);
      int exit_status = get_exitstatus(child_tid);
      list_remove(&find_process(child_tid)->elem);
      free(find_process(child_tid));
      return exit_status;
    }
  } 
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *curr = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = curr->pagedir;
  if (pd != NULL) 
  {
    /* Correct ordering here is crucial.  We must set
       cur->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
    curr->pagedir = NULL;
    pagedir_activate (NULL);
    pagedir_destroy (pd);
  
    struct process *curr_p;
    struct process *parent_p;
    struct process *child_p;
    curr_p = find_process(curr->tid);
    parent_p = find_process(curr_p->parent_pid);

    curr_p -> is_dead = true;
    printf("%s: exit(%d)\n", thread_name(), curr_p->exit_status);

    //CASE 0: NO Parent
    if (parent_p == NULL){
      list_remove(&curr_p->elem);
      free(curr_p);
    }
    //CASE 1: Parent already dead
    else if (parent_p->is_dead == true){
      list_remove(&curr_p->elem);
      free(curr_p);
    }    

    //CASE 2: parent is waiting for exit
    if(!list_empty(&parent_p->sema_pwait.waiters) )
      sema_up(&parent_p->sema_pwait);


    //FREE: FREE (is_dead)ychildren's process structure & remove from process_list
    if (!list_empty(&curr_p -> children_pids)){
      struct list_elem *e;
      struct childpid_elem *child_elem;
      
      e = list_begin(&curr_p -> children_pids);
      while(e != list_end(&curr_p -> children_pids))
      {
        child_elem = list_entry(e, struct childpid_elem, elem);
        child_p = find_process(child_elem->childpid);
        e = list_next(e);

        if ((child_p != NULL) && (child_p->is_dead == true)){
          list_remove(&child_p->elem);
          free(child_p);
        }
      }
    }
    //FREE: FREE 'file_liset'
    struct fd_file * fd_file;
    while (!list_empty (&curr_p->file_list))
    {
      struct list_elem *e = list_pop_front (&curr_p->file_list);
      fd_file = list_entry(e, struct fd_file, elem);
      free(fd_file->file);
      free(fd_file);
    }

    //FREE: FREE 'childpid_elem'
    struct childpid_elem * childpid;
    while (!list_empty (&curr_p->children_pids))
    {
      struct list_elem *e = list_pop_front (&curr_p->children_pids);
      childpid = list_entry(e, struct childpid_elem, elem);
      free(childpid);
    }

    //FREE: FREE 'exec_file'
    if(curr_p->exec_file !=NULL){
        file_close (curr_p->exec_file);
        curr_p->exec_file = NULL;
    }
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

void
set_exitstatus(int status){
  struct process *curr_p;
  curr_p = find_process(thread_current()->tid);
  curr_p -> exit_status = status;
}

int
get_exitstatus(tid_t child_tid){
  struct process *p;
  p = find_process(child_tid);
  return p->exit_status;
}





/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in ////printf(). */
#define PE32Wx PRIx32   /* //print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* //print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* //print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* //print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  //old_level = intr_disable();
  file = filesys_open (file_name);   //critical section

  //intr_set_level(old_level);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);

  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:
        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.
        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.
   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Do calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success){
        *esp = PHYS_BASE;
      }
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


struct list_elem *
find_processelem(tid_t pid){
  struct list_elem *e;
  struct process *p;
  for(e = list_begin(&process_list); e!= list_end(&process_list); e = list_next(e)){
    p = list_entry(e, struct process, elem);
    if (p->pid == pid){
      break;
    }
  }
  return e;
}

struct process *
find_process(tid_t pid){
  struct process *p;
  struct list_elem *e;
  if (list_empty(&process_list))
    return NULL;
  e = find_processelem(pid);

  if (e == list_end(&process_list)){    //there's no pid process in process list
    return NULL;
  }
  else{
    p = list_entry(e, struct process, elem);
  return p;
  } 
}

struct list_elem *
find_fileelem(int fd){
  struct process * p;
  p = find_process(thread_current()->tid);
  struct fd_file *fd_file;
  struct list_elem *e;
  for(e = list_begin(&p->file_list); e!= list_end(&p->file_list); e = list_next(e)){
    fd_file = list_entry(e, struct fd_file, elem);
    if (fd_file->fd == fd){
      break;
    }
  }
  return e;
}

struct fd_file *
find_file(int fd){
  struct fd_file *fd_file;
  struct list_elem *e;
  if (list_empty(&find_process(thread_current()->tid)->file_list))
    return NULL;
  e = find_fileelem(fd);
  if ( e == list_end(&find_process(thread_current()->tid)->file_list))
    return NULL;    
  else
    fd_file = list_entry(e, struct fd_file, elem);
  return fd_file;
}

bool
is_valid_usraddr (void *addr){
  struct thread *t = thread_current();
  void *temp = addr;
  if (is_kernel_vaddr(addr)){
      return false;
  }
  if (addr == NULL){
    return false;
  }
  if (pagedir_get_page (t->pagedir, addr) == NULL){
    return false;
  }
  else 
    return true;
  
}

