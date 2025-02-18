#include "globals.h" // da un warning, non siamo sicuri. Se lo togliamo dobbiamo dichiarare le variabili.

 
void scheduler()
{
    klog_print("Scheduler begin | ");
    ACQUIRE_LOCK(&global_lock);
    klog_print("Lock acquired | ");
    if (emptyProcQ(&ready_queue)) {
        klog_print("emptyProcQ | ");
        if (process_count > 0) {
            klog_print("process_count == 0 | ");
            HALT();
        } else {
            klog_print("process_count > 0 | ");
            // enable interrupts and disable PLT
            setMIE(MIE_ALL & ~MIE_MTIE_MASK);
            unsigned int status = getSTATUS();
            status |= MSTATUS_MIE_MASK;
            setSTATUS(status);

            *((memaddr *)TPR) = 1;
            klog_print("Waiting... | ");
            WAIT();
            klog_print("After wait | ");
        }
    } else {
        klog_print("Ready Queue not empty | ");
        int current_CPU = 0; // TODO
        pcb_t *pcb = removeProcQ(&ready_queue);    
        if (pcb == NULL) {} // TODO
        klog_print("Got pcb from queue | ");
        current_process[current_CPU] = pcb;
        klog_print("Pre Load PLT | ");
        // TODO: Load 5 ms (TIMESLICE) onto PLT of the current_CPU
        klog_print("Pre LDST | ");
        LDST(&(pcb->p_s));
        klog_print("Pcb is now running | ");
    }
    klog_print("Before release | ");
    RELEASE_LOCK(&global_lock);
    klog_print("Release lock, end scheduler | ");
}
