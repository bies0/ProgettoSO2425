extern void interruptHandler(state_t *state, int exccode);
extern void syscallHandler();
extern void passUp(int index, state_t* state);
extern volatile unsigned int global_lock;
extern pcb_t *current_process[];

void exceptionHandler()
{
    int prid = getPRID();

    state_t *state = GET_EXCEPTION_STATE_PTR(prid);
    int exccode = state->cause & CAUSE_EXCCODE_MASK;
    if (CAUSE_IS_INT(state->cause)) {
        interruptHandler(state, exccode); // passing process to nucleus interrupt handler
    } else {
        if (exccode >= 24 && exccode <= 28) {
            passUp(PGFAULTEXCEPT, state);
        } else if (exccode == 8 || exccode == 11) {
            syscallHandler(state);
        } else if ((exccode >= 0 && exccode <= 7) || exccode == 9 || exccode == 10 || (exccode >= 12 && exccode <= 23)) {
            passUp(GENERALEXCEPT, state);
        }
    }
}
