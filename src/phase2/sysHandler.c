#include "uriscv/types.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "../headers/const.h"

extern void scheduler();
extern void exceptionHandler();
extern volatile unsigned int global_lock;
extern int process_count;
extern struct list_head ready_queue;
extern struct pcb_t *current_process[NCPU];
extern cpu_t current_process_start_time[NCPU];
extern int asl_pseudo_clock;


cpu_t getTimeSlice(int prid) {
    cpu_t current_time;
    STCK(current_time);
    cpu_t time_slice = current_time - current_process_start_time[prid];
    return time_slice;
}

void blocksys(state_t *state, int prid, pcb_t* caller){
    state->pc_epc += 4;
    caller->p_s = *state;
    caller->p_time += getTimeSlice(prid);
    scheduler();
}

void restoreCurrentProcess(state_t *state) {
    state->pc_epc += 4;
    LDST(state);
}

void createProcess(state_t *state, int prid, pcb_t* caller) {
    ACQUIRE_LOCK(&global_lock);
    pcb_t *newProc = allocPcb();
    if (newProc == NULL) {
        state->gpr[24] = -1;
        return;
    }
    state_t* initialState = (state_t*) state->gpr[25];
    newProc->p_s = *initialState;
    support_t* support = (support_t*) state->gpr[27];
    newProc->p_supportStruct = support;

    insertProcQ(&ready_queue, newProc);
    insertChild(caller, newProc);

    process_count++;

    RELEASE_LOCK(&global_lock);
    state->gpr[24] = newProc->p_pid;
    restoreCurrentProcess(state);
}

pcb_t* findProcessByPid(int pid) {
    pcb_t* process;
    int i = 0;
    // ricerca fra i processori
    while (i < NCPU) {
        process = current_process[i];
        if (process->p_pid == pid) {
            return process;
        }
        i++;
    }
    // ricerca fra i processi ready
    process = headProcQ(&ready_queue);
    while (process != NULL) {
        if (process->p_pid == pid) {
            return process;
        }
        if (&process->p_list == &ready_queue) process = NULL;
        else {
            process = headProcQ(&process->p_list);
        }   
    }
    // ricerca fra i semafori
    process = outBlockedPid(pid);
    return process;
}

int findInCurrents(pcb_t* process) {
    int prid = getPRID();
    int i = 0;
    pcb_t* current;
    while (i < NCPU) {
        current = current_process[i];
        if (i != prid && process == current) {
            return i;
        }
        i++;
    }
    return -1;
}

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
    outChild(process);
    if (process->p_semAdd != NULL) {
        outBlocked(process);
    }
    outProcQ(&ready_queue, process);
    int processor = findInCurrents(process);
    if (processor != -1) {
        callSchedulerOnProcessor(processor);
    }
    freePcb(process);
    process_count--;
}

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
    int pid = state->gpr[25];
    pcb_t* process;
    int killSelf = 0;
    ACQUIRE_LOCK(&global_lock);
    if (pid == 0) {
        process = caller;
        killSelf = 1;
    }
    else {
        process = findProcessByPid(pid);
    }
    if (process != NULL) {
        killTree(process);
    }
    RELEASE_LOCK(&global_lock);
    if (killSelf || findProcessByPid(caller->p_pid) != NULL) {
        scheduler();
    } else {
        restoreCurrentProcess(state);
    }
}

void passeren(state_t *state, int prid, pcb_t* caller) {
    int *semaddr = (int*) state->gpr[25];
    if (*semaddr == 0) {
        ACQUIRE_LOCK(&global_lock);
        insertBlocked(semaddr, current_process[prid]);
        RELEASE_LOCK(&global_lock);
        blocksys(state, prid, caller);
    }
    else if (*semaddr == 1) {
        ACQUIRE_LOCK(&global_lock);
        pcb_t* removed = removeBlocked(semaddr);
        if (removed == NULL) {
            *semaddr = 0;
        } else {
            insertProcQ(&ready_queue, removed);
        }
        RELEASE_LOCK(&global_lock);
        restoreCurrentProcess(state);
    }
}

