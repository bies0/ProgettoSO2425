// TODO:
// - ottimizzazioni in sezione 10
//   > rilasciare i semafori dei devices prima di terminare i processi in killUproc
//
//
// UPROCS:
// 0. strConcat
// 1. fibEight
// 2. terminalReader
// 3. fibEleven
// 4. terminalTest5
// 5. terminalTest2
// 6. terminalTest3
// 7. printerTest

#include "vmSupport.c"
#include "sysSupport.c"

#define UPROCS UPROCMAX
#define UPROCSTACKPGADDR 0xBFFFF000

// Print utilities // TODO: togli
extern void print(char *msg);
extern int printToTerminal(char* msg, int lenMsg, int termNo);
extern int printToPrinter(char* msg, int lenMsg, int printNo);
extern int inputTerminal(char* addrReturn, int termNo);
void print_dec(char *msg, unsigned int n)
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
int suppDevSems[NSUPPSEM]; // TODO: array con gli asid dei processi che tengono la mutua esclusione per liberarla quando il processo viene ucciso
int suppDevSemsAsid[UPROCMAX] = {-1, -1, -1, -1, -1, -1, -1, -1}; // TODO: facendo esperimenti abbiamo scoperto che così da meno problemi
// int suppDevSemsAsid[UPROCMAX];

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

int asidSemSwapPool;
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
    // for (int i = 0; i < UPROCS; i++) suppDevSemsAsid[i] = -1;
    masterSemaphore = 0;
    asidSemSwapPool = -1;

    // print("test funzioni syscall, scrivi una linea:\n");
    // char str[100]; // TODO: solo per fare vedere a Mathieu e a Flowerboy che funzionano
    // int msgLen = inputTerminal(&str[0], 0);
    // printToTerminal(&str[0], msgLen, 0);
    // printToPrinter(&str[0], msgLen, 7);


    // U-Procs Initialization
    for (int i = 0; i < UPROCS; i++) {
        int ASID = i+1;

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
            unsigned int entryLO = DIRTYON; // GLOBALON and VALIDON are set to 0
            uprocsSuppStructs[i].sup_privatePgTbl[j] = (pteEntry_t){
                .pte_entryHI = entryHi,
                .pte_entryLO = entryLO
            };
        }

        SYSCALL(CREATEPROCESS, (int)&(uprocsStates[i]), 0, (int)&(uprocsSuppStructs[i]));

    }

    for (int i = 0; i < UPROCS; i++) {
        SYSCALL(PASSEREN, (int)&masterSemaphore, 0, 0); // TODO: segnalare ai tutor che sulle specifiche dice di fare la V

    }

    print("\nTest successfully ended!\n");
    print("\nLe race conditions erano di nuovo sulle mie tracce.\n\n");

    SYSCALL(TERMPROCESS, 0, 0, 0);

    print("UNREACHABLE: test process must have terminated\n");
}
