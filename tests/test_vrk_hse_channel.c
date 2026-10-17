/* LH-R1-005 host test: HSE channel liveness latch (fail-closed on timeout). */
#include "vrk_hse_channel.h"
#include <stdio.h>
static int fail=0;
#define CHECK(c,m) do{ if(!(c)){printf("FAIL: %s\n",m);fail++;} }while(0)
int main(void){
    VrkHseChannel c; vrk_hse_ch_init(&c);
    CHECK(vrk_hse_ch_available(&c),"init ready");
    /* normal cycle */
    CHECK(vrk_hse_ch_begin(&c)==1,"begin ok");
    CHECK(!vrk_hse_ch_available(&c),"busy not available");
    CHECK(vrk_hse_ch_begin(&c)==0,"no reuse while busy");
    vrk_hse_ch_complete(&c);
    CHECK(vrk_hse_ch_available(&c),"complete -> ready");
    CHECK(vrk_hse_ch_begin(&c)==1,"begin again ok");
    /* timeout latches failed */
    vrk_hse_ch_fail(&c);
    CHECK(!vrk_hse_ch_available(&c),"failed not available");
    CHECK(vrk_hse_ch_begin(&c)==0,"begin refused after fail");
    vrk_hse_ch_complete(&c); /* must not clear a latched failure */
    CHECK(!vrk_hse_ch_available(&c),"complete does not clear fail");
    CHECK(vrk_hse_ch_begin(&c)==0,"still refused");
    /* only a reliable reset clears */
    vrk_hse_ch_reset(&c);
    CHECK(vrk_hse_ch_available(&c),"reset clears fail");
    CHECK(vrk_hse_ch_begin(&c)==1,"begin ok after reset");
    /* fail directly from ready */
    VrkHseChannel d; vrk_hse_ch_init(&d); vrk_hse_ch_fail(&d);
    CHECK(vrk_hse_ch_begin(&d)==0,"fail from ready latches");
    if(fail){printf("HSE CHANNEL TESTS: %d FAILURE(S)\n",fail);return 1;}
    printf("HSE CHANNEL TESTS: ALL PASS\n");return 0;
}
