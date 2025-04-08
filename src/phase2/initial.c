#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "../headers/const.h"
#include "../klog.c"

#include "uriscv/arch.h"
#include "uriscv/cpu.h"

#include "./p2test.c"
//#include "./p2testSyscall.c" // TODO togliere

#include "./exceptions.c"
#include "./interrupts.c"
#include "./scheduler.c"
#include "./sysHandler.c"

// 1. Global variables declaration
int process_count;
struct list_head ready_queue;
struct pcb_t *current_process[NCPU];

int device_semaphores[NRSEMAPHORES];
const unsigned int PSEUDO_CLOCK_INDEX = NRSEMAPHORES-1;

volatile unsigned int global_lock;

extern void test();
extern void scheduler();
extern void exceptionHandler();
extern void interruptHandler();

cpu_t current_process_start_time[NCPU];

// End of declaration

int main()
{
    // 2. Pass Up Vectors population
    for (int i = 0; i < NCPU; i++) {
        passupvector_t v = {
            .tlb_refill_handler  = (memaddr)uTLB_RefillHandler,
            .tlb_refill_stackPtr = i == 0 ? KERNELSTACK : (0x20020000 + i*PAGESIZE),
            .exception_handler   = (memaddr)exceptionHandler,
            .exception_stackPtr  = i == 0 ? KERNELSTACK : (0x20020000 + i*PAGESIZE)
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
        .pc_epc = (memaddr)test,
        .mie = MIE_ALL,
        .status = MSTATUS_MIE_MASK | MSTATUS_MPP_M,
    }; 
    RAMTOP(first_pcb->p_s.reg_sp);
    // Process tree fields, p_time, p_semAdd and p_supportStruct are already set to NULL/initialized by allocPcb()

    insertProcQ(&ready_queue, first_pcb);
    process_count++;
    //klog_print("first pcb | ");

    // 7. Interrupt routing
    int cpu_counter = -1;
    for (int i = 0; i < IRT_NUM_ENTRY; i++) {
        if (i % IRT_NUM_ENTRY/NCPU == 0) cpu_counter++;
        *((memaddr *)(IRT_START + i*WS)) |= IRT_RP_BIT_ON;
        *((memaddr *)(IRT_START + i*WS)) |= (1 << cpu_counter);
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
    //klog_print("CPUs setting | ");

    // 9. Scheduler calling
    //klog_print("Scheduleeeeer | ");
    scheduler();

    return 0;
}
