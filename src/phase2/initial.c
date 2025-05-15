#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "../headers/const.h"

#include "uriscv/arch.h"
#include "uriscv/cpu.h"

#include "./p2test.c" // Phase 2 test
#include "../phase3/initProc.c" // Phase 3 test

#include "./exceptions.c"
#include "./interrupts.c"
#include "./scheduler.c"
#include "./sysHandler.c"

// 1. Global variables declaration
int process_count;
struct list_head ready_queue;
struct pcb_t *current_process[NCPU];

// Our device_semaphores:
// DevNo:     01234567 | 01234567  | 01234567  | 01234567  | 01234567   | 01234567   | 0
// intLineNo: 0 - disk | 1 - flash | 2 - ether | 3 - print | 4 - term_o | 5 - term_i | pseudo-clock
int device_semaphores[NRSEMAPHORES];
const unsigned int PSEUDO_CLOCK_INDEX = NRSEMAPHORES-1;

volatile unsigned int global_lock;

extern void test(); // Phase 2 test entry point
extern void p3test(); // Phase 3 test entry point
extern void scheduler();
extern void exceptionHandler();

cpu_t current_process_start_time[NCPU]; // for each CPU it keeps the TOD of the last process that transitioned from ready to running on that CPU 

// End of declaration

int main()
{
    // 2. Pass Up Vectors population
    for (int i = 0; i < NCPU; i++) {
        passupvector_t v = {
            .tlb_refill_handler  = (memaddr)uTLB_RefillHandler,
            .tlb_refill_stackPtr = i == 0 ? KERNELSTACK : (RAMSTART + (64 * PAGESIZE) + (i * PAGESIZE)),
            .exception_handler   = (memaddr)exceptionHandler,
            .exception_stackPtr  = i == 0 ? KERNELSTACK : (0x20020000 + (i * PAGESIZE))
        };
        *((passupvector_t *)(PASSUPVECTOR + i*0x10)) = v;
    }

    // 3. Initialization
    initPcbs();
    initASL();

    // 4. Variables initialization
    process_count = 0;
    mkEmptyProcQ(&ready_queue);
    for (int i = 0; i < NCPU; i++) current_process[i] = NULL;
    for (int i = 0; i < NRSEMAPHORES; i++) device_semaphores[i] = 0;
    global_lock = 0;

    // 5. System-wide Interval Timer loading
    LDIT(PSECOND);

    // 6.  First PCB instantiation
    pcb_t *first_pcb = allocPcb();

    first_pcb->p_s = (state_t){
        .pc_epc = (memaddr)p3test, // Currently testing phase 3
        //.pc_epc = (memaddr)test, // Currently testing phase 2
        .mie = MIE_ALL,
        .status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M,
    }; 
    RAMTOP(first_pcb->p_s.reg_sp);
    // Process tree fields, p_time, p_semAdd and p_supportStruct are already set to NULL/initialized by allocPcb()

    insertProcQ(&ready_queue, first_pcb);
    process_count++;

    // 7. Interrupt routing - each IRT entry has the RP bit and the first 8 least significative bits set to 1
    unsigned int bits = 0;
    for (int cpu = 0; cpu < NCPU; cpu++)
        bits += (1 << cpu);
    for (int i = 0; i < IRT_NUM_ENTRY; i++) {
        memaddr entry = IRT_START + i*WS; 
        *((memaddr *)entry) = 0; // just to be sure that the entry is 0 before initializing it
        *((memaddr *)entry) |= IRT_RP_BIT_ON;
        *((memaddr *)entry) |= bits;
    }

    *((memaddr *)TPR) = 0;

    // 8. CPUs state setting
    state_t start_state = {
        .status = MSTATUS_MPP_M,
        .pc_epc = (memaddr)scheduler,
        .gpr = {0},
        .entry_hi = 0,
        .cause = 0,
        .mie = 0
    };

    for (int i = 1; i < NCPU; i++) {
        start_state.reg_sp = (0x20020000 + i * PAGESIZE);
        INITCPU(i, &start_state);
    }

    // 9. Scheduler calling
    scheduler();

    return 0;
}
