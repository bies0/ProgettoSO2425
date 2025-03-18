#include "uriscv/types.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "../headers/const.h"

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

    // se non si riinizializza p_child dopo insertChild 
    // da errore in terminateProcess
    // quando si controlla se il pcb ha figli
    // risulta che newProc non sia vuoto
    // quando si cerca di terminare figli di figli il problema ritorna
    INIT_LIST_HEAD(&(newProc->p_child));

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
        process = current_process[getPRID()];
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

void removePcb(pcb_t* process) {
    outChild(process);
    if (process->p_semAdd != NULL) {
        outBlocked(process);
    }
    freePcb(process);
}

pcb_t* nextPcbInTree(pcb_t* ptr_pcb) {
    pcb_t* ptr_sibling = headProcQ(&ptr_pcb->p_sib);
    if (ptr_sibling != NULL) {
        return ptr_sibling;
    } else {
        return ptr_pcb->p_parent;
    }
}

void killTree(pcb_t* root) {
    pcb_t* ptr_pcb = root;
    pcb_t* ptr_pcb_to_kill;
    int has_no_child;
    while (1) {
        has_no_child = emptyChild(ptr_pcb);
        if (has_no_child && ptr_pcb == root) {
            outChild(root);
            process_count--;
            return;
        }
        else if (has_no_child) {
            ptr_pcb_to_kill = ptr_pcb;
            ptr_pcb = nextPcbInTree(ptr_pcb);
            removePcb(ptr_pcb_to_kill);
            process_count--;
        }
        else if (!has_no_child) {
            ptr_pcb = headProcQ(&ptr_pcb->p_child);
        }
    }
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
    if (process == NULL) ;
    else {
        killTree(process);
    }
    RELEASE_LOCK(&global_lock);
    if (killSelf) {
        scheduler();
    } else {
        restoreCurrentProcess(state);
    }
}

void passeren(state_t *state, int prid, pcb_t* caller) {
    int *semaddr = (int*) state->gpr[25]; // semaphore address
    if (*semaddr == 0) {
        ACQUIRE_LOCK(&global_lock);
        insertBlocked(semaddr, current_process[prid]); // insert in blocked list
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
        insertBlocked(semaddr, current_process[prid]); // insert in blocked list
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

void syscallHandler(state_t *state) {
    int a0 = state->gpr[24];
    int prid = getPRID();
    pcb_t* caller = current_process[prid];

    if (state->cause & MSTATUS_MPP_MASK) {
        state->cause = PRIVINSTR;
        // traphandler
    }

    // ACQUIRE_LOCK(&global_lock);
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
        // traphandler
        break;
    }
    // RELEASE_LOCK(&global_lock);
}