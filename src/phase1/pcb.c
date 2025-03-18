#include "./headers/pcb.h"

// MEMSET e MEMCPY
void *memset (void *dest, register int val, register unsigned int len);
void *memcpy(void *dest, const void *src, unsigned int len);

void *memset (void *dest, register int val, register unsigned int len)
{
  register unsigned char *ptr = (unsigned char*)dest;
  while (len-- > 0)
    *ptr++ = val;
  return dest;
}

void *memcpy(void *dest, const void *src, unsigned int len)
{
  char *d = dest;
  const char *s = src;
  while (len--)
    *d++ = *s++;
  return dest;
}
// ~ MEMSET e MEMCPY

static struct list_head pcbFree_h;
static pcb_t pcbFree_table[MAXPROC];
static int next_pid = 1;

void initPcbs()
{
  INIT_LIST_HEAD(&(pcbFree_h));
  for (int i = 0; i < MAXPROC; i++)
    list_add(&(pcbFree_table[i].p_list), &pcbFree_h);
}

void freePcb(pcb_t* p)
{
  if (p == NULL) return;
  list_add(&(p->p_list), &pcbFree_h);
}

pcb_t* allocPcb()
{
  if (list_empty(&pcbFree_h)) return NULL;

  // prende il primo elemento di pcbFree
  struct list_head *newPcb_list = list_next(&pcbFree_h);
  pcb_t *newPcb = container_of(newPcb_list, pcb_t, p_list);

  // inizializzazione del nuovo pcb
  list_del(newPcb_list); // stacca il pcb da pcbFree e lo reinizializza
  newPcb->p_parent = NULL;
  INIT_LIST_HEAD(&(newPcb->p_child));
  INIT_LIST_HEAD(&(newPcb->p_sib));
  newPcb->p_s = (state_t){0};
  newPcb->p_time = 0;
  newPcb->p_semAdd = NULL;
  newPcb->p_supportStruct = NULL;
  newPcb->p_pid = next_pid++;

  return newPcb;
}

void mkEmptyProcQ(struct list_head* head)
{
  INIT_LIST_HEAD(head);
}

int emptyProcQ(struct list_head* head)
{
  return (head != NULL && head->next == head);
}

void insertProcQ(struct list_head* head, pcb_t* p)
{
  if (head == NULL || p == NULL) return;
  list_add_tail(&(p->p_list), head);
}

pcb_t* headProcQ(struct list_head* head)
{
  if (head == NULL) return NULL;
  if (head->next == head) return NULL;
  return container_of(head->next, pcb_t, p_list);
}

pcb_t* removeProcQ(struct list_head* head)
{
  if (emptyProcQ(head)) return NULL;
  pcb_t *pcb_ptr = container_of(head->next, pcb_t, p_list);
  list_del(head->next);
  return pcb_ptr;
}

pcb_t* outProcQ(struct list_head* head, pcb_t* p)
{
  if (head == NULL || p == NULL) return NULL;
  struct list_head* iter;
  list_for_each(iter, head){
    if (iter == &(p->p_list)){
      list_del(&(p->p_list));
      return p;
    }
  }
  return NULL;
}

int emptyChild(pcb_t* p)
{
  return (p != NULL && list_empty(&(p->p_child)));
}

void insertChild(pcb_t *prnt, pcb_t *p)
{
    if (prnt == NULL || p == NULL) return;
    p->p_parent = prnt;
    if (emptyChild(prnt)) list_add(&(p->p_sib), &(prnt->p_child));
    else list_add_tail(&(p->p_sib), &(prnt->p_child));
}

pcb_t *removeChild(pcb_t *p)
{
    if (p == NULL || emptyChild(p)) return NULL;

    pcb_t *first_child = container_of(list_next(&(p->p_child)), pcb_t, p_sib);
    list_del(&(first_child->p_sib));
    first_child->p_parent = NULL;
    return first_child;
}

pcb_t *outChild(pcb_t *p)
{
    if (p == NULL || p->p_parent == NULL) return NULL;
    list_del(&(p->p_sib));
    p->p_parent = NULL;
    return p;
}
