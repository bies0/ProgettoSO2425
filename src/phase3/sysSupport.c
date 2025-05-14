// mancava la macro in const.h // segnalare ai tutor
#define READTERMINAL 5

#define KUSEGENDPAGES (KUSEG+PAGESIZE*(MAXPAGES-1))

#include "../klog.c"

extern void print(char *msg);
extern void print_dec(char *msg, unsigned int n);

extern void restoreCurrentProcess(state_t *state);
extern int suppDevSems[NSUPPSEM];
extern int suppDevSemsAsid[UPROCMAX];

extern volatile unsigned int global_lock;
extern int asidSemSwapPool;
extern void acquireSwapPoolTable(int asid);
extern void releaseSwapPoolTable();
extern void acquireDevice(int asid, int deviceIndex);
extern void releaseDevice(int asid, int deviceIndex);

extern int masterSemaphore;

int printToPrinter(char* msg, int lenMsg, int printNo);

void killUproc(int asidToTerminate) {
    if (asidSemSwapPool != asidToTerminate) { // TODO: commentare il fatto che non e' un problema se non accediamo questa variabile globale in mutua esclusione
        acquireSwapPoolTable(asidToTerminate);
    }
    for (int i = 0; i < POOLSIZE; i++) { // Optimization to eliminate extraneous writes to the backing store
        swap_t *swap = &swapPoolTable[i];
        if (swap->sw_asid == asidToTerminate) {
            swap->sw_asid = -1;
        }
    }
    releaseSwapPoolTable();

    int deviceIndex = suppDevSemsAsid[asidToTerminate-1];
    if (deviceIndex != -1) {
        releaseDevice(asidToTerminate, deviceIndex);
    }

    SYSCALL(VERHOGEN, (int)&masterSemaphore, 0, 0);
    SYSCALL(TERMPROCESS, 0, 0, 0);
}

void supportTrapHandler(int asidToTerminate) {
    killUproc(asidToTerminate);
}

int printToTerminal(char* msg, int lenMsg, int termNo) {
    termreg_t* devReg = (termreg_t*)DEV_REG_ADDR(IL_TERMINAL, termNo);
    unsigned int status;
    unsigned int value;
    int charSent = 0;
    while (charSent < lenMsg) {
        value = PRINTCHR | (((unsigned int)*msg) << 8);
        status = SYSCALL(DOIO, (int)&devReg->transm_command, (int)value, 0);
        if ((status & 0xFF) != 5) {
            return -status;
        }
        msg++;
        charSent++;
    }
    return charSent;
}

int printToPrinter(char* msg, int lenMsg, int printNo) {
    dtpreg_t* devReg = (dtpreg_t*)DEV_REG_ADDR(IL_PRINTER, printNo);
    unsigned int status;
    int charSent = 0;
    while (charSent < lenMsg) {
        devReg->data0 = (unsigned int)*msg;
        status = SYSCALL(DOIO, (int)&devReg->command, TRANSMITCHAR, 0);
        if ((status & 0xFF) != 1) {
            return -status;
        }
        msg++;
        charSent++;
    }
    return charSent;
}

int inputTerminal(char* addrReturn, int termNo) {
    termreg_t* devReg = (termreg_t*)DEV_REG_ADDR(IL_TERMINAL, termNo);
    int msgLen = 0;
    while (1) {
        int status = SYSCALL(DOIO, (int)&devReg->recv_command, RECEIVECHAR, 0);
        if ((status & 0xFF) != 5) {
            return -status;
        }
        char receivedCh = (char)(status >> 8);
        *addrReturn = receivedCh;
        addrReturn++;
        msgLen++;
        if (receivedCh == '\n') break;
    }
    return msgLen;
}

void writeDevice(state_t* state, int asid, int operation) {
    char* vAddrMsg = (char*)state->reg_a1;
    int msgLen = (int)state->reg_a2;
    if (msgLen < 0 || msgLen > MAXSTRLENG) {
        killUproc(asid);
    }
    if ((memaddr)vAddrMsg < KUSEG || (memaddr)vAddrMsg > 0xFFFFFFFF) {
        killUproc(asid);
    }
    int devNo = asid-1;
    int status;
    if (operation == WRITETERMINAL) {
        int deviceIndex = (4*8)+devNo;
        acquireDevice(asid, deviceIndex);
        status = printToTerminal(vAddrMsg, msgLen, devNo);
        releaseDevice(asid, deviceIndex);
    }
    else if (operation == WRITEPRINTER) {
        int deviceIndex = (3*8)+devNo;
        acquireDevice(asid, deviceIndex);
        status = printToPrinter(vAddrMsg, msgLen, devNo);
        releaseDevice(asid, deviceIndex);
    }
    state->reg_a0 = status;
    restoreCurrentProcess(state);
}

void readTerminal(state_t* state, int asid) {
    char* vAddrReturn = (char*)state->reg_a1;
    if ((memaddr)vAddrReturn < KUSEG || (memaddr)vAddrReturn > 0xFFFFFFFF) {
        killUproc(asid);
    }
    int devNo = asid-1;
    int deviceIndex = (5*8)+devNo;
    acquireDevice(asid, deviceIndex);
    int status = inputTerminal(vAddrReturn, devNo);
    releaseDevice(asid, deviceIndex);
    state->reg_a0 = status;
    restoreCurrentProcess(state);
}

void generalExceptHandler()
{
    support_t* supp = (support_t*)SYSCALL(GETSUPPORTPTR, 0, 0, 0); 
    state_t* state = &(supp->sup_exceptState[GENERALEXCEPT]);
    int asid = supp->sup_asid;

    switch(state->reg_a0) {
        case TERMINATE:
            killUproc(asid);
            break;
        case WRITEPRINTER:
            writeDevice(state, asid, WRITEPRINTER);
            break;
        case WRITETERMINAL:
            writeDevice(state, asid, WRITETERMINAL);
            break;
        case READTERMINAL:
            readTerminal(state, asid);
            break;
        default:
            supportTrapHandler(asid);
            break;
    }
}
