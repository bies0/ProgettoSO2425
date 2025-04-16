// non siamo sicuri di callSchedulerOnProcessor, anche se funziona tutto in ogni caso
void callSchedulerOnProcessor(int prid) {
    state_t start_state = {
        .status = MSTATUS_MPP_M,
        .pc_epc = (memaddr)scheduler,
        .gpr = {0},
        .entry_hi = 0,
        .cause = 0,
        .mie = 0
    };
    start_state.reg_sp = (0x20020000 + prid * PAGESIZE);
    INITCPU(prid, &start_state);
}

void removePcb(pcb_t* process) {
    if (process->p_semAdd != NULL) {
        outBlocked(process);
    }

    outProcQ(&ready_queue, process);

    int processor = findInCurrents(process);
    if (processor != -1) {
        current_process[processor] = NULL;
        if (processor != getPRID()) {
            callSchedulerOnProcessor(processor); // TODO: che si fa?
        }
    }
    process_count--;
    freePcb(process);
}
