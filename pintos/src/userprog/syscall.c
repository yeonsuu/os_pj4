#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/malloc.h"
#include "threads/init.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *sp = f->esp;
  uint32_t *argv[3];

  if(!is_valid_usraddr(sp)){
  	sys_exit(-1);
  }

  switch (*sp) {
    case SYS_HALT :
    	sys_halt();
    	break;

    case SYS_EXIT :
      syscall_arguments(argv, sp, 1);
    	sys_exit((int)*argv[0]);
    break;

    case SYS_EXEC :   
      syscall_arguments(argv, sp, 1);          
    	f -> eax = sys_exec((char *)*(uint32_t *)argv[0]);
    break;

    case SYS_WAIT :
      syscall_arguments(argv, sp, 1);
    	f -> eax = sys_wait((tid_t)*argv[0]);
    	break;        

    case SYS_CREATE :
      syscall_arguments(argv, sp, 2);               
    	f -> eax = sys_create((char *)*(uint32_t *)argv[0], (unsigned)*argv[1]);
    	break;

    case SYS_REMOVE :   
      syscall_arguments(argv, sp, 1);     
    	f -> eax = sys_remove((char *)*(uint32_t *)argv[0]);
    	break;

    case SYS_OPEN :  
      syscall_arguments(argv, sp, 1);
    	f->eax = sys_open((char *)*(uint32_t *)argv[0]);
    	break;

    case SYS_FILESIZE :
      syscall_arguments(argv, sp, 1);
    	f->eax = sys_filesize((int)*argv[0]);
    	break;

    case SYS_READ :   
      syscall_arguments(argv, sp, 3);
    	f->eax = (off_t) sys_read((int)*argv[0], (void *)*(uint32_t *)argv[1], (unsigned) * argv[2]);
    	break;

    case SYS_WRITE : 
      syscall_arguments(argv, sp, 3);
    	f->eax = sys_write((int)*argv[0], (void *)*(uint32_t *)argv[1], (unsigned) *argv[2]);
    	break;

    case SYS_SEEK :
      syscall_arguments(argv, sp, 2);
      sys_seek((int)*argv[0], (unsigned)*argv[1]);
    	break;

    case SYS_TELL :  
      syscall_arguments(argv, sp, 1);
    	f->eax = sys_tell((int)*argv[0]);
    	break;

    case SYS_CLOSE : 
      syscall_arguments(argv, sp, 1);
    	sys_close((int)*argv[0]);
    	break;
  }
}


void
syscall_arguments(uint32_t **argv, uint32_t *sp, int argc) {
  int i;

  for (i = 0; i < argc; i++){
    sp++;
    if(!is_valid_usraddr((void *)sp))
      sys_exit(-1);  
    argv[i] = sp;
  }
}

void
sys_halt(void)
{
  power_off();
}

void
sys_exit(int status)
{
	set_exitstatus(status);
  thread_exit();
}

int
sys_exec(const char *cmd_line)
{
  if(!is_valid_usraddr((void *)cmd_line))
    return -1;
  else
    return process_execute(cmd_line);
}

int
sys_wait(tid_t pid)
{
  return process_wait(pid);
}

bool
sys_create(const char *file, unsigned initial_size)
{
  if(file == NULL)
    sys_exit(-1);
  if(!is_valid_usraddr((void *)file))
    sys_exit(-1);  
  return filesys_create(file, initial_size);
}

bool
sys_remove(const char *file)
{
  if(file == NULL)
    sys_exit(-1);
  if(!is_valid_usraddr((void *)file))
    sys_exit(-1);
  return filesys_remove(file);  
}
int
sys_open(const char *file)
{
  if(file == NULL)
    sys_exit(-1);
  if(!is_valid_usraddr((void *)file))
    sys_exit(-1);
    
  int fd;
  struct file * f;
  struct process * p;
  p = find_process(thread_current()->tid);

  f = filesys_open (file);

  //ERROR: file is NULL
  if(f == NULL){
    fd = -1;
  }
  //ADD file&fd to current process's file_list
  else{
    fd = p->fd_cnt;
    p->fd_cnt++;

    struct fd_file *fd_and_file;
    fd_and_file = malloc (sizeof *fd_and_file); 

    fd_and_file->fd = fd;
    fd_and_file->file = f;
    list_push_back(&p->file_list, &fd_and_file->elem);
  }
  return fd;
}

