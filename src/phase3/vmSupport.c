extern swap_t swapPoolTable[];
extern void supportTrapHandler(int asidToTerminate);

extern void acquireSwapPoolTable(int asid);
extern void releaseSwapPoolTable();
extern void acquireDevice(int asid, int deviceIndex);
extern void releaseDevice(int asid, int deviceIndex);

#define SWAP_POOL_START_ADDR (RAMSTART + (64 * PAGESIZE) + (NCPU * PAGESIZE))
#define GET_SWAP_POOL_ADDR(block) (SWAP_POOL_START_ADDR + (block)*PAGESIZE)
#define FLASH_INTLINENO 4
#define FLASHWRITE_ERROR 5
#define FLASHREAD_ERROR 4
#define STATUSMASK 0XFF
#define FLASHADDR(asid) DEV_REG_ADDR(IL_FLASH, asid-1)
#define STACK_VPN (ENTRYHI_VPN_MASK >> VPNSHIFT)

unsigned int get_page_index(unsigned int entry_hi)
{
    unsigned int vpn = ENTRYHI_GET_VPN(entry_hi);
    if (vpn == STACK_VPN) return USERPGTBLSIZE-1; // if the issued page is the stack one, returns the stack page number (31)
    else return vpn;
}

#define BETTERUPDATETLB // switch between the two tlb update algorithms
void updateTLB(pteEntry_t *entry)
{
#ifndef BETTERUPDATETLB // simply clears the tlb
    TLBCLR();
#else // probe the tlb to check if it needs to be updated
    setENTRYHI(entry->pte_entryHI);
    TLBP();
    unsigned int index = getINDEX();
    if (!(index & PRESENTFLAG)) { // Index.P == 0 => found in TLB
        setENTRYLO(entry->pte_entryLO);
        TLBWI();
    }
#endif
}

#define BETTERPAGERALGORITHM TRUE // switch between two page algorithms 
int pickFrame()
{
    static int frame = 0;
    if (BETTERPAGERALGORITHM) { // searches for an unoccupied page
        for (int i = 0; i < POOLSIZE; i++) {
            if (swapPoolTable[i].sw_asid == -1) {
                return i;
            }
        }
    }
    return ((frame++) % POOLSIZE); // round robin
}

// Dispatches an I/O operation on a flash device. In particular, addr is the frame address in the swap pool to be read/written and block is the block in the flash to be read/written.
void flashRW(unsigned int asid, memaddr addr, int block, int is_read)
{
    int semIndex = (FLASH_INTLINENO-3)*DEVPERINT+asid-1;
    acquireDevice(asid, semIndex);

    dtpreg_t *devreg = (dtpreg_t *)FLASHADDR(asid);
    int commandAddr = (int)&devreg->command;
    int commandValue = (is_read ? FLASHREAD : FLASHWRITE) | (block << 8);

    devreg->data0 = addr;

    int status = SYSCALL(DOIO, commandAddr, commandValue, 0);

    releaseDevice(asid, semIndex);

    int error = is_read ? FLASHREAD_ERROR : FLASHWRITE_ERROR;
    if ((status & STATUSMASK) == error) { // treat flash I/O error as a program trap
        releaseSwapPoolTable();
        supportTrapHandler(asid);
    }
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
    support_t *supp = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0); 
    state_t *state = &(supp->sup_exceptState[PGFAULTEXCEPT]);

    if (state->cause == EXC_MOD) { // treat it as a program trap
        supportTrapHandler(supp->sup_asid);
    }

    unsigned int p = get_page_index(state->entry_hi);
    unsigned int ASID = supp->sup_asid;
    acquireSwapPoolTable(ASID);

    int i = 0;
    int found = FALSE;
    while (!found && i < POOLSIZE) { // check if the page is loaded in the swap pool
        unsigned int sw_asid = swapPoolTable[i].sw_asid;
        if (sw_asid == ASID && swapPoolTable[i].sw_pageNo == p) {
            found = TRUE;     
        } else {
            i++;
        }
    } 
    if (found) {
        updateTLB(swapPoolTable[i].sw_pte);
        if (supp->sup_privatePgTbl[p].pte_entryLO & ENTRYLO_VALID) { // page is valid
            releaseSwapPoolTable();

            LDST(state);
        } 
    }

    int frame = pickFrame(); // this is frame i
    memaddr frame_addr = GET_SWAP_POOL_ADDR(frame);
    swap_t *swap = &swapPoolTable[frame];
    int occupied = swap->sw_asid != -1;
    if (occupied) {
        int k = swap->sw_pageNo; // page number of the occupying page
        pteEntry_t *entry = swap->sw_pte;
        entry->pte_entryLO &= ~VALIDON; // invalidate page k
        int x_asid = swap->sw_asid;
        updateTLB(entry);
        writeFlash(x_asid, frame_addr, k);
    }
    readFlash(ASID, frame_addr, p);
    // update swap pool table entry
    swap->sw_asid = ASID;
    swap->sw_pageNo = p;
    swap->sw_pte = &supp->sup_privatePgTbl[p];
    swap->sw_pte->pte_entryLO |= VALIDON; // validate page p
    swap->sw_pte->pte_entryLO &= ~ENTRYLO_PFN_MASK; // set PFN to 0
    swap->sw_pte->pte_entryLO |= (frame_addr); // set PFN to frame i's address

    updateTLB(swap->sw_pte);

    releaseSwapPoolTable();

    LDST(state);
}
