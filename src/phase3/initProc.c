// 2.1: table initialization ?
// 2.2: backing store (already done within the Makefile)

// TODO: mettere il masterSemaphore in tutti i testers

#include "globals.h"
#include "vmSupport.c"
#include "sysSupport.c"

extern void print(char *msg);
extern void TLBHandler(); // TODO: abbiamo scoperto che in realta' basta includere il file c e non dichiarare la funzione extern
extern void generalExceptHandler(); // TODO: anche qui

// Data Structures
#define SWAP_POOL_TABLE_ADDR (RAMSTART + (64 * PAGESIZE) + (NCPU * PAGESIZE))
swap_t *swapPoolTable;
int semSwapPoolTable;
int suppDevSems[NSUPPSEM];

#define UPROCS 1 // UPROCMAX // TODO: cambia

state_t uprocsStates[UPROCS] = {0};
support_t uprocsSuppStructs[UPROCS] = {0};

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
    masterSemaphore = 0; // TODO: chiedere ai tutor se va bene averlo messo nel file `globals.h`

    print("Data Structures have been successfully initialized\n");

    // U-Procs Initialization: TODO (5.1) (read the flash and get the information (with DOIO), come si fa?)
    for (int i = 0; i < UPROCS; i++) {
        int ASID = i+1;
        // States Initialization
        uprocsStates[i] = (state_t){
            .pc_epc = UPROCSTARTADDR,
            .reg_sp = USERSTACKTOP,
            .status = (MSTATUS_MPIE_MASK), //  user mode is achieved by not setting the MSTATUS_MPP_M, all interrupts enabled (MSTATUS_MPIE_MASK) e PLT enabled (TODO (TUTOR): chiedere ai tutor)
            .mie = MIE_ALL,
            .entry_hi = ASID, // TODO (TUTOR): EntryHI.ASID? (oppure (ASID)<<32), chiedere ai tutor
        };
        print("UPROCS states initialized\n");

        // Support Structures Initialization
        uprocsSuppStructs[i] = (support_t){
            .sup_asid = ASID,
            .sup_exceptContext = {
                (context_t){
                    .pc = (memaddr)TLBHandler,
                    .status = MSTATUS_MPP_M, // TODO: cosi' va in kernel mode?
                    //.stackPtr = &uprocsSuppStructs[i].sup_stackTLB[499] // TODO (TUTOR): chiedere ai tutor, sup_stackTLB non esiste
                },
                (context_t){
                    .pc = (memaddr)generalExceptHandler,
                    .status = MSTATUS_MPP_M, // TODO: cosi' va in kernel mode?
                    //.stackPtr = &uprocsSuppStructs[i].sup_stackGen[499] // TODO (TUTOR): chiedere ai tutor, sup_stackGen non esiste
                }
            },
        };
        print("UPROCS Support Structures initialized\n");

        // Page Tables Initialization (TODO: non e' elegante)
        for (int j = 0; j < USERPGTBLSIZE; j++) {
            if (j == USERPGTBLSIZE-1) uprocsSuppStructs[i].sup_privatePgTbl[USERPGTBLSIZE-1].pte_entryHI = 0xBFFFF << VPNSHIFT; // page 32
            else uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryHI = (KUSEG + j) << VPNSHIFT; // pages from 0 to 31
            uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryHI = ASID << ASIDSHIFT;
            uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryLO |= DIRTYON;
            uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryLO &= ~GLOBALON;
            uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryLO &= ~VALIDON;
        }
        print("UPROCS Page Tables initialized\n");

        SYSCALL(CREATEPROCESS, (int)&uprocsStates[i], PROCESS_PRIO_LOW, (int)&uprocsSuppStructs[i]);

    }
    print("U-Procs have been successfully initialized\n");

    for (int i = 0; i < UPROCS; i++) {
        SYSCALL(PASSEREN, (int)&masterSemaphore, 0, 0); // TODO: segnalare ai tutor che sulle specifiche dice di fare la V
    }

    print("Test successfully ended!\n");
    print("\n");
    print("Le race conditions erano di nuovo sulle mie tracce.\n");
    print("\n");

    SYSCALL(TERMPROCESS, 0, 0, 0);

    print("UNREACHABLE: test process must have terminated");
}
