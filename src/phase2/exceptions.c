extern void interruptHandler(state_t *state, int exccode);
extern void syscallHandler();
extern void scheduler();
extern void TLBExceptionHandler();
extern volatile unsigned int global_lock;
extern pcb_t *current_process[];

extern void print_long_dec(char *msg, unsigned int n);

extern unsigned int get_page_index(state_t *state);
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

// No calls to print in uTLB_RefillHandler! (anche se funzionano, boooh)
void uTLB_RefillHandler() { // TODO: non funziona
    print("~~~ TLB Refill ~~~\n");

#if 1 // TODO: togli
    int prid = getPRID();
    state_t *state = GET_EXCEPTION_STATE_PTR(prid);
    unsigned int p = get_page_index(state);

    ACQUIRE_LOCK(&global_lock);

    pcb_t *pcb = current_process[prid];
    if (pcb == NULL || pcb->p_supportStruct == NULL) {
        RELEASE_LOCK(&global_lock);
        print("Error in uTLB_RefillHandler\n");
        PANIC();
    }

    pteEntry_t *entry = &(pcb->p_supportStruct->sup_privatePgTbl[p]);
    //if (!(entry->pte_entryLO & VALIDON)) {
    //    RELEASE_LOCK(&global_lock);
    //    print("Page is not valid\n");
    //    TLBExceptionHandler();
    //}

    unsigned int entryHI = (p << VPNSHIFT) | (pcb->p_supportStruct->sup_asid << ASIDSHIFT);
    unsigned int entryLO = entry->pte_entryLO;

    setENTRYHI(entryHI);
    setENTRYLO(entryLO);
    TLBWR();

    RELEASE_LOCK(&global_lock);

    LDST(state);

#else
    // Vecchia uTLB_RefillHandler
    int prid = getPRID();
    setENTRYHI(0x80000000);
    setENTRYLO(0x00000000);
    TLBWR();
    LDST(GET_EXCEPTION_STATE_PTR(prid));
#endif
}   

void passUpOrDie(int index, state_t* state) {
    pcb_t *caller = NULL;
    ACQUIRE_LOCK(&global_lock);
    caller = current_process[getPRID()];
    RELEASE_LOCK(&global_lock);

    if (caller->p_supportStruct == NULL) {
        ACQUIRE_LOCK(&global_lock);
        killTree(caller);
        RELEASE_LOCK(&global_lock);
    } else {
        caller->p_supportStruct->sup_exceptState[index] = *state;
        unsigned int asid = ENTRYHI_GET_ASID(state->entry_hi);
        print_long_dec("asid in passup: ", asid);
        context_t* context = &caller->p_supportStruct->sup_exceptContext[index];
        LDCXT(context->stackPtr, context->status, context->pc);
    }
    scheduler(); // call the scheduler after the passUpOrDie procedure has killed the process
}

