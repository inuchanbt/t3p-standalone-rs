/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#define T3P_CAPTURE_PROBE 1
#include "../native/board.c"
static uint32_t now;
static unsigned sections, probe_reads;
static jmp_buf done;
uint32_t t3p_boot_parameters[4]={1,2,3,4};
struct t3p_boot_snapshot t3p_boot_snapshot={.valid=T3P_BOOT_MAGIC};
static uint32_t rd(void *ctx,uint32_t address) {
    (void)ctx;
    if(address==PA_BASE) return (now>=100 && now<160)||(now>=800 && now<2200) ? 0 : PA11;
    if(address>=0xfe0200 && address<=0xfe0224) { probe_reads++;return address; }
    return 0;
}
static void wr(void *ctx,uint32_t address,uint32_t value) {
    (void)ctx;(void)value;assert(address>=PA_BASE && address<=PA_BASE+0x20);
}
void t3p_bsp_native_io(struct t3p_bsp_io *io) { *io=(struct t3p_bsp_io){.read32=rd,.write32=wr}; }
int t3p_bsp_init(struct t3p_bsp *b,const struct t3p_bsp_io *io) { b->io=*io;return 0; }
int t3p_bsp_lcd_init(struct t3p_bsp *b) { (void)b;return 0; }
uint32_t t3p_bsp_millis(struct t3p_bsp *b) { (void)b;assert(now<3000);return now++; }
int t3p_bsp_delay(struct t3p_bsp *b,uint32_t ms) { (void)b;now+=ms;return 0; }
int t3p_bsp_rectangle(void *b,uint16_t x,uint16_t xe,uint16_t y,uint16_t ye,const uint16_t *p,uint32_t count) {
    (void)b;(void)x;(void)xe;(void)y;(void)ye;(void)p;assert(count==1920);now++;return 0;
}
void t3p_bsp_backlight(struct t3p_bsp *b,int enabled) { (void)b;assert(enabled); }
void meter_capture_probe_render(const uint32_t words[36],uint32_t section,uint32_t top,uint16_t pixels[1920]) {
    (void)pixels;assert(top<240 && section<4 && words[0]==1);sections|=1u<<section;
}
void meter_demo_sample(uint32_t tick) { (void)tick;assert(now>=1800 && probe_reads==7 && sections==3);longjmp(done,1); }
void meter_next_page(void) { abort(); }
uint32_t meter_double_press(void) { abort(); }
void meter_long_press(void) { abort(); }
void meter_snapshot(void) { abort(); }
void meter_demo_render(uint32_t top,uint16_t pixels[1920]) { (void)top;(void)pixels;abort(); }
void t3p_signal_failure(unsigned stage) { (void)stage;abort(); }
int main(void) { if(!setjmp(done))t3p_board_start();return 0; }
