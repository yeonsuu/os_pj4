
#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <list.h>
#include "threads/synch.h"

struct fd_file
{
	int fd;
	struct file* file;
	struct list_elem elem;
};

/* process의 childeren_pids에 저장해 주기 위한 struct 
이 역시 load success한 child만 저장 */
struct childpid_elem
{
	tid_t childpid;
	struct list_elem elem;
};

/* load success 한 process를 process_list에 넣어주기 위한 struct */
struct process
{
	tid_t pid;
	tid_t parent_pid;
	struct list children_pids;

	struct semaphore sema_pexec;	/* 이 process가 exec을 call 했으면 child가 load 완료할때 까지 기다림 */
	struct semaphore sema_pwait;	/* 이 process가 wait을 call 했으면 child가 exit 할때까지 sema down */
	
	bool load_success;				/* 이 process가 load success 했는지 process_execute()에서 sema_up 후 childr가 load success 했는지를 받아오기 위해*/
	bool is_dead;					/* 이 process가 exit 했는지 */
	int exit_status;				/* exit 했다면 exit_status 가 뭐였는지 */
		
	/* Process's file list */
	int fd_cnt;
	struct file * exec_file;		/* 이 process의 exec file를 process가 exit 할 때 free 해주기 위해서 */
	struct list file_list;			/* 이 process가 open 한 file_list */

	struct list_elem elem;
};

void process_init (void);
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void set_exitstatus(int);
int get_exitstatus(tid_t);
struct process * find_process(tid_t);
struct list_elem * find_processelem(tid_t);
struct list_elem * find_fileelem(int);
struct fd_file * find_file(int);
bool is_valid_usraddr (void *);
#endif /* userprog/process.h */
