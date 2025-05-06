#include "uriscv/types.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "../headers/const.h"

extern void klog_print(); // TODO: togli
extern void klog_print_dec();
extern void klog_print_hex();

extern void scheduler();
extern void exceptionHandler();
extern void passUpOrDie(int index, state_t* state);
extern volatile unsigned int global_lock;
extern int process_count;
extern struct list_head ready_queue;
extern struct pcb_t *current_process[NCPU];
extern cpu_t current_process_start_time[NCPU];
extern int device_semaphores[];
extern const unsigned int PSEUDO_CLOCK_INDEX;


// returns the slice of time from the moment when the process running on CPU prid transitioned from ready to running until now
cpu_t getTimeSlice(int prid) {
    cpu_t current_time;
    STCK(current_time);
    cpu_t time_slice = current_time - current_process_start_time[prid];
    return time_slice;
}

// blocks the process in case of a blocking syscall
void blocksys(state_t *state, int prid, pcb_t* caller){
    state->pc_epc += WS;
    caller->p_s = *state;
    caller->p_time += getTimeSlice(prid);
    ACQUIRE_LOCK(&global_lock);
    current_process[prid] = NULL;
    RELEASE_LOCK(&global_lock);
    scheduler();
}

void restoreCurrentProcess(state_t *state) {
    state->pc_epc += WS;
    LDST(state);
}

void createProcess(state_t *state, int prid, pcb_t* caller) {
    ACQUIRE_LOCK(&global_lock);
    pcb_t *newProc = allocPcb(); // allocPcb has already set the pid
    if (newProc == NULL) {
        state->reg_a0 = -1;
        return;
    }
    state_t* initialState = (state_t*) state->reg_a1;
    newProc->p_s = *initialState;
    support_t* support = (support_t*) state->reg_a3;
    newProc->p_supportStruct = support;

    insertProcQ(&ready_queue, newProc);
    insertChild(caller, newProc);

    process_count++;

    RELEASE_LOCK(&global_lock);
    state->reg_a0 = newProc->p_pid;
    restoreCurrentProcess(state);
}

pcb_t* findProcessByPid(int pid) {
    pcb_t* process = NULL;
    int i = 0;
    // search among CPUs
    while (i < NCPU) {
        process = current_process[i];
        if (process->p_pid == pid) {
            return process;
        }
        i++;
    }

    process = NULL;
    // search in ready queue
    list_for_each_entry(process, &ready_queue, p_list) {
        if (process->p_pid == pid) return process;
    }

    process = NULL;
    // search in semaphores
    process = outBlockedPid(pid);
    return process;
}

// returns the prid of the CPU on which the process is running
int findInCurrents(pcb_t* process) {
    int i = -1;
    pcb_t* current = NULL;
    while (i < NCPU) {
        current = current_process[i];
        if (process == current) {
            return i;
        }
        i++;
    }
    return -1;
}

void removePcb(pcb_t* process) {
    // removes pcb from the semaphore on which it's blocked
    if (process->p_semAdd != NULL) {
        outBlocked(process);
    }

    // removes pcb from the ready_queue
    outProcQ(&ready_queue, process);

    // removes pcb from current_process (if present)
    int processor = findInCurrents(process);
    if (processor != -1) {
        current_process[processor] = NULL;
    }

    process_count--;
    freePcb(process);
}

// recursively kills the tree of pcb 'root'
void killTree(pcb_t* root) {
    if (root == NULL) return;

    outChild(root);
    while (!emptyChild(root)) {
        pcb_t* child = removeChild(root);
        killTree(child);
    }
    removePcb(root);
}

void terminateProcess(state_t *state, int prid, pcb_t* caller) {
    int pid = state->reg_a1;
    pcb_t* process = NULL;

    ACQUIRE_LOCK(&global_lock);
    if (pid == 0) { // kills the calling process
        killTree(caller);
        RELEASE_LOCK(&global_lock);
        scheduler();
    } else {
        process = findProcessByPid(pid); // searches for process with that pid
        if (process == NULL) { // pid was not found: continue the execution
            RELEASE_LOCK(&global_lock);
            restoreCurrentProcess(state);
        }
        killTree(process); // pid was found: kill the process
        if (findProcessByPid(caller->p_pid) == NULL) { // the process running on this CPU has been killed
            RELEASE_LOCK(&global_lock);
            scheduler(); // call the scheduler
        } else {
            RELEASE_LOCK(&global_lock);
            restoreCurrentProcess(state); // continue the execution
        }
    }
}

void passeren(state_t *state, int prid, pcb_t* caller) {
    int *semaddr = (int*) state->reg_a1;
    if (*semaddr == 0) { // blocks the process if value == 0
        ACQUIRE_LOCK(&global_lock);
        insertBlocked(semaddr, current_process[prid]);
        RELEASE_LOCK(&global_lock);
        blocksys(state, prid, caller);
    }
    else if (*semaddr == 1) {
        ACQUIRE_LOCK(&global_lock);
        pcb_t* removed = removeBlocked(semaddr);
        if (removed == NULL) { // unblocks the process if value == 1 and there is a blocked process
            *semaddr = 0;
        } else {
            insertProcQ(&ready_queue, removed);
        }
        RELEASE_LOCK(&global_lock);
        restoreCurrentProcess(state);
    }
}

