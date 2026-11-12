/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Drive the real board loop through a 3-second hold, result notice, and
 * return to measurement. Storage/render bodies have separate integration tests. */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#define T3P_CAPTURE_DEVICE 1
#define T3P_MEASURE 1
#define T3P_PD_PROFILES 1
#define T3P_PD_CABLE_READ 1
#define T3P_PD_CHARGER 1
#include "../native/board.c"
static jmp_buf done;
static uint32_t clock_ms,notice_at;
static unsigned scenario,init_calls,usb_polls,prepares,saves,snapshots,frozen,notice_done,samples_after;
uint32_t t3p_capture_status,t3p_capture_jedec,t3p_capture_sfdp;
int32_t t3p_capture_result;
struct t3p_boot_snapshot t3p_boot_snapshot={.valid=T3P_BOOT_MAGIC};
static uint32_t rd(void *ctx,uint32_t at)
{(void)ctx;return at==PA_BASE&&clock_ms>=100&&clock_ms<3300?0:PA11;}
static void wr(void *ctx,uint32_t at,uint32_t v){(void)ctx;(void)at;(void)v;}
void t3p_bsp_native_io(struct t3p_bsp_io *io){*io=(struct t3p_bsp_io){.read32=rd,.write32=wr};}
int t3p_bsp_init(struct t3p_bsp *b,const struct t3p_bsp_io *io){b->io=*io;return 0;}
int t3p_bsp_lcd_init(struct t3p_bsp *b){(void)b;return 0;}
uint32_t t3p_bsp_millis(struct t3p_bsp *b){(void)b;assert(clock_ms<10000);return clock_ms;}
int t3p_bsp_delay(struct t3p_bsp *b,uint32_t ms){(void)b;clock_ms+=ms;return 0;}
int t3p_bsp_rectangle(void *b,uint16_t x,uint16_t xe,uint16_t y,uint16_t ye,const uint16_t *p,uint32_t n)
{(void)b;(void)x;(void)xe;(void)y;(void)ye;(void)p;assert(n==1920);clock_ms++;return 0;}
void t3p_bsp_backlight(struct t3p_bsp *b,int enabled){(void)b;assert(enabled);}
void t3p_signal_failure(unsigned stage){(void)stage;abort();}
int t3p_capture_store_init(struct t3p_bsp *b){assert(b==&board&&!prepares&&!saves);init_calls++;return scenario==1?-1:0;}
int t3p_capture_store_prepare(void){assert(!notice_done&&frozen==snapshots);prepares++;return scenario==4?-1:0;}
int t3p_capture_usb_start(struct t3p_bsp *b){(void)b;assert(init_calls==1&&!prepares&&!saves);return 0;}
void t3p_capture_usb_poll(void){usb_polls++;}
int32_t meter_capture_save(uint16_t p[1920])
{(void)p;assert(frozen==snapshots&&!notice_done&&prepares==1);saves++;return scenario==5?-2:1;}
void meter_capture_notice(const uint32_t w[4],uint32_t top,uint16_t p[1920])
{
    (void)p;
    if(!top){
        if(!(int32_t)w[0])frozen=snapshots;
        else {notice_done=1;notice_at=clock_ms;}
    }
}
void meter_snapshot(void)
{
    if(notice_done&&clock_ms-notice_at>=4000)longjmp(done,1);
    snapshots++;
}
void meter_demo_render(uint32_t top,uint16_t p[1920]){(void)top;(void)p;}
void meter_long_press(void){abort();}
void meter_next_page(void){abort();}
uint32_t meter_double_press(void){abort();}
void meter_live_sample(uint32_t at,uint32_t valid,uint32_t mv,int32_t ma)
{(void)at;assert(valid&&mv==5000&&ma==1000);if(notice_done)samples_after++;}
void meter_pd_status(uint32_t s,uint32_t cc,uint32_t mode){(void)s;(void)cc;(void)mode;}
void meter_pd_warning(uint32_t w){(void)w;}
void meter_charger_clear(void){abort();}
void meter_charger_status(uint32_t s){(void)s;}
void t3p_meter_gpio_init(struct t3p_bsp *b,struct t3p_meter_bus *bus){(void)b;*bus=(struct t3p_meter_bus){0};}
int t3p_meter_fast_bus(struct t3p_bsp *b,struct t3p_meter_bus *bus){(void)b;(void)bus;return 0;}
int t3p_meter_identify(struct t3p_meter_bus *bus){(void)bus;return 0;}
int t3p_meter_sample(struct t3p_meter_bus *bus,uint16_t *mv,int16_t *ma){(void)bus;*mv=5000;*ma=1000;return 0;}
void t3p_pd_poll_capture(struct t3p_pd_capture *pd,struct t3p_meter_bus *bus,uint32_t now)
{
    (void)bus;(void)now;
    if(scenario==2)pd->state=T3P_PD_LISTENING;
    if(scenario==3)charger_state=CHARGER_RESTORE;
}
void t3p_pd_start_capture(struct t3p_pd_capture *pd,struct t3p_meter_bus *bus,uint32_t now,enum t3p_pd_mode mode)
{(void)pd;(void)bus;(void)now;(void)mode;abort();}
void t3p_pd_read_cable(struct t3p_pd_capture *pd,struct t3p_meter_bus *bus,struct t3p_bsp *b)
{(void)pd;(void)bus;(void)b;abort();}
enum t3p_charger_state t3p_charger_arm(struct t3p_meter_bus *bus){(void)bus;abort();}
enum t3p_charger_state t3p_charger_cancel(struct t3p_meter_bus *bus,enum t3p_charger_state s){(void)bus;(void)s;abort();}
enum t3p_charger_state t3p_charger_scan(struct t3p_bsp *b,struct t3p_meter_bus *bus){(void)b;(void)bus;abort();}
int main(void)
{
    for(scenario=0;scenario<6;scenario++){
        clock_ms=notice_at=0;init_calls=usb_polls=prepares=saves=snapshots=frozen=notice_done=samples_after=0;
        t3p_capture_status=scenario==1?1:0;t3p_capture_result=0;capture_pending=0;charger_state=CHARGER_READY;
        if(!setjmp(done))t3p_board_start();
        assert(init_calls==1&&usb_polls>100&&samples_after>=30&&notice_done);
        assert(prepares==((scenario==0||scenario>=4)?1u:0u));
        assert(saves==((scenario==0||scenario==5)?1u:0u));
        assert(t3p_capture_result==(scenario==0?1:scenario==1||scenario==4?-1:scenario==5?-2:-3));
    }
    return 0;
}
