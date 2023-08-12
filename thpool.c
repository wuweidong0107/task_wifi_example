#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#else
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include "thpool.h"

#ifdef THPOOL_DEBUG
#define THPOOL_DEBUG 1
#else
#define THPOOL_DEBUG 0
#endif

#if !defined(DISABLE_PRINT) || defined(THPOOL_DEBUG)
#define err(str) fprintf(stderr, str)
#else
#define err(str)
#endif

static volatile int threads_keepalive;
static volatile int threads_on_hold;

/* ========================== STRUCTURES ============================ */

/* Binary semaphore */
typedef struct bsem {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int v;
} bsem;

/* Task queue */
struct task_queue {
    pthread_mutex_t rwmutex; /* used for queue r/w access */
    task_t *front;           /* pointer to front of queue */
    task_t *rear;            /* pointer to rear  of queue */
    bsem *has_tasks;         /* flag as binary semaphore  */
    int len;                 /* number of tasks in queue   */
};

/* Thread */
typedef struct thread {
    int id;                   /* friendly id               */
    pthread_t pthread;        /* pointer to actual thread  */
    struct thpool_ *thpool_p; /* access to thpool          */
} thread_t;

/* Threadpool */
struct thpool_ {
    thread_t **threads;               /* pointer to threads        */
    volatile int num_threads_alive;   /* threads currently alive   */
    volatile int num_threads_working; /* threads currently working */
    pthread_mutex_t thcount_lock;     /* used for thread count etc */
    pthread_cond_t threads_all_idle;  /* signal to thpool_wait     */
    task_queue_t task_queue;          /* task queue                 */
};

/* ========================== PROTOTYPES ============================ */

static int thread_init(thpool_t *thpool_p, struct thread **thread_p, int id);
static void *thread_do(struct thread *thread_p);
static void thread_hold(int sig_id);
static void thread_destroy(struct thread *thread_p);

static int task_queue_init(task_queue_t *task_queue_p);
static void task_queue_clear(task_queue_t *task_queue_p);
static struct task *task_queue_pull(task_queue_t *task_queue_p);
static void task_queue_destroy(task_queue_t *task_queue_p);

static void bsem_init(struct bsem *bsem_p, int value);
static void bsem_reset(struct bsem *bsem_p);
static void bsem_post(struct bsem *bsem_p);
static void bsem_post_all(struct bsem *bsem_p);
static void bsem_wait(struct bsem *bsem_p);

/* ========================== THREADPOOL ============================ */

/* Initialise thread pool */
struct thpool_ *thpool_init(int num_threads) {

    threads_on_hold = 0;
    threads_keepalive = 1;

    if (num_threads < 0) {
        num_threads = 0;
    }

    /* Make new thread pool */
    thpool_t *thpool_p;
    thpool_p = (struct thpool_ *)malloc(sizeof(struct thpool_));
    if (thpool_p == NULL) {
        err("thpool_init(): Could not allocate memory for thread pool\n");
        return NULL;
    }
    thpool_p->num_threads_alive = 0;
    thpool_p->num_threads_working = 0;

    /* Initialise the task queue */
    if (task_queue_init(&thpool_p->task_queue) == -1) {
        err("thpool_init(): Could not allocate memory for task queue\n");
        free(thpool_p);
        return NULL;
    }

    /* Make threads in pool */
    thpool_p->threads = (struct thread **)malloc(num_threads * sizeof(struct thread *));
    if (thpool_p->threads == NULL) {
        err("thpool_init(): Could not allocate memory for threads\n");
        task_queue_destroy(&thpool_p->task_queue);
        free(thpool_p);
        return NULL;
    }

    pthread_mutex_init(&(thpool_p->thcount_lock), NULL);
    pthread_cond_init(&thpool_p->threads_all_idle, NULL);

    /* Thread init */
    int n;
    for (n = 0; n < num_threads; n++) {
        thread_init(thpool_p, &thpool_p->threads[n], n);
#if THPOOL_DEBUG
        printf("THPOOL_DEBUG: Created thread %d in pool \n", n);
#endif
    }

    /* Wait for threads to initialize */
    while (thpool_p->num_threads_alive != num_threads) {
    }

    return thpool_p;
}

/* Wait until all tasks have finished */
void thpool_wait(thpool_t *thpool_p) {
    pthread_mutex_lock(&thpool_p->thcount_lock);
    while (thpool_p->task_queue.len || thpool_p->num_threads_working) {
        pthread_cond_wait(&thpool_p->threads_all_idle, &thpool_p->thcount_lock);
    }
    pthread_mutex_unlock(&thpool_p->thcount_lock);
}

