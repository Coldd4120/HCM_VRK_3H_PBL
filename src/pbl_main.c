#include "pbl_board.h"

void pbl_entry(void)
{
    pbl_board_init();
#ifdef PBL_TXN_BACKEND
    (void)pbl_run_txn(&pbl_board_port, pbl_board_txn_media(), pbl_board_txn_auth);
#else
    (void)pbl_run(&pbl_board_port);
#endif
    for (;;) {
        __asm volatile ("wfi");
    }
}
