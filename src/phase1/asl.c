#include "./headers/asl.h"
#include "./headers/pcb.h"

static semd_t semd_table[MAXPROC];
static struct list_head semdFree_h;
static struct list_head semd_h;

// Inizializzazione delle liste 
void initASL() {
    INIT_LIST_HEAD(&semdFree_h);
    INIT_LIST_HEAD(&semd_h);
    for (int i = 0; i < MAXPROC; i++) {
        list_add_tail(&semd_table[i].s_link, &semdFree_h);
        INIT_LIST_HEAD(&semd_table[i].s_procq);
    }
}

// Inserisce il semaforo sem nella lista semd, mantenendo l'ordine crescente delle chiavi.
void add_to_semd(struct list_head *sem, int *key) {
    struct semd_t *pos;
    int cycle_ended = 1;
    list_for_each_entry(pos, &semd_h, s_link) {
        if (*pos->s_key > *key) {
            __list_add(sem, pos->s_link.prev, &pos->s_link);
            cycle_ended = 0;
            break;
        }
    }
    if (cycle_ended) {
        list_add_tail(sem, &semd_h);
    }
}

// Scorre la lista dei semafori attivi ritornando il semaforo con chiave key.
// Ritorna NULL se non lo trova.
semd_PTR search_key(int *key) {
	struct list_head *pos;
    semd_PTR c_sem;
	list_for_each(pos, &semd_h){
		c_sem = container_of(pos, semd_t, s_link);
		if (c_sem->s_key == key){
            return c_sem;
		}
	}
    return NULL;
}

// Cerca nella lista dei semafori attivi il semaforo con chiave semAdd.
// Se il semaforo non è attivo:
//  - Alloca un nuovo SEMD da semdFree.
//  - Inizializza i campi del SEMD.
// Inserisce PCB p nella coda s_procq del SEMD.
// Aggiorna p->p_semAdd con l'indirizzo semAdd.
// Ritorna TRUE se ci sono errori (es. nessun SEMD disponibile), altrimenti FALSE.
int insertBlocked(int* semAdd, pcb_t* p) {
    semd_PTR ptr_sem = search_key(semAdd);
    if (ptr_sem != NULL) {
        insertProcQ(&ptr_sem->s_procq, p);
        p->p_semAdd = semAdd;
        return 0;
    }
    if (list_empty(&semdFree_h)) return 1;

    struct list_head *ptr_list_head;
            
    ptr_list_head = list_next(&semdFree_h);
    list_del(ptr_list_head);

    add_to_semd(ptr_list_head, semAdd);

    ptr_sem = container_of(ptr_list_head, semd_t, s_link);
    ptr_sem->s_key = semAdd;
    INIT_LIST_HEAD(&ptr_sem->s_procq);

    list_add(&p->p_list, &ptr_sem->s_procq);
    p->p_semAdd = semAdd;

    return 0;
}

// Cerca nella lista dei semafori attivi il semaforo con chiave semAdd.
// Se non trova nulla, ritorna NULL,
// altrimenti rimuove il primo PCB dalla coda dei processi del semaforo trovato e ritorna un puntatore ad esso.
// Se la coda dei processi per questo semaforo diventa vuota, rimuovi il semaforo dalla ASL
// e ritornalo alla coda dei semafori liberi.
pcb_t* removeBlocked(int* semAdd) {
    semd_PTR ptr_sem = search_key(semAdd);
    if (ptr_sem == NULL) return NULL;
    
    pcb_PTR ptr_pcb = removeProcQ(&ptr_sem->s_procq);
    if (emptyProcQ(&ptr_sem->s_procq)) {
        list_del(&ptr_sem->s_link);
        list_add(&ptr_sem->s_link, &semdFree_h);
    }
    return ptr_pcb;
}

// Cerca nella lista dei processi attivi il semaforo associato a p.
// Scorre la lista dei processi nel semaforo e rimuove p.
// Nel caso in cui il semaforo o il processo non vengono trovati ritorna NULL.
pcb_t* outBlocked(pcb_t* p) {
    semd_PTR ptr_sem = search_key(p->p_semAdd);
    if (ptr_sem == NULL) {
        return NULL;
    }
    pcb_t *ptr_pcb;
    list_for_each_entry(ptr_pcb, &ptr_sem->s_procq, p_list) {
        if (ptr_pcb == p) {
            list_del(&ptr_pcb->p_list);
            if (emptyProcQ(&ptr_sem->s_procq)) {
                list_del(&ptr_sem->s_link);
                list_add(&ptr_sem->s_link, &semdFree_h);
            }
            return ptr_pcb;
        }
    }
    return NULL;
}

// Cerca il semaforo attivo con chiave semAdd, e ritorna il pcb in testa a s_procq.
// Nel caso in cui il semaforo non viene trovato o la lista dei processi è vuota ritorna NULL.
pcb_t* headBlocked(int* semAdd) {
    semd_PTR ptr_sem = search_key(semAdd);
    if (ptr_sem == NULL) {
        return NULL;
    }
    if (list_empty(&ptr_sem->s_procq)) {
        return NULL;
    }
    return headProcQ(&ptr_sem->s_procq);
}
