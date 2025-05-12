extern void print(char *msg);
extern void print_dec(char *msg, unsigned int n);
extern void print_hex(char *msg, unsigned int n);
extern void print_state(state_t *state);

extern void klog_print(char *msg);
extern void klog_print_hex(unsigned int n);

extern void breakpoint();

extern volatile unsigned int global_lock; 
extern struct pcb_t *current_process[];
extern swap_t swapPoolTable[];
extern void supportTrapHandler(int asidToTerminate);
extern int suppDevSems[];

extern void acquireSwapPoolTable(int asid);
extern void releaseSwapPoolTable();

#define SWAP_POOL_START_ADDR (RAMSTART + (64 * PAGESIZE) + (NCPU * PAGESIZE))
#define GET_SWAP_POOL_ADDR(block) (SWAP_POOL_START_ADDR + (block)*PAGESIZE)
#define FLASH_INTLINENO 4
#define FLASHWRITE_ERROR 5
#define FLASHREAD_ERROR 4
#define STATUSMASK 0XFF
//#define FLASHADDR(asid) (START_DEVREG + ((FLASH_INTLINENO - 3) * 0x80) + (((asid) - 1) * 0x10)) // abbiamo trovato la macro gia' fatta, quindi questa si puo togliere se tutto funziona (TODO)
#define FLASHADDR(asid) DEV_REG_ADDR(IL_FLASH, asid-1)

unsigned int get_page_index(unsigned int entry_hi)
{
    unsigned int vpn = ENTRYHI_GET_VPN(entry_hi);
    if (vpn == (ENTRYHI_VPN_MASK >> VPNSHIFT)) return USERPGTBLSIZE-1;
    else return vpn;
}

//#define BETTERUPDATETLB // TODO
void updateTLB(pteEntry_t *entry)
{
#ifndef BETTERUPDATETLB
    TLBCLR();
#else
    setENTRYHI(entry->pte_entryHI);
    TLBP();
    unsigned int index = getINDEX();
    if (!(index & PRESENTFLAG)) { // Index.P == 0 => found in TLB
        setENTRYLO(entry->pte_entryLO);
        TLBWI();
    }
#endif
}

int pickFrame()
{
    static int frame = 0;
    return ((frame++) % POOLSIZE);
}

void flashRW(unsigned int asid, memaddr addr, int block, int is_read) // TODO: non da mai errore, pero non sembra funzionare
{
    if (is_read) print("Reading flash\n");
    else print("Writing flash\n");

    print_dec("flash: ", asid-1);
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

    int error = is_read ? FLASHREAD_ERROR : FLASHWRITE_ERROR;
    if ((status & STATUSMASK) == error) {
        if (is_read) print("ERROR reading flash\n");
        else print("ERROR writing flash\n");
        supportTrapHandler(asid);
    } else print_dec("status: ", status & STATUSMASK);

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
        print("program trap");
        supportTrapHandler(supp->sup_asid);
    }

    unsigned int ASID = supp->sup_asid;
    print_dec("asid: ", ASID);
    acquireSwapPoolTable(ASID);

    unsigned int p = get_page_index(state->entry_hi);
    print_dec("vpn: ", p);

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
        print("page found\n");
        updateTLB(swapPoolTable[i].sw_pte);
        if (supp->sup_privatePgTbl[p].pte_entryLO & ENTRYLO_VALID) { // page is valid
            print("page is valid\n");
            releaseSwapPoolTable();

            //print_state(state); // TODO: togli

            LDST(state); // TODO: non e' GET_EXCEPTION_STATE_PTR(getPRID())
        } 
    }

    int frame = pickFrame(); // TODO: se po fa' mejo // this is frame i
    memaddr frame_addr = GET_SWAP_POOL_ADDR(frame);
    print_dec("frame: ", frame);
    print_hex("frame addr: ", frame_addr);
    swap_t *swap = &swapPoolTable[frame];
    int occupied = swap->sw_asid != -1;
    if (occupied) {
        int k = swap->sw_pageNo;    
        pteEntry_t *entry = swap->sw_pte;
        entry->pte_entryLO &= ~VALIDON; // invalidate page k
        int x_asid = swap->sw_asid;
        updateTLB(entry);
        writeFlash(x_asid, frame_addr, k);
    }
    readFlash(ASID, frame_addr, p);
    swap->sw_asid = ASID;
    swap->sw_pageNo = p;
    swap->sw_pte = &supp->sup_privatePgTbl[p];
    swap->sw_pte->pte_entryLO |= VALIDON;
    swap->sw_pte->pte_entryLO &= ~ENTRYLO_PFN_MASK; // set PFN to 0
    swap->sw_pte->pte_entryLO |= (frame_addr << ENTRYLO_PFN_BIT); // set PFN to frame i's address

    releaseSwapPoolTable();

    // char buf[10]; // TODO: togli, sono prove per le syscall
    // int status = SYSCALL(5, (int)&buf[0], 0, 0);
    // SYSCALL(WRITETERMINAL, (int)&buf[0], status, 0);

    LDST(state);
}
