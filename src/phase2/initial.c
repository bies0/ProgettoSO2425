#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./p2test.c"
#include "./exceptions.c"
#include "../klog.c"
#include "uriscv/arch.h"
#include "./const.h"

// TODO: non funziona l'include "./const.h"
#define IRT_START 0x10000300
#define IRT_RP_BIT_ON (1 << 28)
#define IRT_NUM_ENTRY 48
#define TPR 0x10000408 

// ma siamo sicuro di queste due?
extern void exceptionHandler();
extern void test();

// 1. Global variables declaration
static int process_count;
static struct list_head ready_queue;
static struct pcb_t *current_process[NCPU];
static struct semd_t device_semaphores[NRSEMAPHORES];
static int global_lock;

// 2. Pass Up Vectors population
#define POPULATE_PASSUPVECTOR(ADDR, V) ((*((passupvector_t *)ADDR))) = (V)

int main()
{
    klog_print("Inizio Main | ");

    for (int i = 0; i < NCPU; i++) {
        passupvector_t v = {
            .tlb_refill_handler  = (memaddr)uTLB_RefillHandler,
            .tlb_refill_stackPtr = i == 0 ? KERNELSTACK : (0x20020000 + i*PAGESIZE),
            .exception_handler   = (memaddr)exceptionHandler,
            .exception_stackPtr  = i == 0 ? KERNELSTACK : (0x20020000 + i*PAGESIZE)
        };
        POPULATE_PASSUPVECTOR((PASSUPVECTOR + i*0x10), v);
    }
    klog_print("Passupvectors | ");

    // 3. Initialization
    initPcbs();
    initASL();
    klog_print("pcbs e asl | ");

    // 4. Variables initialization
    process_count = 0;
    INIT_LIST_HEAD(&ready_queue);
    for (int i = 0; i < NCPU; i++) current_process[i] = NULL;
    for (int i = 0; i < NRSEMAPHORES; i++) device_semaphores[i] = (semd_t){0};
    global_lock = 1;
    klog_print("variabili globali | ");

    // 5. System-wide Interval Timer loading
    LDIT(PSECOND);
    klog_print("timer | ");

    // 6.  First PCB instantiation
    pcb_t *pcb = allocPcb();
    if (pcb == NULL) return 1; // che si fa?

    pcb->p_s = (state_t){
        .mie = MIE_ALL,
        .status = MSTATUS_MIE_MASK | MSTATUS_MPP_M,
        .pc_epc = (memaddr)test
    };
    // Process tree fields, p_time, p_semAdd and p_supportStruct are already set to NULL/initialized by allocPcb()

    list_add(&(pcb->p_list), &ready_queue);
    process_count++;
    klog_print("first pcb (manca SP?) | "); // TODO: manca SP set to RAMTOP

    // 7. Interrupt routing
    int cpu_counter = -1;
    for (int i = 0; i < IRT_NUM_ENTRY; i++) {
        if (i % IRT_NUM_ENTRY/NCPU == 0) cpu_counter++;
        //*((memaddr *)(IRT_START + i)) = (IRT_START + i) | IRT_RP_BIT_ON;
        *((memaddr *)(IRT_START + i*WS)) |= IRT_RP_BIT_ON;
        *((memaddr *)(IRT_START + i*WS)) |= (1 << cpu_counter);
    }
    *((memaddr *)TPR) = 0;
    klog_print("interrupt | ");

    //print("Print eseguita correttamente!\n"); // Non funziona ancora
    klog_print("Fine Main");
    return 0;
}
