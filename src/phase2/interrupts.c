extern volatile unsigned int global_lock;
extern int process_count;
extern struct list_head ready_queue;
extern int device_semaphores[];
extern struct pcb_t *current_process[NCPU];
extern const unsigned int PSEUDO_CLOCK_INDEX;

extern void scheduler();
extern void print(char *msg); // presa dal test

#define TERMSTATMASK 0xFF
#define TRANSMITTED 5
#define RECEIVED 5
#define BITMAP(IntlineNo) (*(memaddr *)(CDEV_BITMAP_BASE + ((IntlineNo) - 3) * WS))

int getDevNo(int IntlineNo)
{
    memaddr bitmap = BITMAP(IntlineNo);

    //klog_print_hex(bitmap);
    //klog_print(" ");

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

int getDeviceOn(int DevNo)
{
    switch (DevNo)
    {
        case 0: return DEV0ON;
        case 1: return DEV1ON;
        case 2: return DEV2ON;
        case 3: return DEV3ON;
        case 4: return DEV4ON;
        case 5: return DEV5ON;
        case 6: return DEV6ON;
        case 7: return DEV7ON;
        default: return -1;
    }
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
        //klog_print(" -> ");

        memaddr devAddrBase = START_DEVREG + ((IntlineNo - 3) * 0x80) + (DevNo * 0x10);
        int status_code;
        ACQUIRE_LOCK(&global_lock);
        devreg_t devreg = *(devreg_t *)devAddrBase;
        if (IntlineNo == 7) {
            //klog_print("terminal: ");
            // Check if the command is transmit or receive
            status_code = devreg.term.transm_status;
            if ((status_code & TERMSTATMASK) == TRANSMITTED) {
                //klog_print("output | ");
                devreg.term.transm_command = ACK; 
                if ((devreg.term.recv_status & TERMSTATMASK) != RECEIVED) { 
                    //*(memaddr *)BITMAP(7) &= (~getDeviceOn(DevNo)); // TODO: dobbiamo settare il bit del device nella bitmap a 0?
                }
            } else {
                //klog_print("input | ");
                IntlineNo = 8; // Makes it easier to get the device semaphore
                status_code = devreg.term.recv_status;
                devreg.term.recv_command = ACK; 
                //*(memaddr *)BITMAP(7) &= (~getDeviceOn(DevNo)); 
            }
        } else {
            //klog_print("other | ");
            status_code = devreg.dtp.status;
            devreg.dtp.command = ACK;
            //*(memaddr *)BITMAP(IntlineNo) &= (~getDeviceOn(DevNo)); 
        }

        int *semaddr = &(device_semaphores[(IntlineNo-3)*8 + DevNo]); // get the right device semaphore
        pcb_t *pcb = removeBlocked(semaddr); // V on the semaphore
        if (pcb != NULL) {
            //klog_print(" pcbready | ");
            pcb->p_s.reg_a0 = status_code;
            insertProcQ(&ready_queue, pcb);
        } else {
            //klog_print("ERROR: no pcb blocked | ");
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
        if (cpu_has_process) {
            //klog_print("has process | ");
            LDST(state);
        } else {
            //klog_print("no process | ");
            scheduler();
        }
    }
}
