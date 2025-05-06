extern void print(char *msg);

// mancava la macro in const.h // segnalare ai tutor
#define READTERMINAL 5

void supportTrapHandler() { // TODO
    //print("~~~ Trap Handler ~~~\n");
}

void writePrinter(support_t *supp, state_t *state) { // TODO
    char* vAddrMsg = (char*)state->reg_a1;
    int msgLen = (int)state->reg_a2;
    if (msgLen < 0 || msgLen > MAXSTRLENG) {
        
    }
    
    int printerLineN = 7;
    int devNo = supp->sup_asid - 1;
    // DEV_REG_ADDR(line, dev);
    dtpreg_t* devReg = (dtpreg_t*)DEV_REG_ADDR(printerLineN, devNo);
    // dtpreg_t* devReg = devregarea.devreg[3][devNo] // si può fare anche così?

    unsigned int status;
    (void) status; // TODO: usa sto status
    unsigned int value;
    int charSent = 0;
    while (charSent < msgLen) {
        value = PRINTCHR | (((unsigned int)*vAddrMsg) << 8);
        status = SYSCALL(DOIO, (int)&devReg->command, (int)value, 0);
        
        vAddrMsg++;
        charSent++;
    }

}

void generalExceptHandler()
{
    //print("~~~ generalExceptHandler ~~~\n");
    
    support_t* supp = (support_t*)SYSCALL(GETSUPPORTPTR, 0, 0, 0); 
    state_t* state = &(supp->sup_exceptState[GENERALEXCEPT]);

    switch(state->reg_a0) {
    case TERMINATE:
        SYSCALL(TERMPROCESS, 0, 0, 0);
        break;
    case WRITEPRINTER:
        break;
    case WRITETERMINAL:
        break;
    case READTERMINAL:
        break;
    default:
        supportTrapHandler();
        break;
    }
}
