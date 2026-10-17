#ifndef PBL_CORE_H
#define PBL_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define PBL_RECORD_MAGIC 0x50424C31u
#define PBL_RECORD_VERSION 1u
#define PBL_MAX_TRIAL_BOOTS 3u

typedef enum {
    PBL_STATE_CONFIRMED = 1u,
    PBL_STATE_ACTIVATING = 2u,
    PBL_STATE_TRIAL = 3u,
    PBL_STATE_ROLLING_BACK = 4u
} PblState;

/* Little endian persistent record. The storage port must append, read back,
 * and verify a complete record before returning success. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t state;
    uint32_t confirmed_bank;
    uint32_t target_bank;
    uint32_t trial_boots;
    uint32_t confirmed_package_id;
    uint32_t target_package_id;
    uint32_t crc32;
} PblRecord;

typedef struct {
    void *ctx;
    bool (*physical_bank)(void *ctx, uint32_t *bank);
    bool (*read_record)(void *ctx, uint32_t copy, PblRecord *record);
    bool (*append_record)(void *ctx, const PblRecord *record);
    bool (*verify_active_package)(void *ctx, uint32_t package_id,
                                  uint32_t *c0_vector);
    bool (*activate_passive)(void *ctx);
    void (*jump_c0)(void *ctx, uint32_t vector);
    void (*reset)(void *ctx);
    void (*recovery)(void *ctx);
} PblPort;

typedef enum {
    PBL_RESULT_JUMP = 1,
    PBL_RESULT_RESET = 2,
    PBL_RESULT_RECOVERY = 3
} PblResult;

uint32_t pbl_crc32(const void *data, uint32_t length);
void pbl_record_seal(PblRecord *record);
bool pbl_record_valid(const PblRecord *record);
bool pbl_select_record(const PblRecord *a, const PblRecord *b,
                       PblRecord *chosen);
PblResult pbl_run(const PblPort *port);

#endif
