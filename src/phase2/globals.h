#ifdef GLOBALS
    #define EXTERN
#else
    #define EXTERN extern
#endif

EXTERN void test();
EXTERN void scheduler();
EXTERN void exceptionHandler();
EXTERN void interruptHandler();

EXTERN int process_count;
EXTERN struct list_head ready_queue;
EXTERN struct pcb_t *current_process[NCPU];
EXTERN struct semd_t device_semaphores[NRSEMAPHORES];
EXTERN volatile unsigned int global_lock;
