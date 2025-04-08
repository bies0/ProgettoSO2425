extern void interruptHandler(int exccode);
extern void syscallHandler();

void exceptionHandler()
{
    klog_print("Exception -> ");
    int prid = getPRID();
    state_t *state = GET_EXCEPTION_STATE_PTR(prid);
    int exccode = state->cause & CAUSE_EXCCODE_MASK;
    if (CAUSE_IS_INT(state->cause)) { // e provare con getCAUSE()?
        klog_print("interrupt: ");
        // PASSING process to nucleus interrupt handler
        interruptHandler(exccode);
    } else {
        if (exccode >= 24 && exccode <= 28) {
            klog_print("TLB: ");
            // TODO: TLB exceptions
        } else if (exccode == 8 || exccode == 11) {
            klog_print("syscall: ");
            syscallHandler(state);
        } else if ((exccode >= 0 && exccode <= 7) || exccode == 9 || exccode == 10 || (exccode >= 12 && exccode <= 23)) {
            klog_print("trap: ");
            // TODO: Program trap exceptions
        }
    }
}
