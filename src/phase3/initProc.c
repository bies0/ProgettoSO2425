// 2.1: table initialization ?
// 2.2: backing store (already done within the Makefile)
//

extern int process_count;
extern void print(char *msg);

// Data Structures
#define SWAP_POOL_TABLE_ADDR (RAMSTART + (64 * PAGESIZE) + (NCPU * PAGESIZE))
swap_t *swapPoolTable;
int semSwapPoolTable;
int suppDevSems[NSUPPSEM];

void p3test()
{
    print("Phase 3 test begins!\n");

    // Data Structures Initialization
    swapPoolTable = (swap_t *)SWAP_POOL_TABLE_ADDR;
    for (int i = 0; i < POOLSIZE; i++) {
        swap_t swap = {
            .sw_asid   = -1,
            .sw_pageNo = 0,
            .sw_pte    = NULL
        };
        swapPoolTable[i] = swap;
        //*(swap_t *)(SWAP_POOL_TABLE_ADDR + i*sizeof(swap_t)) = swap; // E' la stessa cosa
    }
    semSwapPoolTable = 1;
    for (int i = 0; i < NSUPPSEM; i++) suppDevSems[i] = 1;

    print("Data Structures have been successfully initialized\n");

    // U-Procs Initialization: TODO (5.1)
    // 1. read the flash and get the information (with DOIO)
    // 2. perform CREATEPROCESS
    state_t uproc_state = {0};
    STST(&uproc_state); // TODO: questo serve?

    // Codice copiato dal test di fase 2 (TODO)
    //uproc_state.reg_sp = uproc_state.reg_sp - QPAGE;
    //uproc_state.pc_epc = (memaddr)0;
    //uproc_state.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M; 
    //uproc_state.mie = MIE_ALL;
    print("U-Procs have been successfully initialized\n");
    print("Test successfully ended!\n");
    print("\nLe race conditions erano di nuovo sulle mie tracce.\n\n");

    while (process_count > 1) { // TODO: fa shchifo, chiediamolo ai tutor
        SYSCALL(CLOCKWAIT, 0, 0, 0);
    }

    SYSCALL(TERMPROCESS, 0, 0, 0);

    print("Unreachable: test process must have terminated");
}
