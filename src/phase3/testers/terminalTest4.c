/* Does nothing but outputs to the printer and terminates */

#include <uriscv/liburiscv.h>

#include "h/tconst.h"
#include "h/print.h"
#include "../../headers/const.h"
#include "../globals.h"

void main() {
	print(WRITETERMINAL, "printTest is ok\n");
	
	print(WRITETERMINAL, "Test number 4 is ok\n");
	
    SYSCALL(VERHOGEN, (int)&masterSemaphore, 0, 0);

	SYSCALL(TERMINATE, 0, 0, 0);
}
