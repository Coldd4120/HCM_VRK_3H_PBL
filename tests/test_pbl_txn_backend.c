/* LH-R1-002/003: PBL TxnRecordV1 boot path (pbl_run_txn) wired over the journal
 * media and the board authenticator, exercised against a simulated interruptible
 * D-Flash. Joint PBL/C0 flow is seeded by hand-built records (the C0 writer side
 * is covered by the main repo's test_txn_fsm.c); here we check that the PBL glue
 * maps every txn_pbl outcome to the right port action and that a torn append
 * never bricks the boot. Built only with -DPBL_TXN_BACKEND. */
#include "pbl_core.h"
#include "txn_store.h"
#include "txn_pbl.h"
#include <stdio.h>
#include <string.h>

#define UNITS 2u
#define SLOTS 64u
typedef struct { uint8_t data[UNITS][SLOTS][TXN_RECORD_SIZE]; int fail_after; int prog_count; } Sim;
static void sim_init(Sim*s){ memset(s->data,0xFF,sizeof s->data); s->fail_after=-1; s->prog_count=0; }
static int sim_read(void*c,uint32_t u,uint32_t sl,uint8_t o[TXN_RECORD_SIZE]){ Sim*s=c; memcpy(o,s->data[u][sl],TXN_RECORD_SIZE); return 0; }
static int sim_program(void*c,uint32_t u,uint32_t sl,uint32_t off,const uint8_t*src,uint32_t len){ Sim*s=c; uint32_t i;
    for(i=0;i<len;i+=4u){ s->prog_count++; if(s->fail_after>=0&&s->prog_count>s->fail_after){return -1;}
        {uint32_t j; for(j=0;j<4u&&(i+j)<len;j++) s->data[u][sl][off+i+j]&=src[i+j];} } return 0; }
static int sim_erase(void*c,uint32_t u){ Sim*s=c; s->prog_count++; if(s->fail_after>=0&&s->prog_count>s->fail_after){return -1;} memset(s->data[u],0xFF,sizeof s->data[u]); return 0; }
static TxnMedia MK(Sim*s){ TxnMedia m; m.read=sim_read;m.program=sim_program;m.erase=sim_erase;m.ctx=s;m.slots_per_unit=SLOTS;m.units=UNITS; return m; }
static const TxnMedia*g_m;
static void dig(uint8_t o[TXN_DIGEST_LEN],uint8_t t){memset(o,t,TXN_DIGEST_LEN);}
static void mk(TxnRecordV1*r,uint8_t st,uint8_t wr,uint32_t seq,uint8_t cb,uint8_t tb,uint8_t ct,uint8_t tt,uint8_t att,uint8_t maxa){
    memset(r,0,sizeof *r);
    r->magic=TXN_MAGIC; r->schema=TXN_SCHEMA_V1; r->length=TXN_LENGTH_V1;
    r->sequence=seq; r->txn_generation=1; r->state=st; r->writer=wr;
    r->confirmed_bank=cb; r->target_bank=tb; r->attempts=att; r->max_attempts=maxa;
    r->confirmed_package_id=1; r->target_package_id=(ct==tt)?1:2; r->layout_id=PBL_LAYOUT_ID;
    r->confirmed_security_version=1; r->target_security_version=(ct==tt)?1:2;
    dig(r->confirmed_digest,ct); dig(r->target_digest,tt);
}