/* Destroy the threadpool */
void thpool_destroy(thpool_t *thpool_p) {
    /* No need to destroy if it's NULL */
    if (thpool_p == NULL)
        return;

    volatile int threads_total = thpool_p->num_threads_alive;

    /* End each thread 's infinite loop */
    threads_keepalive = 0;

    /* Give one second to kill idle threads */
    double TIMEOUT = 1.0;
    time_t start, end;
    double tpassed = 0.0;
    time(&start);
    while (tpassed < TIMEOUT && thpool_p->num_threads_alive) {
        bsem_post_all(thpool_p->task_queue.has_tasks);
        time(&end);
        tpassed = difftime(end, start);
    }

    /* Poll remaining threads */
    while (thpool_p->num_threads_alive) {
        bsem_post_all(thpool_p->task_queue.has_tasks);
        sleep(1);
    }

    /* Task queue cleanup */
    task_queue_destroy(&thpool_p->task_queue);
    /* Deallocs */
    int n;
    for (n = 0; n < threads_total; n++) {
        thread_destroy(thpool_p->threads[n]);
    }
    free(thpool_p->threads);
    free(thpool_p);
}

/* Pause all threads in threadpool */
void thpool_pause(thpool_t *thpool_p) 
{
    int n;
    for (n = 0; n < thpool_p->num_threads_alive; n++) {
        pthread_kill(thpool_p->threads[n]->pthread, SIGUSR1);
    }
}

/* Resume all threads in threadpool */
void thpool_resume(thpool_t *thpool_p) 
{
    // resuming a single threadpool hasn't been
    // implemented yet, meanwhile this suppresses
    // the warnings
    (void)thpool_p;

    threads_on_hold = 0;
}

int thpool_num_threads_working(thpool_t *thpool_p) 
{
    return thpool_p->num_threads_working;
}

task_queue_t *thpool_taskqueue(thpool_t *thpool_p) 
{
    return &thpool_p->task_queue;
}

/* ============================ THREAD ============================== */

/* Initialize a thread in the thread pool
 *
 * @param thread        address to the pointer of the thread to be created
 * @param id            id to be given to the thread
 * @return 0 on success, -1 otherwise.
 */
static int thread_init(thpool_t *thpool_p, struct thread **thread_p, int id) 
{

    *thread_p = (struct thread *)malloc(sizeof(struct thread));
    if (*thread_p == NULL) {
        err("thread_init(): Could not allocate memory for thread\n");
        return -1;
    }

    (*thread_p)->thpool_p = thpool_p;
    (*thread_p)->id = id;

    pthread_create(&(*thread_p)->pthread, NULL, (void *(*)(void *))thread_do, (*thread_p));
    pthread_detach((*thread_p)->pthread);
    return 0;
}

/* Sets the calling thread on hold */
static void thread_hold(int sig_id) 
{
    (void)sig_id;
    threads_on_hold = 1;
    while (threads_on_hold) {
        sleep(1);
    }
}

/* What each thread is doing
 *
 * In principle this is an endless loop. The only time this loop gets interuppted is once
 * thpool_destroy() is invoked or the program exits.
 *
 * @param  thread        thread that will run this function
 * @return nothing
 */
static void *thread_do(struct thread *thread_p) 
{

    /* Set thread name for profiling and debugging */
    char thread_name[16] = {0};
    snprintf(thread_name, 16, "thpool-%d", thread_p->id);

#if defined(__linux__)
    /* Use prctl instead to prevent using _GNU_SOURCE flag and implicit declaration */
    prctl(PR_SET_NAME, thread_name);
#elif defined(__APPLE__) && defined(__MACH__)
    pthread_setname_np(thread_name);
#else
    err("thread_do(): pthread_setname_np is not supported on this system");
#endif

    /* Assure all threads have been created before starting serving */
    thpool_t *thpool_p = thread_p->thpool_p;

    /* Register signal handler */
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = thread_hold;
    if (sigaction(SIGUSR1, &act, NULL) == -1) {
        err("thread_do(): cannot handle SIGUSR1");
    }

    /* Mark thread as alive (initialized) */
    pthread_mutex_lock(&thpool_p->thcount_lock);
    thpool_p->num_threads_alive += 1;
    pthread_mutex_unlock(&thpool_p->thcount_lock);

    while (threads_keepalive) {

        bsem_wait(thpool_p->task_queue.has_tasks);

        if (threads_keepalive) {

            pthread_mutex_lock(&thpool_p->thcount_lock);
            thpool_p->num_threads_working++;
            pthread_mutex_unlock(&thpool_p->thcount_lock);

            /* Read task from queue and execute it */
            task_t *task_p = task_queue_pull(&thpool_p->task_queue);
            if (task_p) {
                if (task_p->handler) {
                    task_p->handler(task_p);
                }
                free(task_p);
            }

            pthread_mutex_lock(&thpool_p->thcount_lock);
            thpool_p->num_threads_working--;
            if (!thpool_p->num_threads_working) {
                pthread_cond_signal(&thpool_p->threads_all_idle);
            }
            pthread_mutex_unlock(&thpool_p->thcount_lock);
        }
    }
    pthread_mutex_lock(&thpool_p->thcount_lock);
    thpool_p->num_threads_alive--;
    pthread_mutex_unlock(&thpool_p->thcount_lock);

    return NULL;
}

