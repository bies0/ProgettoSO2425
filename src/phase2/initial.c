// TODO controllare errori nel file ASL.c (warning con i tipi)

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "../headers/const.h"
#include "uriscv/arch.h"
#include "../klog.c"

#include "./p2test.c"
#include "./exceptions.c"
#include "./scheduler.c"

// 1. Global variables declaration
#define GLOBALS 1
#include "globals.h"

int main()
{
    klog_print("Inizio Main | ");

    // 2. Pass Up Vectors population
    for (int i = 0; i < NCPU; i++) {
        passupvector_t v = {
            .tlb_refill_handler  = (memaddr)uTLB_RefillHandler,
            .tlb_refill_stackPtr = i == 0 ? KERNELSTACK : (0x200020000 + i*PAGESIZE),
            .exception_handler   = (memaddr)exceptionHandler,
            .exception_stackPtr  = i == 0 ? KERNELSTACK : (0x20020000 + i*PAGESIZE)
        };
        *((passupvector_t *)(PASSUPVECTOR + i*0x10)) = v;
    }
    klog_print("Passupvectors | ");

    // 3. Initialization
    initPcbs();
    initASL();
    klog_print("pcbs e asl | ");

    // 4. Variables initialization
    process_count = 0;
    mkEmptyProcQ(&ready_queue);
    for (int i = 0; i < NCPU; i++) current_process[i] = NULL;
    for (int i = 0; i < NRSEMAPHORES; i++) device_semaphores[i] = (semd_t){0};
    global_lock = 0;
    klog_print("variabili globali | ");

    // 5. System-wide Interval Timer loading
    LDIT(PSECOND);
    klog_print("timer | ");

    // 6.  First PCB instantiation
    pcb_t *first_pcb = allocPcb();
    if (first_pcb == NULL) return 1; // che si fa?

    first_pcb->p_s = (state_t){
        .mie = MIE_ALL,
        .status = MSTATUS_MIE_MASK | MSTATUS_MPP_M,
        .pc_epc = (memaddr)test
    };
    // Process tree fields, p_time, p_semAdd and p_supportStruct are already set to NULL/initialized by allocPcb()

    insertProcQ(&ready_queue, first_pcb);
    process_count++;
    klog_print("first pcb (manca SP?) | "); // TODO: manca SP set to RAMTOP

    // 7. Interrupt routing
    int cpu_counter = -1;
    for (int i = 0; i < IRT_NUM_ENTRY; i++) {
        if (i % IRT_NUM_ENTRY/NCPU == 0) cpu_counter++;
        //*((memaddr *)(IRT_START + i)) = (IRT_START + i) | IRT_RP_BIT_ON; // funziona allo stesso modo?
        *((memaddr *)(IRT_START + i*WS)) |= IRT_RP_BIT_ON;
        *((memaddr *)(IRT_START + i*WS)) |= (1 << cpu_counter);
    }
    *((memaddr *)TPR) = 0;
    klog_print("interrupt | ");

    // 8. CPUs state setting
    for (int i = 1; i < NCPU; i++) {
        pcb_t *pcb = allocPcb();
        if (pcb == NULL) return 1; // che si fa?

        pcb->p_s = (state_t){
            .status = MSTATUS_MPP_M,
            .pc_epc = (memaddr)scheduler,
            .reg_sp = (0x20020000 + i * PAGESIZE)
        };
    }
    // All the other fields have already been initialized in allocPcb()
    klog_print("CPUs setting | ");

    klog_print("Scheduleeeeer | "); // Chiama lo scheduler
    scheduler();

    klog_print("Fine Main | ");
    return 0;
}
