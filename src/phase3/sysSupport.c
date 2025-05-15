#define READTERMINAL 5

#define PRINT_LINE  3
#define TERM_O_LINE 4
#define TERM_I_LINE 5
#define DEVSEMINDEX(line, dev) ((line)*DEVPERINT+dev) // calculates the device semaphore index in suppDevSems

extern void restoreCurrentProcess(state_t *state);
extern int suppDevSemsAsid[UPROCMAX];

extern int asidSemSwapPool;
extern void acquireSwapPoolTable(int asid);
extern void releaseSwapPoolTable();
extern void acquireDevice(int asid, int deviceIndex);
extern void releaseDevice(int asid, int deviceIndex);

extern int masterSemaphore;

void killUproc(int asidToTerminate) {
    if (asidSemSwapPool != asidToTerminate) {
        acquireSwapPoolTable(asidToTerminate);
    }
    for (int i = 0; i < POOLSIZE; i++) { // optimization to eliminate extraneous writes to the backing store
        swap_t *swap = &swapPoolTable[i];
        if (swap->sw_asid == asidToTerminate) {
            swap->sw_asid = -1;
        }
    }
    releaseSwapPoolTable();

    int deviceIndex = suppDevSemsAsid[asidToTerminate-1]; // release device if the uprocs was holding it
    if (deviceIndex != -1) {
        releaseDevice(asidToTerminate, deviceIndex);
    }

    SYSCALL(VERHOGEN, (int)&masterSemaphore, 0, 0); // signals to the main process that the uproc terminated
    SYSCALL(TERMPROCESS, 0, 0, 0);
}

void supportTrapHandler(int asidToTerminate) {
    killUproc(asidToTerminate);
}

// prints the string 'msg' to the terminal number 'termNo'
int printToTerminal(char* msg, int lenMsg, int termNo) {
    termreg_t* devReg = (termreg_t*)DEV_REG_ADDR(IL_TERMINAL, termNo);
    unsigned int status;
    unsigned int value;
    int charSent = 0;
    while (charSent < lenMsg) { // transmits to the terminal device 1 character at a time
        value = PRINTCHR | (((unsigned int)*msg) << 8);
        status = SYSCALL(DOIO, (int)&devReg->transm_command, (int)value, 0);
        if ((status & 0xFF) != CHARRECV) {
            return -status;
        }
        msg++;
        charSent++;
    }
    return charSent;
}

// prints the string 'msg' to the printer number 'printNo'
int printToPrinter(char* msg, int lenMsg, int printNo) {
    dtpreg_t* devReg = (dtpreg_t*)DEV_REG_ADDR(IL_PRINTER, printNo);
    unsigned int status;
    int charSent = 0;
    while (charSent < lenMsg) { // transmits to the printer device 1 character at a time
        devReg->data0 = (unsigned int)*msg;
        status = SYSCALL(DOIO, (int)&devReg->command, TRANSMITCHAR, 0);
        if ((status & 0xFF) != READY) {
            return -status;
        }
        msg++;
        charSent++;
    }
    return charSent;
}

// receives a string from the terminal number 'termNo' and stores it in 'addrReturn'
int inputTerminal(char* addrReturn, int termNo) {
    termreg_t* devReg = (termreg_t*)DEV_REG_ADDR(IL_TERMINAL, termNo);
    int msgLen = 0;
    while (1) { // reads 1 character at a time until a newline occurs
        int status = SYSCALL(DOIO, (int)&devReg->recv_command, RECEIVECHAR, 0);
        if ((status & 0xFF) != CHARRECV) {
            return -status;
        }
        char receivedCh = (char)(status >> 8); // extracts the character from the device status
        *addrReturn = receivedCh;
        addrReturn++;
        msgLen++;
        if (receivedCh == '\n') break;
    }
    return msgLen;
}

// executes a writeterminal or writeprinter syscall depending on the 'operation' value
void writeDevice(state_t* state, int asid, int operation) {
    char* vAddrMsg = (char*)state->reg_a1;
    int msgLen = (int)state->reg_a2;
    if (msgLen < 0 || msgLen > MAXSTRLENG) {
        killUproc(asid);
    }
    if ((memaddr)vAddrMsg < KUSEG || (memaddr)vAddrMsg > 0xFFFFFFFF) { // checks if the address is in the uproc's logical address space
        killUproc(asid);
    }
    int devNo = asid-1;
    int status;
    if (operation == WRITETERMINAL) {
        int deviceIndex = DEVSEMINDEX(TERM_O_LINE, devNo);
        acquireDevice(asid, deviceIndex);
        status = printToTerminal(vAddrMsg, msgLen, devNo);
        releaseDevice(asid, deviceIndex);
    }
    else if (operation == WRITEPRINTER) {
        int deviceIndex = DEVSEMINDEX(PRINT_LINE, devNo);
        acquireDevice(asid, deviceIndex);
        status = printToPrinter(vAddrMsg, msgLen, devNo);
        releaseDevice(asid, deviceIndex);
    }
    state->reg_a0 = status;
    restoreCurrentProcess(state);
}

// executes a readterminal syscall
void readTerminal(state_t* state, int asid) {
    char* vAddrReturn = (char*)state->reg_a1;
    if ((memaddr)vAddrReturn < KUSEG || (memaddr)vAddrReturn > 0xFFFFFFFF) {  // checks if the address is in the uproc's logical address space
        killUproc(asid);
    }
    int devNo = asid-1;
    int deviceIndex = DEVSEMINDEX(TERM_I_LINE, devNo);
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
        default: // if the exception is not a syscall it is a program trap
            supportTrapHandler(asid);
            break;
    }
}
