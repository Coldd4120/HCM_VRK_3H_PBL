#include "pbl_core.h"

extern const PblPort pbl_board_port;

void pbl_entry(void)
{
    (void)pbl_run(&pbl_board_port);
    for (;;) {
        __asm volatile ("wfi");
    }
}
