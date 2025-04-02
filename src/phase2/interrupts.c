extern volatile unsigned int global_lock;
extern int process_count;
extern struct list_head ready_queue;
extern int device_semaphores[];
extern struct pcb_t *current_process[NCPU];
extern const unsigned int PSEUDO_CLOCK_INDEX;

extern void scheduler();
extern void print(char *msg); // presa dal test

// word   00001000
// DEV0ON 00001000
// &      00001000

int getDevNo(int IntlineNo)
{
    memaddr bitmap = *(memaddr *)(CDEV_BITMAP_BASE + (IntlineNo - 3) * WS);
    klog_print("bitmap: ");
    klog_print_dec(bitmap);
    klog_print(" ");

    // TODO: le specifiche dicono di usare uno switch, ma se ci sono piu' device accesi nello stesso momento, con lo switch va in default, mentre con l'if prende il device a piu' alta priorita' (0 -> 7)
    //switch (bitmap)
    //{
    //    case DEV0ON: return 0; 
    //    case DEV1ON: return 1; 
    //    case DEV2ON: return 2; 
    //    case DEV3ON: return 3; 
    //    case DEV4ON: return 4; 
    //    case DEV5ON: return 5; 
    //    case DEV6ON: return 6;
    //    case DEV7ON: return 7;
    //    default:     return -1; // just to remove the warning 'control reaches the end of non-void function', it should never go here
    //}
    if      (bitmap & DEV0ON) return 0;
    else if (bitmap & DEV1ON) return 1;
    else if (bitmap & DEV2ON) return 2;
    else if (bitmap & DEV3ON) return 3;
    else if (bitmap & DEV4ON) return 4;
    else if (bitmap & DEV5ON) return 5;
    else if (bitmap & DEV6ON) return 6;
    else if (bitmap & DEV7ON) return 7;
    else                      return -1; // just to remove the warning 'control reaches the end of non-void function', it should never go here
}

void interruptHandler(int exccode)
{
    klog_print("interrupt handler: ");
    klog_print("(exccode ");
    klog_print_dec(exccode);
    klog_print(") -> ");

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

    klog_print("line ");
    klog_print_dec(IntlineNo);
    klog_print(" | ");

    // Our device_semaphores:
    // -------- 1------- 12345678 12345678 12345678 12345678 12345678
    // 1        2        3        4        5        6        7

    int prid = getPRID();
    if (exccode != IL_CPUTIMER && exccode != IL_TIMER) {
        DevNo = getDevNo(IntlineNo);
        klog_print("device ");
        klog_print_dec(DevNo);
        klog_print(" | ");

        // TODO: modi alternativi, della macro non ci fidiamo
        //memaddr devAddrBase = START_DEVREG + ((IntlineNo - 3) * 0x80) + (DevNo * 0x10);
        //memaddr devAddrBase = DEV_REG_ADDR(IntlineNo, DevNo);

        //devreg_t devreg = *(devreg_t *)devAddrBase;

        devregarea_t devregarea = *(devregarea_t *)START_DEVREG;
        devreg_t devreg = devregarea.devreg[IntlineNo-3][DevNo];
        unsigned int status_code = IntlineNo == 7 ? devreg.term.recv_status : devreg.dtp.status;
        if (IntlineNo == 7) devreg.term.recv_command = ACK;
        else                devreg.dtp.command = ACK;
        int *semaddr = &(device_semaphores[IntlineNo*8 + DevNo]); // get the right device semaphore
        pcb_t *pcb = removeBlocked(semaddr); // V on the semaphore
        ACQUIRE_LOCK(&global_lock);
        klog_print("lock int - device (");
        klog_print_dec(global_lock);
        klog_print(") ");
        if (pcb != NULL) {
            pcb->p_s.reg_a0 = status_code;
            insertProcQ(&ready_queue, pcb);
        }
        int cpu_has_process = current_process[prid] != NULL;
        RELEASE_LOCK(&global_lock);
        if (cpu_has_process) LDST(GET_EXCEPTION_STATE_PTR(prid));
        else scheduler();
    } else if (exccode == IL_CPUTIMER) {
        setTIMER(TIMESLICE); 
        ACQUIRE_LOCK(&global_lock);
        klog_print("lock int - cpu timer (");
        klog_print_dec(global_lock);
        klog_print(") ");
        pcb_t *current_pcb = current_process[prid];
        if (current_pcb != NULL) {
            current_pcb->p_s = *GET_EXCEPTION_STATE_PTR(prid);
            insertProcQ(&ready_queue, current_pcb);
            current_process[prid] = NULL;
        }
        RELEASE_LOCK(&global_lock);
        scheduler();
    } else {
        klog_print("IntervalTimer interrupt | ");
        LDIT(PSECOND);
        klog_print("ldit | ");
        while (headBlocked(&(device_semaphores[PSEUDO_CLOCK_INDEX])) != NULL) {
            pcb_t *pcb = removeBlocked(&(device_semaphores[PSEUDO_CLOCK_INDEX]));
            insertProcQ(&ready_queue, pcb);
        }
        klog_print("pcbs unblocked | ");

        ACQUIRE_LOCK(&global_lock); // TODO: si blocca qui perche' l'altra cpu ha gia' richiesto il lock e sta aspettando
        klog_print("lock int - int.timer (");
        klog_print_dec(global_lock);
        klog_print(") ");

        int cpu_has_process = current_process[prid] != NULL;
        RELEASE_LOCK(&global_lock);
        klog_print("return | ");
        if (cpu_has_process) LDST(GET_EXCEPTION_STATE_PTR(prid));
        else scheduler();
    }

}
