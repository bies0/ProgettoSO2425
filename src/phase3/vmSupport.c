extern void print(char *msg);
extern void print_dec(char *msg, int n);
extern void print_long_dec(char *msg, unsigned int n);
extern void print_hex(char *msg, unsigned int n);

extern void breakpoint();

extern volatile unsigned int global_lock; 
extern struct pcb_t *current_process[];
extern swap_t *swapPoolTable;
extern int semSwapPoolTable;
extern void supportTrapHandler();
extern int suppDevSems[];

#define FLASH_INTLINENO 4
#define FLASHWRITE_ERROR 5
#define FLASHREAD_ERROR 5
#define STATUSMASK 0XFF
//#define FLASHADDR(asid) (START_DEVREG + ((FLASH_INTLINENO - 3) * 0x80) + (((asid) - 1) * 0x10)) // abbiamo trovato la macro gia' fatta, quindi questa si puo togliere se tutto funziona (TODO)
#define FLASHADDR(asid) DEV_REG_ADDR(IL_FLASH, asid-1)

unsigned int get_page_index(unsigned int entry_hi)
{
    unsigned int vpn = ENTRYHI_GET_VPN(entry_hi);
    if (vpn == (ENTRYHI_VPN_MASK >> VPNSHIFT)) return USERPGTBLSIZE-1;
    else return vpn;
}

void updateTLB(unsigned int entryHi)
{
    setENTRYHI(entryHi);
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

void flashRW(unsigned int asid, memaddr addr, int block, int is_read)
{
    print_dec("flash asid: ", asid);
    print_hex("addr: ", addr);
    print_dec("block: ", block);

    int *sem_flash = &suppDevSems[(FLASH_INTLINENO-3)*DEVPERINT+asid-1]; // the semaphore associated with the flash device
    SYSCALL(PASSEREN, (int)sem_flash, 0, 0);

    dtpreg_t *devreg = (dtpreg_t *)FLASHADDR(asid);
    int commandAddr = (int)&devreg->command;
    int commandValue = (is_read ? FLASHREAD : FLASHWRITE) | (block << 8);

    devreg->data0 = addr;

    int status = SYSCALL(DOIO, commandAddr, commandValue, 0);

    SYSCALL(VERHOGEN, (int)sem_flash, 0, 0);

    if ((status & STATUSMASK) == (is_read ? FLASHREAD_ERROR : FLASHWRITE_ERROR)) {
        if (is_read) print("ERROR reading flash\n");
        else print("ERROR writing flash\n");
        supportTrapHandler();
    }

    if (is_read) print("Reading flash done!\n");
    else print("Writing flash done!\n");
}

void readFlash(unsigned int asid, memaddr addr, int block)
{
    flashRW(asid, addr, block, TRUE);
}

void writeFlash(unsigned int asid, memaddr addr, int block)
{
    flashRW(asid, addr, block, FALSE);
}

// The Pager
void TLBExceptionHandler() {
    print("~~~ Pager ~~~\n");
    support_t *supp = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0); 
    state_t *state = &(supp->sup_exceptState[PGFAULTEXCEPT]);

    if (state->cause == EXC_MOD) {
        // treat it as a program trap
        supportTrapHandler();
    }

    SYSCALL(PASSEREN, (int)&semSwapPoolTable, 0, 0);

    unsigned int p = get_page_index(state->entry_hi);
    unsigned int ASID = supp->sup_asid;

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

    int frame = pickFrame(); // TODO: se po fa' mejo // this is frame i
    memaddr frame_addr = (memaddr)(swapPoolTable + frame);
    swap_t *swap = &swapPoolTable[frame];
    int occupied = swap->sw_asid != -1; 
    if (occupied) {
        int k = swap->sw_pageNo;    
        pteEntry_t *entry = swap->sw_pte;
        entry->pte_entryLO &= ~VALIDON; // invalidate page k
        int asid = swap->sw_asid;
        updateTLB(entry->pte_entryHI);
        writeFlash(asid, frame_addr, k);
    }
    readFlash(ASID, frame_addr, p);
    swap->sw_asid = ASID;
    swap->sw_pageNo = p;
    swap->sw_pte = &supp->sup_privatePgTbl[p];
    swap->sw_pte->pte_entryLO |= VALIDON;
    swap->sw_pte->pte_entryLO &= ~ENTRYLO_PFN_MASK; // set PFN to 0
    swap->sw_pte->pte_entryLO |= (frame << ENTRYLO_PFN_BIT); // set PFN to frame i
    updateTLB(swap->sw_pte->pte_entryHI);

    SYSCALL(VERHOGEN, (int)&semSwapPoolTable, 0, 0);

    breakpoint(); // TODO: togli

    LDST(state);
}
