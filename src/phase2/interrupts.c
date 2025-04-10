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
    ////klog_print("bitmap: ");
    ////klog_print_dec(bitmap);
    ////klog_print(" ");

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

void interruptHandler(state_t *state, int exccode)
{
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
    // 12345678 | 12345678  | 12345678  | 12345678  | 12345678   | 12345678   | 1
    // 0 - disk | 1 - flash | 2 - ether | 3 - print | 4 - term_o | 5 - term_i | pseudo-clock

    int prid = getPRID();
    if (exccode != IL_CPUTIMER && exccode != IL_TIMER) {
        DevNo = getDevNo(IntlineNo);
        //klog_print("device ");
        //klog_print_dec(DevNo);
        //klog_print(" | ");

        memaddr devAddrBase = START_DEVREG + ((IntlineNo - 3) * 0x80) + (DevNo * 0x10);
        int status_code;
        ACQUIRE_LOCK(&global_lock);
        devreg_t devreg = *(devreg_t *)devAddrBase;
        if (IntlineNo == 7) {
            //klog_print("terminal (");
            // Check if the command is transmit or receive
            if (devreg.term.transm_command == 0 || devreg.term.transm_command == ACK) {
                //klog_print("input) | ");
                status_code = devreg.term.recv_status;
                devreg.term.recv_command = ACK; 
                IntlineNo = 8; // Makes it easier to get the device semaphore
            } else {
                //klog_print("output) | ");
                status_code = devreg.term.transm_status;
                devreg.term.transm_command = ACK; 
            }
        } else {
            status_code = devreg.dtp.status;
            devreg.dtp.command = ACK;
        }

        //klog_print(" - status: ");
        //klog_print_hex(status_code);
        //klog_print(" - ");

        int *semaddr = &(device_semaphores[(IntlineNo-3)*8 + DevNo]); // get the right device semaphore
        pcb_t *pcb = removeBlocked(semaddr); // V on the semaphore
        if (pcb != NULL) {
            //klog_print(" pcbready | ");
            pcb->p_s.reg_a0 = status_code;
            insertProcQ(&ready_queue, pcb);
        }
        int cpu_has_process = (current_process[prid] != NULL);
        //if (cpu_has_process) klog_print("yes pcb | ");
        //else klog_print("no pcb | ");
        RELEASE_LOCK(&global_lock);
        if (cpu_has_process) LDST(state);
        else scheduler();
    } else if (exccode == IL_CPUTIMER) {
        //klog_print("cputimer | ");
        setTIMER(TIMESLICE); 
        ACQUIRE_LOCK(&global_lock);
        pcb_t *current_pcb = current_process[prid];
        if (current_pcb != NULL) {
            current_pcb->p_s = *state;
            insertProcQ(&ready_queue, current_pcb);
            current_process[prid] = NULL;
        }
        RELEASE_LOCK(&global_lock);
        scheduler();
    } else {
        //klog_print("IntervalTimer | ");
        LDIT(PSECOND);
        ACQUIRE_LOCK(&global_lock);
        pcb_t *pcb = NULL;
        while ((pcb = removeBlocked(&(device_semaphores[PSEUDO_CLOCK_INDEX]))) != NULL) {
            insertProcQ(&ready_queue, pcb);
        }
        int cpu_has_process = current_process[prid] != NULL;
        RELEASE_LOCK(&global_lock);
        if (cpu_has_process) LDST(state);
        else scheduler();
    }
}
