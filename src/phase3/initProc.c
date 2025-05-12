// TODO:
// - ottimizzazioni in sezione 10
// - mettere NCPU a 8
// - mettere UPROCS a UPROCMAX

#include "vmSupport.c"
#include "sysSupport.c"

#include "../klog.c" // TODO: togli

#define UPROCSTACKPGADDR 0xBFFFF000

// Print utilities
#define ENABLE_PRINT FALSE
#define ENABLE_KLOG  FALSE

extern void print(char *msg);
extern int printToTerminal(char* msg, int lenMsg, int termNo);
extern int printToPrinter(char* msg, int lenMsg, int printNo);
extern int inputTerminal(char* addrReturn, int termNo);
void print_dec(char *msg, unsigned int n) // TODO (non importante): stampa al contrario
{
    if (n < 10) {
        char n_str[] = {n + '0', '\0'};
        print(msg);
        print(n_str);
        print("\n");
        return;
    } 
    print(msg);
    while (n > 0) {
        int d = n % 10;
        char d_str[] = {d + '0', '\0'};
        print(d_str);
        n /= 10;
    }
    print("\n");
}
void print_hex(char *msg, unsigned int n)
{
    const char digits[] = "0123456789ABCDEF";

    print(msg);
    do {
        char c = digits[n % 16];
        char str[] = {c, '\0'};
        print(str); 
        n /= 16;
    } while (n > 0);
    print("\n");
}
void print_state(state_t *state)
{
    print("state:\n");
    print_hex("  entryHi = ", state->entry_hi);
    print_hex("  pc      = ", state->pc_epc);
    print_hex("  status  = ", state->status);
}
//////////

void breakpoint() {}

// Data Structures
int masterSemaphore;

swap_t swapPoolTable[POOLSIZE];
int semSwapPoolTable;
int suppDevSems[NSUPPSEM];

int asidSemSwapPool = -1;
void acquireSwapPoolTable(int asid) {
    asidSemSwapPool = asid;
    SYSCALL(PASSEREN, (int)&semSwapPoolTable, 0, 0);
}
void releaseSwapPoolTable() {
    asidSemSwapPool = -1;
    SYSCALL(VERHOGEN, (int)&semSwapPoolTable, 0, 0);
}

#define UPROCS 1 //UPROCMAX // TODO: metti a UPROCMAX

state_t uprocsStates[UPROCS] = {0};
support_t uprocsSuppStructs[UPROCS] = {0};

void p3test()
{
    if (ENABLE_PRINT) print("Phase 3 test begins!\n");

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
    masterSemaphore = 0;

    if (ENABLE_PRINT) print("Data Structures have been successfully initialized\n");

    // print("test funzioni syscall, scrivi una linea:\n");
    // char str[100]; // TODO: solo per fare vedere a Mathieu e a Flowerboy che funzionano
    // int msgLen = inputTerminal(&str[0], 0);
    // printToTerminal(&str[0], msgLen, 0);
    // printToPrinter(&str[0], msgLen, 7);


    // U-Procs Initialization
    for (int i = 0; i < UPROCS; i++) {
        int ASID = i+1;

        //print_dec("Initializing UPROC ", ASID);

        // States Initialization
        uprocsStates[i] = (state_t){
            .pc_epc = UPROCSTARTADDR,
            .reg_sp = USERSTACKTOP,
            .status = MSTATUS_MPIE_MASK, //  user mode is achieved by not setting the MSTATUS_MPP_M, all interrupts and PLT enabled
            .mie = MIE_ALL, // all interrupts enabled
            .entry_hi = ASID << ASIDSHIFT,
        };

        //print("- states initialized\n");

        // Support Structures Initialization
        uprocsSuppStructs[i] = (support_t){
            .sup_asid = ASID,
            .sup_exceptContext = {
                (context_t){
                    .pc = (memaddr)TLBExceptionHandler,
                    .status = MSTATUS_MPP_M,
                    .stackPtr = (memaddr)&(uprocsSuppStructs[i].sup_stackTLB[499]),
                },
                (context_t){
                    .pc = (memaddr)generalExceptHandler,
                    .status = MSTATUS_MPP_M,
                    .stackPtr = (memaddr)&(uprocsSuppStructs[i].sup_stackGen[499]),
                }
            },
        };
        //print("- Support Structures initialized\n");

        // Page Tables Initialization
        for (int j = 0; j < USERPGTBLSIZE; j++) {
            // TODO: togliamo questo codice commentato dopo che ci siamo assicurati che funzioni allo stesso modo
            //if (j == USERPGTBLSIZE-1) uprocsSuppStructs[i].sup_privatePgTbl[USERPGTBLSIZE-1].pte_entryHI = UPROCSTACKPGADDR << VPNSHIFT; // page 32
            //else uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryHI = (KUSEG + j) << VPNSHIFT; // pages from 0 to 31
            //uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryHI = ASID << ASIDSHIFT;
            //uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryLO |= DIRTYON;
            //uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryLO &= ~GLOBALON;
            //uprocsSuppStructs[i].sup_privatePgTbl[j].pte_entryLO &= ~VALIDON;

            unsigned int vpn;
            if (j != USERPGTBLSIZE-1) vpn = KUSEG | (j << VPNSHIFT);
            else vpn = UPROCSTACKPGADDR; // page 32 is set to the stack starting address

            unsigned int entryHi = vpn | (ASID << ASIDSHIFT);
            unsigned int entryLO = DIRTYON; // GLOBALON and VALIDON are set to 0 (TODO: ma e' giusto? altrimenti lo facciamo in piu' passaggi come sopra)
            uprocsSuppStructs[i].sup_privatePgTbl[j] = (pteEntry_t){
                .pte_entryHI = entryHi,
                .pte_entryLO = entryLO
            };
        }
        //print("- Page Tables initialized\n");

        //print("~ Creating the UPROC\n");
        SYSCALL(CREATEPROCESS, (int)&(uprocsStates[i]), 0, (int)&(uprocsSuppStructs[i]));

    }
    //print("U-Procs have been successfully initialized\n");

    for (int i = 0; i < UPROCS; i++) {
        SYSCALL(PASSEREN, (int)&masterSemaphore, 0, 0); // TODO: segnalare ai tutor che sulle specifiche dice di fare la V
    }

    print("\nTest successfully ended!\n");
    //print("\nLe race conditions erano di nuovo sulle mie tracce.\n\n");

    SYSCALL(TERMPROCESS, 0, 0, 0);

    print("UNREACHABLE: test process must have terminated\n");
}