/* ---- mock board port ---- */
#define VEC  PBL_LAYOUT_C0_BASE            /* 0x442000, aligned to vector_align */
static uint32_t g_bank; static int g_verify_ok;
static uint32_t g_verified_package;
static int g_jumped, g_activated, g_reset, g_recovery; static uint32_t g_jump_vec;
static uint8_t g_allow[8]; static int g_allow_n;
static void allow_clr(void){g_allow_n=0;} static void allow(uint8_t a){g_allow[g_allow_n++]=a;}
static int board_auth(void*ctx,const uint8_t d[TXN_DIGEST_LEN]){(void)ctx;int i;for(i=0;i<g_allow_n;i++)if(d[0]==g_allow[i])return 1;return 0;}
static bool m_bank(void*c,uint32_t*b){(void)c;*b=g_bank;return true;}
static bool m_verify(void*c,uint32_t pkg,uint32_t*v){(void)c;g_verified_package=pkg;*v=VEC;return g_verify_ok?true:false;}
static bool m_read_u32(void*c,uint32_t a,uint32_t*v){(void)c; if(a==VEC)*v=0x20008000u; else if(a==VEC+4u)*v=(VEC+0x420u)|1u; else *v=0u; return true;}
static void m_jump(void*c,uint32_t v){(void)c;g_jumped=1;g_jump_vec=v;}
static bool m_activate(void*c){(void)c;g_activated=1;return true;}
static void m_reset(void*c){(void)c;g_reset=1;}
static void m_recovery(void*c){(void)c;g_recovery=1;}
static PblPort PORT(void){ PblPort p; memset(&p,0,sizeof p); p.ctx=NULL;
    p.physical_bank=m_bank; p.verify_active_package=m_verify; p.read_u32=m_read_u32;
    p.jump_c0=m_jump; p.activate_passive=m_activate; p.reset=m_reset; p.recovery=m_recovery; return p; }
static void reset_obs(void){g_jumped=g_activated=g_reset=g_recovery=0;g_jump_vec=0;g_verify_ok=1;}

static int failures=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);failures++;} }while(0)

