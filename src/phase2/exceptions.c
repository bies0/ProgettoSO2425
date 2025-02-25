extern void interruptHandler();

void exceptionHandler()
{
    klog_print("Exception handler | ");
    int prid = getPRID();
    state_t *state = GET_EXCEPTION_STATE_PTR(prid);
    klog_print("Got cause | ");
    if (CAUSE_IS_INT(state->cause)) { // e provare con getCAUSE()?
        klog_print("cause is int | ");
        // PASSING process to nucleus interrupt handler
        interruptHandler();
    } else {
        int exccode = state->cause & CAUSE_EXCCODE_MASK;
        if (exccode >= 24 && exccode <= 28) {
            klog_print("TLB exception | ");
            // TODO: TLB exceptions
        } else if (exccode == 8 || exccode == 11) {
            klog_print("syscall exception | ");
            // TODO: SYSCALL exceptions
        } else if ((exccode >= 0 && exccode <= 7) || exccode == 9 || exccode == 10 || (exccode >= 12 && exccode <= 23)) {
            klog_print("program trap exception | ");
            // TODO: Program trap exceptions
        }
    }
    // state->status & MSTATUS_MPP_MASK
    // How to determine if in kernel or user mode:
}
