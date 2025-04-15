extern int process_count;
extern struct list_head ready_queue;
extern struct pcb_t *current_process[NCPU];
extern volatile unsigned int global_lock;

extern cpu_t current_process_start_time[NCPU];
 
void scheduler()
{
    ACQUIRE_LOCK(&global_lock);
    if (emptyProcQ(&ready_queue)) {
        if (process_count == 0) { // TODO: non entra mai qui perche' il process_count non va mai a 0 anche dopo la fine del test
            RELEASE_LOCK(&global_lock);
            HALT();
        } else {
            // enable interrupts and disable PLT
            setMIE(MIE_ALL & ~MIE_MTIE_MASK);
            unsigned int status = getSTATUS();
            status |= MSTATUS_MIE_MASK;
            *((memaddr *)TPR) = 1;
            RELEASE_LOCK(&global_lock);
            setSTATUS(status);
            WAIT();
        }
    } else {
        int prid = getPRID();
        pcb_t *pcb = removeProcQ(&ready_queue);    
        current_process[prid] = pcb;

        *((memaddr *)TPR) = 0;

        cpu_t current_time;
        STCK(current_time);
        current_process_start_time[prid] = current_time;

        RELEASE_LOCK(&global_lock);
        setTIMER(TIMESLICE);
        LDST(&(pcb->p_s));
    }
}
