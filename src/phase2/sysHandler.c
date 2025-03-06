#include "uriscv/types.h"

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "../headers/const.h"

extern volatile unsigned int global_lock;
extern int process_count;
extern struct list_head ready_queue;
extern struct pcb_t *current_process[NCPU];


void sysHandler(state_t *state) {

    int a0 = state->gpr[24];

    ACQUIRE_LOCK(&global_lock);
    switch (a0) {
    case -1:
        createProcess(state); 
        break;
    default:
        break;
    }
    RELEASE_LOCK(&global_lock);
}

void createProcess(state_t *state) {

    pcb_t *newProc = allocPcb();

    if (newProc == NULL) {
        state->gpr[24] = -1;
        return;
    }

    state_t *initialState = state->gpr[25];
    newProc->p_s = *initialState;

    int prid = getPRID();
    struct pcb_t *caller = current_process[prid];

    insertProcQ(&ready_queue, newProc);
    insertChild(caller, newProc);

    process_count++;

    state->gpr[24] = newProc->p_pid;

    state_t* current_process_state = (state_t*)(0x0FFFF000 + prid * 0x94);
    current_process_state->pc_epc = (current_process_state->pc_epc) +4;

    LDST(current_process_state);
}
// GET_EXCEPTION_STATE_PTR(id) fa la stessa cosa?