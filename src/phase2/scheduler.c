extern int process_count;
extern struct list_head ready_queue;
extern struct pcb_t *current_process[NCPU];
extern volatile unsigned int global_lock;
extern int lock_acquired_0;

extern cpu_t current_process_start_time[NCPU];
 
void scheduler()
{
    if (!lock_acquired_0 || getPRID() != 0)
        ACQUIRE_LOCK(&global_lock);

    if (emptyProcQ(&ready_queue)) {
        if (process_count == 0) {
            RELEASE_LOCK(&global_lock);
            HALT();
        } else {
            // enable interrupts and disable PLT
            setMIE(MIE_ALL & ~MIE_MTIE_MASK);
            unsigned int status = getSTATUS();
            status |= MSTATUS_MIE_MASK;
            setSTATUS(status);

            *((memaddr *)TPR) = 1;
            RELEASE_LOCK(&global_lock);
            WAIT();
        }
    } else {
        int prid = getPRID();
        pcb_t *pcb = removeProcQ(&ready_queue);    
        current_process[prid] = pcb;
        if (lock_acquired_0 && prid == 0)
            lock_acquired_0 = 0;

        cpu_t current_time;
        STCK(current_time);
        current_process_start_time[prid] = current_time;

        RELEASE_LOCK(&global_lock);
        setTIMER(TIMESLICE);
        LDST(&(pcb->p_s));
    }
}