void verhogen(state_t *state, int prid, pcb_t* caller) {
    int *semaddr = (int *) state->gpr[25];
    if (*semaddr == 1) {
        ACQUIRE_LOCK(&global_lock);
        insertBlocked(semaddr, current_process[prid]);
        RELEASE_LOCK(&global_lock);
        blocksys(state, prid, caller);
    }
    else if (*semaddr == 0) {
        ACQUIRE_LOCK(&global_lock);
        pcb_t *removed = removeBlocked(semaddr);
        if (removed == NULL) {
            *semaddr = 1;
        } else {
            insertProcQ(&ready_queue, removed);
        }
        RELEASE_LOCK(&global_lock);
        restoreCurrentProcess(state);
    }
}

void doInputOutput(state_t *state, int prid, pcb_t* caller) {
    int* commandAddr = (int *) state->gpr[25];
    int commandValue = (int) state->gpr[26];

    ACQUIRE_LOCK(&global_lock);
    insertBlocked(commandAddr, caller);

    state->pc_epc += 4;
    caller->p_s = *state;
    caller->p_time += getTimeSlice(prid);

    *commandAddr = commandValue;
    RELEASE_LOCK(&global_lock);
    scheduler();
}

void getCPUTime(state_t *state, int prid, pcb_t* caller) {
    cpu_t time_slice = getTimeSlice(prid);
    state->gpr[24] = caller->p_time + time_slice;
    restoreCurrentProcess(state);
}

void waitForClock(state_t *state, int prid, pcb_t* caller) {
    ACQUIRE_LOCK(&global_lock);
    insertBlocked(&asl_pseudo_clock, caller);
    RELEASE_LOCK(&global_lock);
    blocksys(state, prid, caller);
}

void getSupportData(state_t *state, int prid, pcb_t* caller) {
    state->gpr[24] = (unsigned int) caller->p_supportStruct;
    restoreCurrentProcess(state);
}

void getProcessId(state_t *state, int prid, pcb_t* caller) {
    int parent = state->gpr[25];
    if (parent) {
        pcb_t* pcp_parent = caller->p_parent;
        if (pcp_parent == NULL) state->gpr[24] = 0;
        else state->gpr[24] = pcp_parent->p_pid;
    } else {
        state->gpr[24] = caller->p_pid;
    }
    restoreCurrentProcess(state);
}

void passUp(int index, pcb_t* caller, state_t* state) {
    if (caller->p_supportStruct == NULL) {
        killTree(caller);
        scheduler();
    } else {
        caller->p_s = *state;
        caller->p_supportStruct->sup_exceptState[index] = *state;
        context_t* context = &caller->p_supportStruct->sup_exceptContext[index];
        LDCXT(context->stackPtr, context->status, context->pc);
    }
}

void syscallHandler(state_t *state) {
    int a0 = state->gpr[24];
    int prid = getPRID();
    pcb_t* caller = current_process[prid];

    if (a0 > 0) {
        passUp(GENERALEXCEPT, caller, state);
    }

    if (state->cause & MSTATUS_MPP_MASK) {
        state->cause = PRIVINSTR;
        exceptionHandler();
    }

    switch (a0) {
    case -1:
        createProcess(state, prid, caller); 
        break;
    case -2:
        terminateProcess(state, prid, caller); 
        break;
    case -3:
        passeren(state, prid, caller); 
        break;
    case -4:
        verhogen(state, prid, caller); 
        break;
    case -5:
        doInputOutput(state, prid, caller);
        break;
    case -6:
        getCPUTime(state, prid, caller); 
        break;
    case -7:
        waitForClock(state, prid, caller); 
        break;
    case -8:
        getSupportData(state, prid, caller); 
        break;
    case -9:
        getProcessId(state, prid, caller); 
        break;
    default:
        state->cause = GENERALEXCEPT;
        exceptionHandler();
        break;
    }
}