int
sys_filesize(int fd)
{
  struct file *f = find_file(fd)->file;
  return file_length (f);
}

int
sys_read(int fd, const void *buffer, unsigned size)
{
  struct file *f;
  int result;

  if((void *)buffer == NULL){
    sys_exit(-1);
  }
  //CASE 0: buffer's address is NOT available
  if(!is_valid_usraddr((void *)buffer) || !is_user_vaddr (buffer + size)){
    sys_exit(-1);
  }
  //CASE 0: fd == 1 is write to command
  if (fd == 1){
    sys_exit(-1);
  }

  //Filesys synchronization
  lock_acquire(&filesys_lock);

  //CASE 1: READ from command
  if(fd == 0){
    int i;
    for (i = 0; i !=(int)size; i++){
      *(uint8_t *)buffer = input_getc();
      buffer++;
    }
    lock_release(&filesys_lock);
    return size;
  }
  //CASE 2: READ from file
  else{
    //ERROR: NO FILE!
    if (find_file(fd) == NULL){
      lock_release(&filesys_lock);
      sys_exit(-1);
    }
    f = find_file(fd)->file;
    result = file_read(f, buffer, (off_t) size);
    lock_release(&filesys_lock);
  }
  return result;
}

int
sys_write(int fd, const void *buffer, unsigned size)
{
  struct file *f;

  //CASE 0: buffer's address is NOT available
  if(!is_valid_usraddr((void *)buffer) || !is_user_vaddr (buffer + size)){
    sys_exit(-1);
  }
  //CASE 0: fd == 0 is read to command
  if (fd == 0){
    sys_exit(-1);
  }

  lock_acquire(&filesys_lock);
  int result;

  //CASE 1: WRITE to command
  if(fd == 1){
    putbuf (buffer, size);
    result = size;
  }
  //CASE 2: WRITE to file
  else{
    if (find_file(fd) == NULL){
      lock_release(&filesys_lock);
      sys_exit(-1);
    }
    
    f = find_file(fd)->file;
    result = file_write(f, buffer, (off_t) size);
  }
  lock_release(&filesys_lock);

  return result;
}
void
sys_seek(int fd, unsigned position)
{
  struct file *f;
  //ERROR: NOT FILE(COMMAND)
  if (fd == 0)
    ASSERT(0);
  if(fd == 1)
    ASSERT(0);
  //ERROR: CANNOT find file
  if (find_file(fd) == NULL)
    sys_exit(-1);

  f = find_file(fd)->file;  
  file_seek(f, position);
  return;
}

unsigned
sys_tell(int fd)
{
  struct file *f;
  //ERROR: NOT FILE(COMMAND)
  if (fd == 0)
    ASSERT(0);
  if(fd == 1)
    ASSERT(0);
  //ERROR: CANNOT find file
  if (find_file(fd) == NULL)
    sys_exit(-1);
  f = find_file(fd)->file;

  return file_tell(f);
}

void
sys_close(int fd)
{
  //ERROR: NOT FILE(COMMAND)
  if (fd == 0)
    sys_exit(-1);
  if (fd == 1)
    sys_exit(-1);
  //ERROR: CANNOT find file
  if (find_file(fd) == NULL){
    sys_exit(-1);
  }
  struct file *f = find_file(fd)->file;
  
  //ERROR: FILE is NULL
  if (f == NULL){
    list_remove(&find_file(fd)->elem);
    free(find_file(fd));
    sys_exit(-1);
  }
  else{
    file_close (f);
    list_remove(&find_file(fd)->elem);
    free(find_file(fd));
  }
}