void verhogen(state_t *state, int prid, pcb_t* caller) {
    int *semaddr = (int *) state->reg_a1;
    if (*semaddr == 1) { // blocks the process if value == 1
        ACQUIRE_LOCK(&global_lock);
        insertBlocked(semaddr, current_process[prid]);
        RELEASE_LOCK(&global_lock);
        blocksys(state, prid, caller);
    }
    else if (*semaddr == 0) {
        ACQUIRE_LOCK(&global_lock);
        pcb_t *removed = removeBlocked(semaddr);
        if (removed == NULL) { // unblocks the process if value == 0 and there is a blocked process
            *semaddr = 1;
        } else {
            insertProcQ(&ready_queue, removed);
        }
        RELEASE_LOCK(&global_lock);
        restoreCurrentProcess(state);
    }
}

#define INT_LINE_SIZE (DEVPERINT * DEVREGSIZE) // size of one interrupt line in bytes
void doInputOutput(state_t *state, int prid, pcb_t* caller) {
    memaddr commandAddr = state->reg_a1;
    int commandValue = (int) state->reg_a2;

    // Here we calculate the interrupt line number and the device number
    memaddr IntLineBase = commandAddr - START_DEVREG; // makes the address start from 0
    int IntlineNo = (IntLineBase / INT_LINE_SIZE) + 3; // gets the interrupt line number by dividing by the size of an interrupt line, then add 3 because the first interrupt line that can perform an I/O is the third (disk)
    //if (IntlineNo != 7) { // TODO: togli
    //    klog_print("lineno in doio: ");
    //    klog_print_dec(IntlineNo);
    //    klog_print("\n");
    //}
    int DevNo = (IntLineBase - ((IntlineNo-3)*INT_LINE_SIZE)) / (DEVREGLEN * WS); // gets the device number by going to the right interrupt line and then dividing by the size of one device register

    if (IntlineNo == 7 && (IntLineBase - ((7-3)*INT_LINE_SIZE + DevNo*DEVREGSIZE) == RECVCOMMAND)) {// it's a terminal and in receive
        IntlineNo = 8; // Makes it easier to get the device semaphore 
    }
    ACQUIRE_LOCK(&global_lock);
    insertBlocked(&(device_semaphores[(IntlineNo-3)*DEVPERINT+DevNo]), caller); // inserts the calling process on the right device semaphore

    // body of blocksys(), but here we need to set the commandAddr before calling the scheduler
    state->pc_epc += WS;
    caller->p_s = *state;
    caller->p_time += getTimeSlice(prid);
    current_process[prid] = NULL;
    /// end of body of blocksys()

    RELEASE_LOCK(&global_lock);

    *(memaddr *)commandAddr = commandValue; // sets the commandAddr as last instruction in order to avoid race conditions
    scheduler();
}

void getCPUTime(state_t *state, int prid, pcb_t* caller) {
    cpu_t time_slice = getTimeSlice(prid);
    state->reg_a0 = caller->p_time + time_slice;
    restoreCurrentProcess(state);
}

void waitForClock(state_t *state, int prid, pcb_t* caller) {
    ACQUIRE_LOCK(&global_lock);
    insertBlocked(&(device_semaphores[PSEUDO_CLOCK_INDEX]), caller);
    RELEASE_LOCK(&global_lock);
    blocksys(state, prid, caller);
}

void getSupportData(state_t *state, int prid, pcb_t* caller) {
    state->reg_a0 = (unsigned int) caller->p_supportStruct;
    restoreCurrentProcess(state);
}

void getProcessId(state_t *state, int prid, pcb_t* caller) {
    int parent = state->reg_a1;
    if (parent) { // parent != 0 -> get the pid of the parent of the calling process
        pcb_t* pcb_parent = caller->p_parent;
        if (pcb_parent == NULL) state->reg_a0 = 0;
        else state->reg_a0 = pcb_parent->p_pid;
    } else { // parent == 0 -> get the pid of the calling process
        state->reg_a0 = caller->p_pid;
    }
    restoreCurrentProcess(state);
}

void syscallHandler(state_t *state) {
    int a0 = state->reg_a0;
    int prid = getPRID();
    pcb_t* caller = current_process[prid];

    // if syscall has positive number, calls the passUpOrDie procedure
    if (a0 > 0) {
        passUpOrDie(GENERALEXCEPT, state);
    }

    // if in user mode launches a trap
    if (!(state->status & MSTATUS_MPP_MASK)) {
        state->cause = PRIVINSTR;
        exceptionHandler();
    }

    switch (a0) {
    case CREATEPROCESS:
        createProcess(state, prid, caller); 
        break;
    case TERMPROCESS:
        terminateProcess(state, prid, caller); 
        break;
    case PASSEREN:
        passeren(state, prid, caller); 
        break;
    case VERHOGEN:
        verhogen(state, prid, caller); 
        break;
    case DOIO:
        doInputOutput(state, prid, caller);
        break;
    case GETTIME:
        getCPUTime(state, prid, caller); 
        break;
    case CLOCKWAIT:
        waitForClock(state, prid, caller); 
        break;
    case GETSUPPORTPTR:
        getSupportData(state, prid, caller); 
        break;
    case GETPROCESSID:
        getProcessId(state, prid, caller); 
        break;
    default: // if syscall number didn't exist launches a trap
        state->cause = GENERALEXCEPT;
        exceptionHandler();
        break;
    }
}
