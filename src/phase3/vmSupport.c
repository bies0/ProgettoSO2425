extern void print(char *msg);
extern volatile unsigned int global_lock; 
extern struct pcb_t *current_process[];
extern swap_t *swapPoolTable;
extern int semSwapPoolTable;

unsigned int get_page_index(state_t *state)
{
    unsigned int vpn = ENTRYHI_GET_VPN(state->entry_hi);
    return (vpn == 0xBFFFF) ? (USERPGTBLSIZE-1) : (vpn & 0x000FF);
}

void updateTLB(unsigned int entryHi)
{
    //setENTRYHI(state->entry_hi); TODO: forse non e' questo
    setENTRYHI(entryHi); // TODO: ma e' questo
    TLBP();
    unsigned int index = getINDEX();
    if (!(index & PRESENTFLAG)) { // Index.P == 0 => found in TLB
        //TLBWI(); // TODO: da fare in futuro
        TLBCLR();
    }
}

int pickFrame()
{
    static int frame = 0;
    return ((frame++) % POOLSIZE);
}

void writeFlash(unsigned int asid, memaddr addr) // TODO
{
    
}

// The Pager
void TLBExceptionHandler() {
    print("~~~ The Pager ~~~\n");

    support_t *supp = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0); 
    state_t *state = &(supp->sup_exceptState[0]);

    if (state->cause == EXC_MOD) {
        // TODO: treat it as a program trap (section 8)
    }

    SYSCALL(PASSEREN, (int)&semSwapPoolTable, 0, 0);
    unsigned int p = get_page_index(state);

    unsigned int ASID = ENTRYHI_GET_ASID(state->entry_hi);

    int i = 0;
    int found = FALSE;
    while (!found && i < POOLSIZE) {
        unsigned int sw_asid = swapPoolTable[i].sw_asid;
        if (sw_asid == ASID && swapPoolTable[i].sw_pageNo == p) {
            found = TRUE;     
        } else {
            i++;
        }
    } 
    if (found) {
        updateTLB(swapPoolTable[i].sw_pte->pte_entryHI);
        if (supp->sup_privatePgTbl[p].pte_entryLO & ENTRYLO_VALID) { // page is valid
            SYSCALL(VERHOGEN, (int)&semSwapPoolTable, 0, 0);
            LDST(state); // TODO: e' questo o e' quello in GET_EXCEPTION_STATE_PTR(getPRID())?
        } 
    }
    int frame = pickFrame(); // TODO: se po fa' mejo
    int occupied = swapPoolTable[frame].sw_asid != -1; 
    if (occupied) {
        int k = swapPoolTable[frame].sw_pageNo;    
        pteEntry_t *entry = swapPoolTable[frame].sw_pte;
        swapPoolTable[frame].sw_asid = -1;
        updateTLB(entry->pte_entryHI);
        writeFlash(ASID, BADADDR); // TODO
    }
}
