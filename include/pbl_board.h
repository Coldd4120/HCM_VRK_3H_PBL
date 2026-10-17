#ifndef PBL_BOARD_H
#define PBL_BOARD_H

#include "pbl_core.h"

/* Board binding selected at configure time (CMake PBL_BOARD). */
extern const PblPort pbl_board_port;
/* First call after the C runtime is up: safe outputs, watchdog. */
void pbl_board_init(void);

#endif
