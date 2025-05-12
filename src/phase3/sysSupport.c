// mancava la macro in const.h // segnalare ai tutor
#define READTERMINAL 5

#define KUSEGENDPAGES (KUSEG+PAGESIZE*(MAXPAGES-1))

extern void print(char *msg);
extern void print_dec(char *msg, unsigned int n);

extern void restoreCurrentProcess(state_t *state);
extern int suppDevSems[NSUPPSEM];

extern int asidSemSwapPool;
extern void releaseSwapPoolTable();

extern int masterSemaphore;

void killUproc(int asidToTerminate) {
    if (asidSemSwapPool == asidToTerminate) {
        releaseSwapPoolTable();
    }
    SYSCALL(VERHOGEN, (int)&masterSemaphore, 0, 0);
    SYSCALL(TERMPROCESS, 0, 0, 0);
}

void supportTrapHandler(int asidToTerminate) { // TODO
    print("~~~ Trap Handler ~~~\n");
    killUproc(asidToTerminate);
}

int printToTerminal(char* msg, int lenMsg, int termNo) {
    termreg_t* devReg = (termreg_t*)DEV_REG_ADDR(IL_TERMINAL, termNo);
    unsigned int status;
    unsigned int value;
    int charSent = 0;
    int* sem = &suppDevSems[(4*8)+termNo];
    SYSCALL(PASSEREN, (int)sem, 0, 0);
    while (charSent < lenMsg) {
        value = PRINTCHR | (((unsigned int)*msg) << 8);
        status = SYSCALL(DOIO, (int)&devReg->transm_command, (int)value, 0);
        if ((status & 0xFF) != 5) {
            SYSCALL(VERHOGEN, (int)sem, 0, 0);
            return -status;
        }
        msg++;
        charSent++;
    }
    SYSCALL(VERHOGEN, (int)sem, 0, 0);
    return charSent;
}

int printToPrinter(char* msg, int lenMsg, int printNo) {
    dtpreg_t* devReg = (dtpreg_t*)DEV_REG_ADDR(IL_PRINTER, printNo);
    unsigned int status;
    int charSent = 0;
    int* sem = &suppDevSems[(3*8)+printNo];
    SYSCALL(PASSEREN, (int)sem, 0, 0);
    while (charSent < lenMsg) {
        devReg->data0 = (unsigned int)*msg;
        status = SYSCALL(DOIO, (int)&devReg->command, TRANSMITCHAR, 0);
        if ((status & 0xFF) != 1) {
            SYSCALL(VERHOGEN, (int)sem, 0, 0);
            return -status;
        }
        msg++;
        charSent++;
    }
    SYSCALL(VERHOGEN, (int)sem, 0, 0);
    return charSent;
}

int inputTerminal(char* addrReturn, int termNo) {
    termreg_t* devReg = (termreg_t*)DEV_REG_ADDR(IL_TERMINAL, termNo);
    int msgLen = 0;
    int* sem = &suppDevSems[(5*8)+termNo];
    SYSCALL(PASSEREN, (int)sem, 0, 0);
    while (1) {
        int status = SYSCALL(DOIO, (int)&devReg->recv_command, RECEIVECHAR, 0);
        if ((status & 0xFF) != 5) {
            print_dec("errore inputTerminal: ", status);
            SYSCALL(VERHOGEN, (int)sem, 0, 0);
            return -status;
        }
        char receivedCh = (char)(status >> 8);
        *addrReturn = receivedCh;
        addrReturn++;
        msgLen++;
        if (receivedCh == '\n') break;
    }
    SYSCALL(VERHOGEN, (int)sem, 0, 0);
    return msgLen;
}

void writeDevice(state_t* state, int asid, int operation) { // TODO
    char* vAddrMsg = (char*)state->reg_a1;
    int msgLen = (int)state->reg_a2;
    if (msgLen < 0 || msgLen > MAXSTRLENG) {
        killUproc(asid);
    }
    // TODO: tolto il controllo degli indirizzi validi per fare funzionare la syscall nel pager
    // if ((memaddr)vAddrMsg < (memaddr)KUSEG || (memaddr)vAddrMsg > (memaddr)KUSEGENDPAGES) {
    //     killUproc(asid);
    // }
    int devNo = asid-1;
    int status;
    if (operation == WRITETERMINAL) {
        status = printToTerminal(vAddrMsg, msgLen, devNo);
    }
    else if (operation == WRITEPRINTER) {
        status = printToPrinter(vAddrMsg, msgLen, devNo);
    }
    state->reg_a0 = status;
    restoreCurrentProcess(state);
}

void readTerminal(state_t* state, int asid) {
    char* vAddrReturn = (char*)state->reg_a1;
    // TODO: tolto il controllo degli indirizzi validi per fare funzionare la syscall nel pager
    // if ((memaddr)vAddrReturn < (memaddr)KUSEG || (memaddr)vAddrReturn > (memaddr)KUSEGENDPAGES) {
    //     killUproc(asid);
    // }
    int devNo = asid-1;
    int status = inputTerminal(vAddrReturn, devNo);
    state->reg_a0 = status;
    restoreCurrentProcess(state);
}

void generalExceptHandler()
{
    print("~~~ generalExceptHandler ~~~\n");
    
    support_t* supp = (support_t*)SYSCALL(GETSUPPORTPTR, 0, 0, 0); 
    state_t* state = &(supp->sup_exceptState[GENERALEXCEPT]);
    int asid = supp->sup_asid;

    print_dec("asid: ", asid);
    print_dec("syscall: ", state->reg_a0);

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
