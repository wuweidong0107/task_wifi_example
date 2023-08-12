#ifndef _THPOOL_
#define _THPOOL_

#ifdef __cplusplus
extern "C" {
#endif

/* =================================== API ======================================= */


typedef struct thpool_ thpool_t;
typedef struct task_queue task_queue_t;

thpool_t* thpool_init(int num_threads);
void thpool_wait(thpool_t*);
void thpool_pause(thpool_t*);
void thpool_resume(thpool_t*);
void thpool_destroy(thpool_t*);
int thpool_num_threads_working(thpool_t*);
task_queue_t* thpool_taskqueue(thpool_t *thpool_p);

/* Task */
typedef struct task task_t;
typedef void (*task_handler_t)(task_t* task);
struct task
{
    struct task *prev;           /* pointer to previous task   */
 
    task_handler_t handler;
    task_handler_t result_cb;
    task_handler_t cleanup_cb;

    void *user_data;

};

task_t* task_init(void);
int task_queue_push(task_queue_t *task_queue_p, task_t* newtask);


#ifdef __cplusplus
}
#endif

#endif
