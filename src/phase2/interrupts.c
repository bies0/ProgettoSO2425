extern volatile unsigned int global_lock;
extern int process_count;
extern struct list_head ready_queue;
extern int device_semaphores[];
extern struct pcb_t *current_process[NCPU];
extern const unsigned int PSEUDO_CLOCK_INDEX;

extern void scheduler();
extern void print(char *msg); // presa dal test

int getDevNo(int IntlineNo)
{
    unsigned int word = 0x10000040 + (IntlineNo - 3)*0x04;
    // TODO: le specifiche dicono di usare uno switch, ma e' impossibbileh
    if      (word & DEV0ON) return 0;
    else if (word & DEV1ON) return 1;
    else if (word & DEV2ON) return 2;
    else if (word & DEV3ON) return 3;
    else if (word & DEV4ON) return 4;
    else if (word & DEV5ON) return 5;
    else if (word & DEV6ON) return 6;
    else if (word & DEV7ON) return 7;
    else { PANIC();         return -1; } // just to remove the warning 'control reaches the end of non-void function'
}

void interruptHandler(int exccode)
{
    print("interrupt handler\n");

    int IntlineNo, DevNo;

    switch (exccode)
    {
        case IL_CPUTIMER: IntlineNo = 1; break;
        case IL_TIMER:    IntlineNo = 2; break;
        case IL_DISK:     IntlineNo = 3; break;
        case IL_FLASH:    IntlineNo = 4; break;
        case IL_ETHERNET: IntlineNo = 5; break;
        case IL_PRINTER:  IntlineNo = 6; break;
        case IL_TERMINAL: IntlineNo = 7; break;
        default: PANIC();
    }

    // Our device_semaphores:
    // 12345678 1------- 12345678 12345678 12345678 12345678 12345678
    // 1        2        3        4        5        6        7

    int prid = getPRID();
    if (exccode != IL_CPUTIMER && exccode != IL_TIMER) {
        DevNo = getDevNo(IntlineNo);
        memaddr devAddrBase = START_DEVREG + ((IntlineNo - 3) * 0x80) + (DevNo * 0x10);
        // si puo' anche fare cosi'
        //devregarea_t devregarea = *(devregarea_t *)START_DEVREG;
        //memaddr = devAddrBase = devregarea.devreg[IntlineNo-3][DevNo];
        devreg_t devreg = *(devreg_t *)devAddrBase;
        unsigned int status_code = IntlineNo == 7 ? devreg.term.recv_status : devreg.dtp.status;
        if (IntlineNo == 7) devreg.term.recv_command = ACK;
        else                devreg.dtp.command = ACK;
        int *semaddr = &(device_semaphores[IntlineNo*8 + DevNo]); // get the right device semaphore
        pcb_t *pcb = removeBlocked(semaddr); // V on the semaphore
        pcb->p_s.reg_a0 = status_code;
        ACQUIRE_LOCK(&global_lock);
        insertProcQ(&ready_queue, pcb);
        int cpu_has_process = current_process[prid] != NULL;
        RELEASE_LOCK(&global_lock);
        if (cpu_has_process) LDST(GET_EXCEPTION_STATE_PTR(prid));
        else scheduler();
    } else if (exccode == IL_CPUTIMER) {
        setTIMER(TIMESLICE); 
        ACQUIRE_LOCK(&global_lock);
        pcb_t *current_pcb = current_process[prid];
        current_pcb->p_s = *GET_EXCEPTION_STATE_PTR(prid);
        insertProcQ(&ready_queue, current_pcb);
        current_process[prid] = NULL;
        RELEASE_LOCK(&global_lock);
        scheduler();
    } else {
        LDIT(PSECOND);
        while (headBlocked(&(device_semaphores[PSEUDO_CLOCK_INDEX])) != NULL) {
            pcb_t *pcb = removeBlocked(&(device_semaphores[PSEUDO_CLOCK_INDEX]));
            insertProcQ(&ready_queue, pcb);
        }
        ACQUIRE_LOCK(&global_lock);
        int cpu_has_process = current_process[prid] != NULL;
        RELEASE_LOCK(&global_lock);
        if (cpu_has_process) LDST(GET_EXCEPTION_STATE_PTR(prid));
        else scheduler();
    }

}
