/* Q16 (PBL side): PBL boot decision table over the TxnRecordV1 journal, driven
 * against a simulated interruptible D-Flash. Covers every PBL table cell plus the
 * W1..W7 power-cut points during the PBL commit; no C0 code is needed here (the
 * full C0+PBL lifecycle is exercised in the main repo's test_txn_fsm.c). */
#include "txn_store.h"
#include "txn_pbl.h"
#include <stdio.h>
#include <string.h>

#define UNITS 2u
#define SLOTS 64u
typedef struct { uint8_t data[UNITS][SLOTS][TXN_RECORD_SIZE]; int fail_after; int prog_count; int aborted; } Sim;
static void sim_init(Sim*s){ memset(s->data,0xFF,sizeof s->data); s->fail_after=-1; s->prog_count=0; s->aborted=0; }
static int sim_read(void*c,uint32_t u,uint32_t sl,uint8_t o[TXN_RECORD_SIZE]){ Sim*s=c; memcpy(o,s->data[u][sl],TXN_RECORD_SIZE); return 0; }
static int sim_program(void*c,uint32_t u,uint32_t sl,uint32_t off,const uint8_t*src,uint32_t len){ Sim*s=c; uint32_t i;
    for(i=0;i<len;i+=4u){ s->prog_count++; if(s->fail_after>=0&&s->prog_count>s->fail_after){s->aborted=1;return -1;}
        {uint32_t j; for(j=0;j<4u&&(i+j)<len;j++) s->data[u][sl][off+i+j]&=src[i+j];} } return 0; }
static int sim_erase(void*c,uint32_t u){ Sim*s=c; s->prog_count++; if(s->fail_after>=0&&s->prog_count>s->fail_after){memset(s->data[u],0,sizeof s->data[u]/2);s->aborted=1;return -1;} memset(s->data[u],0xFF,sizeof s->data[u]); return 0; }
static TxnMedia MK(Sim*s){ TxnMedia m; m.read=sim_read;m.program=sim_program;m.erase=sim_erase;m.ctx=s;m.slots_per_unit=SLOTS;m.units=UNITS; return m; }

static const TxnMedia*g_m;
static int commit_port(void*ctx,const TxnRecordV1*n){(void)ctx; return (txn_store_append(g_m,n)==TXN_APPEND_OK)?1:0;}
static int commit_fail(void*ctx,const TxnRecordV1*n){(void)ctx;(void)n; return 0;}
static uint8_t g_allow[8]; static int g_allow_n;
static void allow(uint8_t a){g_allow[g_allow_n++]=a;}
static void allow_clr(void){g_allow_n=0;}
static int auth_port(void*ctx,const uint8_t d[TXN_DIGEST_LEN]){(void)ctx;int i;for(i=0;i<g_allow_n;i++)if(d[0]==g_allow[i])return 1;return 0;}
static void dig(uint8_t o[TXN_DIGEST_LEN],uint8_t t){memset(o,t,TXN_DIGEST_LEN);}

static int failures=0;
#define CHECK(c,m) do{ if(!(c)){printf("FAIL: %s\n",m);failures++;} }while(0)

/* append a hand-built record straight to the journal */
static void seed(const TxnMedia*m,const TxnRecordV1*r){ CHECK(txn_store_append(m,r)==TXN_APPEND_OK,"seed"); }

static void mk(TxnRecordV1*r,uint8_t st,uint8_t wr,uint32_t seq,uint8_t cb,uint8_t tb,uint8_t ct,uint8_t tt,uint8_t att,uint8_t maxa){
    memset(r,0,sizeof *r);
    r->magic=TXN_MAGIC; r->schema=TXN_SCHEMA_V1; r->length=TXN_LENGTH_V1;
    r->sequence=seq; r->txn_generation=1; r->state=st; r->writer=wr;
    r->confirmed_bank=cb; r->target_bank=tb; r->attempts=att; r->max_attempts=maxa;
    r->confirmed_package_id=1; r->target_package_id=(ct==tt)?1:2; r->layout_id=0x1234;
    r->confirmed_security_version=1; r->target_security_version=(ct==tt)?1:2;
    dig(r->confirmed_digest,ct); dig(r->target_digest,tt);
}