/* Frees a thread  */
static void thread_destroy(thread_t *thread_p) {
    free(thread_p);
}

/* ============================ JOB QUEUE =========================== */

/* Initialize queue */
static int task_queue_init(task_queue_t *task_queue_p) 
{
    task_queue_p->len = 0;
    task_queue_p->front = NULL;
    task_queue_p->rear = NULL;

    task_queue_p->has_tasks = (struct bsem *)malloc(sizeof(struct bsem));
    if (task_queue_p->has_tasks == NULL) {
        return -1;
    }

    pthread_mutex_init(&(task_queue_p->rwmutex), NULL);
    bsem_init(task_queue_p->has_tasks, 0);

    return 0;
}

/* Clear the queue */
static void task_queue_clear(task_queue_t *task_queue_p) 
{

    while (task_queue_p->len) {
        free(task_queue_pull(task_queue_p));
    }

    task_queue_p->front = NULL;
    task_queue_p->rear = NULL;
    bsem_reset(task_queue_p->has_tasks);
    task_queue_p->len = 0;
}

/* Add (allocated) task to queue
 */
int task_queue_push(task_queue_t *task_queue_p, task_t *newtask) 
{

    if (newtask->handler == NULL)
        return -1;

    pthread_mutex_lock(&task_queue_p->rwmutex);
    newtask->prev = NULL;

    switch (task_queue_p->len) {

    case 0: /* if no tasks in queue */
        task_queue_p->front = newtask;
        task_queue_p->rear = newtask;
        break;

    default: /* if tasks in queue */
        task_queue_p->rear->prev = newtask;
        task_queue_p->rear = newtask;
    }
    task_queue_p->len++;

    bsem_post(task_queue_p->has_tasks);
    pthread_mutex_unlock(&task_queue_p->rwmutex);

    return 0;
}

/* Get first task from queue(removes it from queue)
 * Notice: Caller MUST hold a mutex
 */
static struct task *task_queue_pull(task_queue_t *task_queue_p) 
{

    pthread_mutex_lock(&task_queue_p->rwmutex);
    task_t *task_p = task_queue_p->front;

    switch (task_queue_p->len) {

    case 0: /* if no tasks in queue */
        break;

    case 1: /* if one task in queue */
        task_queue_p->front = NULL;
        task_queue_p->rear = NULL;
        task_queue_p->len = 0;
        break;

    default: /* if >1 tasks in queue */
        task_queue_p->front = task_p->prev;
        task_queue_p->len--;
        /* more than one task in queue -> post it */
        bsem_post(task_queue_p->has_tasks);
    }

    pthread_mutex_unlock(&task_queue_p->rwmutex);
    return task_p;
}

/* Free all queue resources back to the system */
static void task_queue_destroy(task_queue_t *task_queue_p) 
{
    task_queue_clear(task_queue_p);
    free(task_queue_p->has_tasks);
}

/* ======================== SYNCHRONISATION ========================= */

/* Init semaphore to 1 or 0 */
static void bsem_init(bsem *bsem_p, int value) 
{
    if (value < 0 || value > 1) {
        err("bsem_init(): Binary semaphore can take only values 1 or 0");
        exit(1);
    }
    pthread_mutex_init(&(bsem_p->mutex), NULL);
    pthread_cond_init(&(bsem_p->cond), NULL);
    bsem_p->v = value;
}

/* Reset semaphore to 0 */
static void bsem_reset(bsem *bsem_p) 
{
    bsem_init(bsem_p, 0);
}

/* Post to at least one thread */
static void bsem_post(bsem *bsem_p) 
{
    pthread_mutex_lock(&bsem_p->mutex);
    bsem_p->v = 1;
    pthread_cond_signal(&bsem_p->cond);
    pthread_mutex_unlock(&bsem_p->mutex);
}

/* Post to all threads */
static void bsem_post_all(bsem *bsem_p) 
{
    pthread_mutex_lock(&bsem_p->mutex);
    bsem_p->v = 1;
    pthread_cond_broadcast(&bsem_p->cond);
    pthread_mutex_unlock(&bsem_p->mutex);
}

/* Wait on semaphore until semaphore has value 0 */
static void bsem_wait(bsem *bsem_p) 
{
    pthread_mutex_lock(&bsem_p->mutex);
    while (bsem_p->v != 1) {
        pthread_cond_wait(&bsem_p->cond, &bsem_p->mutex);
    }
    bsem_p->v = 0;
    pthread_mutex_unlock(&bsem_p->mutex);
}

/* ======================== JOB ========================= */

task_t *task_init(void) 
{
    task_t *newtask;

    newtask = (struct task *)malloc(sizeof(struct task));
    if (newtask == NULL) {
        err("task_init(): Could not allocate memory for new task\n");
        return NULL;
    }

    newtask->handler = NULL;
    newtask->result_cb = NULL;
    newtask->cleanup_cb = NULL;

    return newtask;
}