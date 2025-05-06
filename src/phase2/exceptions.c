extern void interruptHandler(state_t *state, int exccode);
extern void syscallHandler();
extern void scheduler();
extern void TLBExceptionHandler();
extern volatile unsigned int global_lock;
extern pcb_t *current_process[];

extern void print_long_dec(char *msg, unsigned int n);
extern void klog_print();
extern void klog_print_dec();

extern unsigned int get_page_index(unsigned int entry_hi);
extern void killTree(pcb_t* root); // declared in sysHandler.c
void passUpOrDie(int index, state_t* state); // forward declaration

void exceptionHandler()
{
    int prid = getPRID();

    state_t *state = GET_EXCEPTION_STATE_PTR(prid);
    int exccode = state->cause & CAUSE_EXCCODE_MASK;
    if (CAUSE_IS_INT(state->cause)) {
        interruptHandler(state, exccode); // passing process to nucleus interrupt handler
    } else {
        if (exccode >= 24 && exccode <= 28) {
            passUpOrDie(PGFAULTEXCEPT, state); // passUpOrDie for page fault
        } else if (exccode == 8 || exccode == 11) {
            syscallHandler(state); // passing control to the handler of the syscalls
        } else if ((exccode >= 0 && exccode <= 7) || exccode == 9 || exccode == 10 || (exccode >= 12 && exccode <= 23)) {
            passUpOrDie(GENERALEXCEPT, state); // passUpOrDie for general exception
        }
    }
}

// No calls to print in uTLB_RefillHandler!
void uTLB_RefillHandler() { // TODO: togli klog
    //klog_print("tlb refill: ");

    int prid = getPRID();
    state_t *state = GET_EXCEPTION_STATE_PTR(prid);

    unsigned int p = get_page_index(state->entry_hi);

    //klog_print("page ");
    //klog_print_dec(p);
    //klog_print(" | ");

    ACQUIRE_LOCK(&global_lock);
    pcb_t *pcb = current_process[prid];
    RELEASE_LOCK(&global_lock);

    if (pcb == NULL || pcb->p_supportStruct == NULL) {
        PANIC();
    }

    pteEntry_t *entry = &(pcb->p_supportStruct->sup_privatePgTbl[p]);

    setENTRYHI(entry->pte_entryHI);
    setENTRYLO(entry->pte_entryLO);
    TLBWR();

    //klog_print("fine.\n"); // TODO: togli

    breakpoint();

    LDST(state);
}   

void passUpOrDie(int index, state_t* state) {
    pcb_t *caller = NULL;
    int prid = getPRID();
    ACQUIRE_LOCK(&global_lock);
    caller = current_process[prid];
    RELEASE_LOCK(&global_lock);

    if (caller->p_supportStruct == NULL) {
        ACQUIRE_LOCK(&global_lock);
        killTree(caller);
        RELEASE_LOCK(&global_lock);
    } else {
        caller->p_supportStruct->sup_exceptState[index] = *state;
        context_t* context = &caller->p_supportStruct->sup_exceptContext[index];

        //ACQUIRE_LOCK(&global_lock); // TODO: senza questo ci da' opcode not handled, con questo crasha senza dire niente.
        //current_process[prid] = NULL;
        //RELEASE_LOCK(&global_lock);

        breakpoint(); // TODO: togli

        LDCXT(context->stackPtr, context->status, context->pc);
    }
    scheduler(); // call the scheduler after the passUpOrDie procedure has killed the process
}