int main(void){
    uint8_t A=0xA1,B=0xB2;
    TxnPblPorts pb;

    /* CONFIRMED on confirmed side, auth ok -> JUMP_CONFIRMED */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_CONFIRMED,TXN_WR_FACTORY,0,0,0,A,A,0,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A);
      CHECK(txn_pbl_run(&r,0,&pb)==TXN_PBL_JUMP_CONFIRMED,"CONFIRMED jump"); }

    /* CONFIRMED, auth fails -> RECOVERY */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_CONFIRMED,TXN_WR_FACTORY,0,0,0,A,A,0,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr();
      CHECK(txn_pbl_run(&r,0,&pb)==TXN_PBL_RECOVERY,"CONFIRMED auth fail recovery"); }

    /* CONFIRMED but wrong physical bank -> RECOVERY (unregistered swap) */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_CONFIRMED,TXN_WR_FACTORY,0,0,0,A,A,0,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A);
      CHECK(txn_pbl_run(&r,1,&pb)==TXN_PBL_RECOVERY,"CONFIRMED wrong bank recovery"); }

    /* PREPARED on confirmed side -> JUMP_CONFIRMED (no write) */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_PREPARED,TXN_WR_C0,0,0,1,A,B,0,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A);
      CHECK(txn_pbl_run(&r,0,&pb)==TXN_PBL_JUMP_CONFIRMED,"PREPARED confirmed jump");
      TxnLocation loc; txn_store_load(&m,&loc); CHECK(loc.record.sequence==0,"PREPARED no write"); }

    /* PREPARED but physical bank is target -> ROLLBACK_REQUESTED(7)+activate */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_PREPARED,TXN_WR_C0,0,0,1,A,B,0,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A); allow(B);
      CHECK(txn_pbl_run(&r,1,&pb)==TXN_PBL_ACTIVATE_RESET,"PREPARED target activate");
      TxnLocation loc; txn_store_load(&m,&loc); CHECK(loc.record.state==TXN_ST_ROLLBACK_REQUESTED&&loc.record.result==TXN_RES_LAYOUT_MISMATCH,"PREPARED target result7"); }

    /* SWAP_REQUESTED on target, auth ok -> TRIAL a1 + JUMP_TARGET */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_SWAP_REQUESTED,TXN_WR_C0,0,0,1,A,B,0,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A); allow(B);
      CHECK(txn_pbl_run(&r,1,&pb)==TXN_PBL_JUMP_TARGET,"SWAP target jump");
      TxnLocation loc; txn_store_load(&m,&loc); CHECK(loc.record.state==TXN_ST_TRIAL&&loc.record.attempts==1,"SWAP->trial a1"); }

    /* SWAP_REQUESTED on target, auth fail -> ROLLBACK(auth)+activate */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_SWAP_REQUESTED,TXN_WR_C0,0,0,1,A,B,0,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A);
      CHECK(txn_pbl_run(&r,1,&pb)==TXN_PBL_ACTIVATE_RESET,"SWAP auth-fail activate");
      TxnLocation loc; txn_store_load(&m,&loc); CHECK(loc.record.state==TXN_ST_ROLLBACK_REQUESTED&&loc.record.result==TXN_RES_AUTH_FAIL,"SWAP auth-fail result"); }

    /* TRIAL on target, below max -> attempts+1 + JUMP_TARGET */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_TRIAL,TXN_WR_PBL,0,0,1,A,B,1,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A); allow(B);
      CHECK(txn_pbl_run(&r,1,&pb)==TXN_PBL_JUMP_TARGET,"TRIAL inc jump");
      TxnLocation loc; txn_store_load(&m,&loc); CHECK(loc.record.attempts==2,"TRIAL a2"); }

    /* TRIAL on target, at max -> ROLLBACK(limit)+activate */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_TRIAL,TXN_WR_PBL,0,0,1,A,B,3,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A); allow(B);
      CHECK(txn_pbl_run(&r,1,&pb)==TXN_PBL_ACTIVATE_RESET,"TRIAL limit activate");
      TxnLocation loc; txn_store_load(&m,&loc); CHECK(loc.record.state==TXN_ST_ROLLBACK_REQUESTED&&loc.record.result==TXN_RES_TRIAL_LIMIT,"TRIAL limit result"); }

    /* TRIAL on confirmed side -> confirm old (CONFIRMED) + JUMP_CONFIRMED */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_TRIAL,TXN_WR_PBL,0,0,1,A,B,1,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A); allow(B);
      CHECK(txn_pbl_run(&r,0,&pb)==TXN_PBL_JUMP_CONFIRMED,"TRIAL confirmed-side jump");
      TxnLocation loc; txn_store_load(&m,&loc); CHECK(loc.record.state==TXN_ST_CONFIRMED&&loc.record.confirmed_digest[0]==A,"TRIAL confirm old"); }

    /* ROLLBACK_REQUESTED on confirmed side -> CONFIRMED old + jump */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_ROLLBACK_REQUESTED,TXN_WR_PBL,0,0,1,A,B,1,3); r.flags=2; seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A);
      CHECK(txn_pbl_run(&r,0,&pb)==TXN_PBL_JUMP_CONFIRMED,"ROLLBACK confirmed jump");
      TxnLocation loc; txn_store_load(&m,&loc); CHECK(loc.record.state==TXN_ST_CONFIRMED,"ROLLBACK->confirmed"); CHECK((loc.record.flags&0x0F)==0,"ROLLBACK->confirmed clears retry flags"); }

    /* ROLLBACK_REQUESTED on target, retry below cap -> flags+1 + activate */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_ROLLBACK_REQUESTED,TXN_WR_PBL,0,0,1,A,B,1,3); r.flags=0; seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A); allow(B);
      CHECK(txn_pbl_run(&r,1,&pb)==TXN_PBL_ACTIVATE_RESET,"ROLLBACK target retry activate");
      TxnLocation loc; txn_store_load(&m,&loc); CHECK((loc.record.flags&0x0F)==1,"ROLLBACK retry flags+1"); }

    /* ROLLBACK_REQUESTED on target, retry cap reached -> RECOVERY */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_ROLLBACK_REQUESTED,TXN_WR_PBL,0,0,1,A,B,1,3); r.flags=2; seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A); allow(B);
      CHECK(txn_pbl_run(&r,1,&pb)==TXN_PBL_RECOVERY,"ROLLBACK retry cap recovery"); }

    /* RECOVERY state -> RECOVERY */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_RECOVERY,TXN_WR_PBL,0,0,1,A,B,0,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr();
      CHECK(txn_pbl_run(&r,0,&pb)==TXN_PBL_RECOVERY,"RECOVERY stays recovery"); }

    /* commit failure converges to RECOVERY */
    { Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_SWAP_REQUESTED,TXN_WR_C0,0,0,1,A,B,0,3); seed(&m,&r);
      pb.auth=auth_port; pb.commit=commit_fail; pb.ctx=NULL; allow_clr(); allow(A); allow(B);
      CHECK(txn_pbl_run(&r,1,&pb)==TXN_PBL_RECOVERY,"commit fail recovery"); }

    /* W1..W7: power cut at each sub-op during the SWAP->TRIAL commit; journal must
     * stay loadable and consistent, and a retry must converge. */
    { int cut; for(cut=0;cut<30;cut++){
        Sim s; sim_init(&s); TxnMedia m=MK(&s); g_m=&m; TxnRecordV1 r; mk(&r,TXN_ST_SWAP_REQUESTED,TXN_WR_C0,0,0,1,A,B,0,3); seed(&m,&r);
        s.fail_after=cut; s.prog_count=0; s.aborted=0;
        pb.auth=auth_port; pb.commit=commit_port; pb.ctx=NULL; allow_clr(); allow(A); allow(B);
        txn_pbl_run(&r,1,&pb);
        TxnLocation loc; TxnLoadStatus st=txn_store_load(&m,&loc);
        CHECK(st==TXN_LOAD_OK,"pc loads");
        if(st==TXN_LOAD_OK){ uint8_t enc[TXN_RECORD_SIZE]; txn_encode(&loc.record,enc);
            CHECK(txn_record_valid(enc,NULL)==1,"pc valid");
            CHECK(loc.record.state==TXN_ST_SWAP_REQUESTED||loc.record.state==TXN_ST_TRIAL,"pc consistent state");
            s.fail_after=-1; TxnPblOutcome o=txn_pbl_run(&loc.record,1,&pb);
            CHECK(o==TXN_PBL_JUMP_TARGET||o==TXN_PBL_RECOVERY||o==TXN_PBL_ACTIVATE_RESET,"pc retry converges"); }
    } }

    if(failures){ printf("TXN PBL TESTS: %d FAILURE(S)\n",failures); return 1; }
    printf("TXN PBL TESTS: ALL PASS\n"); return 0;
}
