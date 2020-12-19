
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "kernel_cc.h"

void start_thread()
{
  int exitval;

  TCB* curthread = cur_thread();

  Task call =  curthread->ptcb->task;
  int argl = curthread->ptcb->argl;
  void* args = curthread->ptcb->args;

  exitval = call(argl,args);
  ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  if(task == NULL)
    return NOTHREAD;

  PCB* curproc = CURPROC;
  curproc->thread_count++;
  
  PTCB* ptcb = (PTCB *)xmalloc(sizeof(PTCB));

  /* Initialize the thread's ptcb */
  ptcb->task = task;
  ptcb->argl = argl;
  ptcb->args = args;
  ptcb->refcount = 0;
  ptcb->exited = 0;
  ptcb->detached = 0;
  ptcb->exit_cv = COND_INIT;

  rlnode_init(&ptcb->ptcb_list_node, ptcb);
  rlist_push_back(&curproc->ptcb_list, &ptcb->ptcb_list_node);
  
  TCB* tcb = spawn_thread(curproc, start_thread);
  ptcb->tcb = tcb;
  tcb->ptcb = ptcb;
  wakeup(tcb);
  
  return (Tid_t)ptcb;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
  return (Tid_t)cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  PTCB* ptcb_to_join = (PTCB*)tid;
  
  //check if this is a valid tid
  rlnode* node = rlist_find(&CURPROC->ptcb_list, ptcb_to_join, NULL);
  
  if(node == NULL)
    return -1;

  //can't join a detached thread
  if(ptcb_to_join->detached)
    return -1;

  //can't join ourself
  if(ptcb_to_join->tcb == cur_thread())
    return -1;

  ptcb_to_join->refcount++;
  
  while(!ptcb_to_join->exited && !ptcb_to_join->detached){
    kernel_wait(&ptcb_to_join->exit_cv, SCHED_USER);
  }

  ptcb_to_join->refcount--;
  /*if we woke up but the reason is that
  the thread got detached, return error*/
  if(ptcb_to_join->detached)
    return -1;

  if(exitval)
    *exitval = ptcb_to_join->exitval;

  if(ptcb_to_join->refcount == 0){
    rlist_remove(&ptcb_to_join->ptcb_list_node);
    free(ptcb_to_join);
  }
  
  return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB* ptcb_to_detach = (PTCB*)tid;
  
  /* Check if this is a valid tid */
  rlnode* node = rlist_find(&CURPROC->ptcb_list, ptcb_to_detach, NULL);
  if(node == NULL)
    return -1;
  
  /* Can't detach an exited thread */
  if(ptcb_to_detach->exited)
    return -1;

  ptcb_to_detach->detached = 1;
  kernel_broadcast(&ptcb_to_detach->exit_cv);
  
  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PTCB* ptcb_to_exit = cur_thread()->ptcb;
  ptcb_to_exit->exitval = exitval;
  ptcb_to_exit->exited = 1;    
  
  /* Wake up every thread waiting for me to exit */
  kernel_broadcast(&ptcb_to_exit->exit_cv);
  CURPROC->thread_count--;

  /* Reparent any children of the exiting process to the 
  initial task */
  if(CURPROC->thread_count == 0){
      PCB* curproc = CURPROC;
      
      if(get_pid(curproc) != 1){
        PCB* initpcb = get_pcb(1);
        while(!is_rlist_empty(& curproc->children_list)) {
          rlnode* child = rlist_pop_front(& curproc->children_list);
          child->pcb->parent = initpcb;
          rlist_push_front(& initpcb->children_list, child);
        }

        /* Add exited children to the initial task's exited list 
        and signal the initial task */
        if(!is_rlist_empty(& curproc->exited_list)) {
          rlist_append(& initpcb->exited_list, &curproc->exited_list);
          kernel_broadcast(& initpcb->child_exit);
        }

        /* Put me into my parent's exited list */
        rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
        kernel_broadcast(& curproc->parent->child_exit);
      }
      
      assert(is_rlist_empty(& curproc->children_list));
      assert(is_rlist_empty(& curproc->exited_list));

      /* 
      Do all the other cleanup we want here, close files etc. 
      */

      /* Release the args data */
      if(curproc->args) {
        free(curproc->args);
        curproc->args = NULL;
      }

      /* Clean up FIDT */
      for(int i=0;i<MAX_FILEID;i++) {
          if(curproc->FIDT[i] != NULL) {
          FCB_decref(curproc->FIDT[i]);
          curproc->FIDT[i] = NULL;
          }
      }
      
      /* Clean up non freed PTCB's */
      PTCB* node;
      while(!is_rlist_empty(&curproc->ptcb_list)){
        node = rlist_pop_front(&curproc->ptcb_list)->ptcb;
        free(node);
      }

      /* Disconnect my main_thread */
      curproc->main_thread = NULL;

      /* Now, mark the process as exited. */
      curproc->pstate = ZOMBIE;
  }
    
  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}
