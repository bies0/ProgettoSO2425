#include "globals.h" // da' un warning, non siamo sicuri. Se lo togliamo dobbiamo dichiarare le variabili.
 
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
        RELEASE_LOCK(&global_lock);
        setTIMER(TIMESLICE);
        LDST(&(pcb->p_s));
    }
}
