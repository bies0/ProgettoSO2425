#include "vmSupport.c"
#include "sysSupport.c"

#define UPROCS UPROCMAX
#define UPROCSTACKPGADDR 0xBFFFF000

// Data Structures
int masterSemaphore;

swap_t swapPoolTable[POOLSIZE];
int semSwapPoolTable;
int suppDevSems[NSUPPSEM]; // devices semaphores in support level
int suppDevSemsAsid[UPROCMAX]; // contains the index in suppDevSems of the device that the uproc is holding in mutual exclusion. When the uproc is terminated, if it's holding a device, it should free it.

void acquireDevice(int asid, int deviceIndex) {
    int* sem = &suppDevSems[deviceIndex];
    SYSCALL(PASSEREN, (int)sem, 0, 0);
    suppDevSemsAsid[asid-1] = deviceIndex;
}
void releaseDevice(int asid, int deviceIndex) {
    int* sem = &suppDevSems[deviceIndex];
    suppDevSemsAsid[asid-1] = -1;
    SYSCALL(VERHOGEN, (int)sem, 0, 0);
}

int asidSemSwapPool; // contains the asid of the uproc currently holding the swap pool table
void acquireSwapPoolTable(int asid) {
    SYSCALL(PASSEREN, (int)&semSwapPoolTable, 0, 0);
    asidSemSwapPool = asid;
}
void releaseSwapPoolTable() {
    asidSemSwapPool = -1;
    SYSCALL(VERHOGEN, (int)&semSwapPoolTable, 0, 0);
}

state_t uprocsStates[UPROCS] = {0};
support_t uprocsSuppStructs[UPROCS] = {0};

void p3test()
{
    // Data Structures Initialization
    for (int i = 0; i < POOLSIZE; i++) {
        swapPoolTable[i] = (swap_t){
            .sw_asid   = -1,
            .sw_pageNo = 0,
            .sw_pte    = NULL
        };
    }
    semSwapPoolTable = 1;
    for (int i = 0; i < NSUPPSEM; i++) suppDevSems[i] = 1;
    for (int i = 0; i < UPROCS; i++) suppDevSemsAsid[i] = -1;
    masterSemaphore = 0;
    asidSemSwapPool = -1;

    // U-Procs Initialization
    for (int i = 0; i < UPROCS; i++) {
        int ASID = i+1; // asids goes from 1 to 8 (0 is kernel's daemon)

        // States Initialization
        uprocsStates[i] = (state_t){
            .pc_epc = UPROCSTARTADDR,
            .reg_sp = USERSTACKTOP,
            .status = MSTATUS_MPIE_MASK, //  user mode is achieved by not setting the MSTATUS_MPP_M, all interrupts and PLT enabled
            .mie = MIE_ALL, // all interrupts enabled
            .entry_hi = ASID << ASIDSHIFT,
        };

        // Support Structures Initialization
        uprocsSuppStructs[i] = (support_t){
            .sup_asid = ASID,
            .sup_exceptContext = {
                (context_t){ // PGFAULTEXCEPT context
                    .pc = (memaddr)TLBExceptionHandler,
                    .status = MSTATUS_MPP_M,
                    .stackPtr = (memaddr)&(uprocsSuppStructs[i].sup_stackTLB[499]),
                },
                (context_t){ // GENERALEXCEPT context
                    .pc = (memaddr)generalExceptHandler,
                    .status = MSTATUS_MPP_M,
                    .stackPtr = (memaddr)&(uprocsSuppStructs[i].sup_stackGen[499]),
                }
            },
        };

        // Page Tables Initialization
        for (int j = 0; j < USERPGTBLSIZE; j++) {
            unsigned int vpn;
            if (j != USERPGTBLSIZE-1) vpn = KUSEG | (j << VPNSHIFT);
            else vpn = UPROCSTACKPGADDR; // page 32 is set to the stack starting address

            unsigned int entryHi = vpn | (ASID << ASIDSHIFT);
            unsigned int entryLO = DIRTYON; // GLOBALON and VALIDON are already set to 0
            uprocsSuppStructs[i].sup_privatePgTbl[j] = (pteEntry_t){
                .pte_entryHI = entryHi,
                .pte_entryLO = entryLO
            };
        }

        SYSCALL(CREATEPROCESS, (int)&(uprocsStates[i]), 0, (int)&(uprocsSuppStructs[i]));

    }

    for (int i = 0; i < UPROCS; i++) { // P on the master semaphore to wait for every uproc to terminate
        SYSCALL(PASSEREN, (int)&masterSemaphore, 0, 0);
    }

    print("\nTest successfully ended!\n");
    print("\nLe race conditions erano di nuovo sulle mie tracce.\n\n");

    SYSCALL(TERMPROCESS, 0, 0, 0);

    print("UNREACHABLE: test process must have terminated\n");
}
