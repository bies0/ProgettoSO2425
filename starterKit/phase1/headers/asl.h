#ifndef ASL_H_INCLUDED
#define ASL_H_INCLUDED

#include "../../headers/listx.h"
#include "../../headers/types.h"

void initASL();
int insertBlocked(int* semAdd, pcb_t* p);
pcb_t* removeBlocked(int* semAdd);
pcb_t* outBlockedPid(int pid);
pcb_t* outBlocked(pcb_t* p);
pcb_t* headBlocked(int* semAdd);

#endif
