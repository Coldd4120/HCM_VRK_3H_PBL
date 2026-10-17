#include "pbl_board.h"

void pbl_entry(void)
{
    pbl_board_init();
    (void)pbl_run(&pbl_board_port);
    for (;;) {
        __asm volatile ("wfi");
    }
}