int main(void){
    uint8_t A=0xA1,B=0xB2;
    /* 1) CONFIRMED on confirmed bank, auth+verify ok -> JUMP the confirmed vector */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_CONFIRMED,TXN_WR_FACTORY,0,0,0,A,A,0,3);
      CHECK(txn_store_append(&m,&r)==TXN_APPEND_OK,"seed confirmed");
      PblPort p=PORT(); reset_obs(); g_bank=0; allow_clr(); allow(A);
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_JUMP,"confirmed->jump");
      CHECK(g_jumped&&g_jump_vec==VEC&&!g_recovery,"confirmed jump vector"); }

    /* 2) CONFIRMED but the active image fails verification -> RECOVERY */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_CONFIRMED,TXN_WR_FACTORY,0,0,0,A,A,0,3);
      txn_store_append(&m,&r);
      PblPort p=PORT(); reset_obs(); g_bank=0; g_verify_ok=0; allow_clr(); allow(A);
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_RECOVERY,"verify fail->recovery");
      CHECK(g_recovery&&!g_jumped,"recovery observed"); }

    /* 3) SWAP_REQUESTED on target bank, auth ok -> PBL promotes to TRIAL, JUMP target */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_SWAP_REQUESTED,TXN_WR_C0,0,0,1,A,B,0,3);
      txn_store_append(&m,&r);
      PblPort p=PORT(); reset_obs(); g_bank=1; allow_clr(); allow(B);
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_JUMP,"swap->trial jump");
      CHECK(g_jumped&&!g_recovery,"trial jump observed");
      TxnLocation loc; CHECK(txn_store_load(&m,&loc)==TXN_LOAD_OK&&loc.record.state==TXN_ST_TRIAL,"committed TRIAL"); }

    /* 4) SWAP_REQUESTED on target bank, auth FAILS -> rollback request -> ACTIVATE+RESET */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_SWAP_REQUESTED,TXN_WR_C0,0,0,1,A,B,0,3);
      txn_store_append(&m,&r);
      PblPort p=PORT(); reset_obs(); g_bank=1; allow_clr(); /* no digest allowed */
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_RESET,"auth fail->reset");
      CHECK(g_activated&&g_reset&&!g_jumped,"activate+reset observed");
      TxnLocation loc; CHECK(txn_store_load(&m,&loc)==TXN_LOAD_OK&&loc.record.state==TXN_ST_ROLLBACK_REQUESTED,"committed ROLLBACK_REQUESTED"); }

    /* 5) Power-cut: a torn append (body written, commit lost) must not brick the
     *    boot; the PBL loads the last good CONFIRMED and boots it. */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r0; mk(&r0,TXN_ST_CONFIRMED,TXN_WR_FACTORY,0,0,0,A,A,0,3);
      CHECK(txn_store_append(&m,&r0)==TXN_APPEND_OK,"seed good");
      TxnRecordV1 r1; mk(&r1,TXN_ST_SWAP_REQUESTED,TXN_WR_C0,1,0,1,A,B,0,3);
      s.fail_after=s.prog_count+30; /* abort right before the commit word */
      (void)txn_store_append(&m,&r1);
      s.fail_after=-1;
      PblPort p=PORT(); reset_obs(); g_bank=0; allow_clr(); allow(A);
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_JUMP,"torn append still boots");
      CHECK(g_jumped&&!g_recovery,"booted last good confirmed"); }

    /* 6) Implausible physical bank -> RECOVERY */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_CONFIRMED,TXN_WR_FACTORY,0,0,0,A,A,0,3); txn_store_append(&m,&r);
      PblPort p=PORT(); reset_obs(); g_bank=2; allow_clr(); allow(A);
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_RECOVERY,"bad bank->recovery"); }

    /* 7) Blank journal -> RECOVERY */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m;
      PblPort p=PORT(); reset_obs(); g_bank=0; allow_clr();
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_RECOVERY,"blank->recovery"); }

    /* 8) LH-R2-007: a committed record built for a DIFFERENT layout must never
     *    drive a boot decision; the PBL rejects it and stays in recovery, with
     *    no jump/activate/reset and no journal write. */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_CONFIRMED,TXN_WR_FACTORY,0,0,0,A,A,0,3);
      r.layout_id=0xDEADBEEFu; /* valid CRC/commit, wrong layout */
      CHECK(txn_store_append(&m,&r)==TXN_APPEND_OK,"seed wrong-layout");
      TxnLocation before; txn_store_load(&m,&before);
      PblPort p=PORT(); reset_obs(); g_bank=0; allow_clr(); allow(A);
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_RECOVERY,"wrong layout_id->recovery");
      CHECK(g_recovery&&!g_jumped&&!g_activated&&!g_reset,"no boot/activate on wrong layout");
      TxnLocation after; txn_store_load(&m,&after);
      CHECK(before.record.sequence==after.record.sequence,"no journal write on wrong layout"); }

    /* FF02 after confirmation: run the real PBL glue, not just txn_pbl_run.
     * Arrival must verify/jump using the restored package id, not the stale
     * pre-finalization confirmed package in the original loaded record. */
    for (uint8_t source=0; source<2u; source++) {
      Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m;
      TxnRecordV1 r; mk(&r,TXN_ST_ROLLBACK_REQUESTED,TXN_WR_C0,0,
                       source,(uint8_t)(source^1u),B,A,0,3);
      r.result=TXN_RES_ACTIVITY_ROLLBACK;
      r.confirmed_package_id=22; r.target_package_id=11;
      r.confirmed_security_version=6; r.target_security_version=5;
      CHECK(txn_store_append(&m,&r)==TXN_APPEND_OK,"seed FF02 intent");
      PblPort p=PORT(); reset_obs(); g_bank=source; allow_clr(); allow(A);
      allow(B);
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_JUMP,"FF02 source waits for explicit ACK-authorized activation");
      CHECK(!g_activated&&!g_reset&&g_jumped&&g_verified_package==22,"source retains current package identity");
      reset_obs(); g_bank=source^1u; g_verified_package=0;
      CHECK(pbl_run_txn(&p,&m,board_auth)==PBL_RESULT_JUMP,"FF02 arrival boots baseline");
      CHECK(g_verified_package==11&&g_jumped&&!g_activated,"verify restored package identity");
      TxnLocation loc;
      CHECK(txn_store_load(&m,&loc)==TXN_LOAD_OK &&
            loc.record.confirmed_bank==g_bank&&loc.record.confirmed_package_id==11,
            "FF02 baseline persisted before handoff");
    }

    if(failures){ printf("PBL TXN BACKEND TESTS: %d FAIL\n",failures); return 1; }
    puts("PBL TXN BACKEND TESTS: ALL PASS"); return 0;
}
