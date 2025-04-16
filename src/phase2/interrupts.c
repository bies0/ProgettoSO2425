extern volatile unsigned int global_lock;
extern int process_count;
extern struct list_head ready_queue;
extern int device_semaphores[];
extern struct pcb_t *current_process[NCPU];
extern const unsigned int PSEUDO_CLOCK_INDEX;

extern void scheduler();

#define TERMSTATMASK 0xFF
#define TRANSMITTED 5
#define RECEIVED 5
#define BITMAP(IntlineNo) (*(memaddr *)(CDEV_BITMAP_BASE + ((IntlineNo) - 3) * WS))

int getDevNo(int IntlineNo)
{
    memaddr bitmap = BITMAP(IntlineNo);

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
    int IntlineNo = 0;

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

    int prid = getPRID();
    if (exccode != IL_CPUTIMER && exccode != IL_TIMER) {
        int DevNo = getDevNo(IntlineNo);

        memaddr devAddrBase = START_DEVREG + ((IntlineNo - 3) * 0x80) + (DevNo * 0x10);
        int status_code;
        ACQUIRE_LOCK(&global_lock);
        devreg_t *devreg = (devreg_t *)devAddrBase;
        if (IntlineNo == 7) {
            // Check if the command is transmit or receive
            status_code = devreg->term.transm_status;
            if ((status_code & TERMSTATMASK) == TRANSMITTED) {
                devreg->term.transm_command = ACK; 
            } else {
                IntlineNo = 8; // Makes it easier to get the device semaphore
                status_code = devreg->term.recv_status;
                devreg->term.recv_command = ACK; 
            }
        } else {
            status_code = devreg->dtp.status;
            devreg->dtp.command = ACK;
        }

        int *semaddr = &(device_semaphores[(IntlineNo-3)*8 + DevNo]); // get the right device semaphore
        pcb_t *pcb = removeBlocked(semaddr); // V on the device semaphore
        if (pcb != NULL) { // there was actually a pcb blocked on the device semaphore
            pcb->p_s.reg_a0 = status_code;
            insertProcQ(&ready_queue, pcb);
        }
        int cpu_has_process = (current_process[prid] != NULL);
        RELEASE_LOCK(&global_lock);
        if (cpu_has_process) LDST(state);
        else scheduler();
    } else if (exccode == IL_CPUTIMER) {
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
        LDIT(PSECOND);
        ACQUIRE_LOCK(&global_lock);
        pcb_t *pcb = NULL;
        while ((pcb = removeBlocked(&(device_semaphores[PSEUDO_CLOCK_INDEX]))) != NULL) {
            insertProcQ(&ready_queue, pcb);
        }
        int cpu_has_process = current_process[prid] != NULL;
        RELEASE_LOCK(&global_lock);
        if (cpu_has_process) {
            LDST(state);
        } else {
            scheduler();
        }
    }
}
