/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Run the real board loop and ENS220 state machine with a synthetic 780 ms
 * LCD frame. This reproduces acquisition/repaint phase locking; it does not
 * model electrical timing or replace the on-device trial. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define T3P_CAPTURE_DEVICE 1
#define T3P_MEASURE 1
#define T3P_PD_PROFILES 1
#define T3P_PD_CABLE_READ 1
#define T3P_PD_CHARGER 1
#ifndef T3P_TEST_BOARD_SOURCE
#define T3P_TEST_BOARD_SOURCE "../native/board.c"
#endif
#include T3P_TEST_BOARD_SOURCE

static jmp_buf done;
static uint32_t clock_ms, triggered;
static unsigned scenario, snapshots, starts, sensor_ops, on_system, listening, running, tr;
static uint8_t regs[32];
struct t3p_boot_snapshot t3p_boot_snapshot={.valid=T3P_BOOT_MAGIC};
uint32_t t3p_capture_status,t3p_capture_jedec,t3p_capture_sfdp;
int32_t t3p_capture_result;

static int fast_delay(void *ctx){(void)ctx;return 0;}
static uint32_t rd(void *ctx,uint32_t at){(void)ctx;return at==PA_BASE?PA11:0;}
static void wr(void *ctx,uint32_t at,uint32_t value){(void)ctx;(void)at;(void)value;}
void t3p_bsp_native_io(struct t3p_bsp_io *io)
{*io=(struct t3p_bsp_io){.read32=rd,.write32=wr};}
int t3p_bsp_init(struct t3p_bsp *b,const struct t3p_bsp_io *io){b->io=*io;return 0;}
int t3p_bsp_lcd_init(struct t3p_bsp *b){(void)b;return 0;}
uint32_t t3p_bsp_millis(struct t3p_bsp *b){(void)b;assert(clock_ms<20000);return clock_ms;}
int t3p_bsp_delay(struct t3p_bsp *b,uint32_t ms){(void)b;clock_ms+=ms;return 0;}
int t3p_bsp_rectangle(void *b,uint16_t x,uint16_t xe,uint16_t y,uint16_t ye,
                      const uint16_t *p,uint32_t count)
{
    (void)b;(void)x;(void)xe;(void)y;(void)ye;(void)p;assert(count==1920);
    clock_ms+=26;
    /* Change gates inside a frame, after the outer loop has checked them. */
    if(scenario==1)on_system=!(clock_ms>=1800&&clock_ms<2500);
    if(scenario==3)charger_state=clock_ms>=1800&&clock_ms<2500?CHARGER_ARMED:CHARGER_READY;
    return 0;
}
void t3p_bsp_backlight(struct t3p_bsp *b,int enabled){(void)b;assert(enabled);}
void t3p_signal_failure(unsigned stage){(void)stage;abort();}
void t3p_capture_profile_begin(void){abort();}
void t3p_capture_profile_phase(uint32_t p){(void)p;abort();}
void t3p_capture_profile_end(int32_t r){(void)r;abort();}
int t3p_capture_store_init(struct t3p_bsp *b){(void)b;return 0;}
int t3p_capture_store_prepare(void){abort();}
int t3p_capture_usb_start(struct t3p_bsp *b){(void)b;return 0;}
void t3p_capture_usb_poll(void){}
int32_t meter_capture_save(uint16_t p[1920]){(void)p;abort();}
void meter_capture_notice(const uint32_t w[4],uint32_t top,uint16_t p[1920])
{(void)w;(void)top;(void)p;abort();}
uint32_t meter_on_system_page(void){return on_system;}
void meter_system_status(const struct t3p_system_stats *s){(void)s;}
void meter_snapshot(void)
{
    ++snapshots;
    if(t3p_temperature_diagnostics.attempts>=16||(scenario==4&&snapshots>=17))longjmp(done,1);
}
void meter_demo_render(uint32_t top,uint16_t p[1920]){(void)top;(void)p;}
void meter_long_press(void){abort();}
void meter_next_page(void){abort();}
uint32_t meter_double_press(void){abort();}
void meter_live_sample(uint32_t at,uint32_t valid,uint32_t mv,int32_t ma)
{(void)at;assert(valid&&mv==5000&&ma==1000);}
void meter_pd_status(uint32_t state,uint32_t cc,uint32_t mode){(void)state;(void)cc;(void)mode;}
void meter_pd_warning(uint32_t warning){(void)warning;}
void meter_charger_clear(void){abort();}
void meter_charger_status(uint32_t state){(void)state;}
void t3p_meter_gpio_init(struct t3p_bsp *b,struct t3p_meter_bus *bus)
{(void)b;*bus=(struct t3p_meter_bus){0};}
int t3p_meter_fast_bus(struct t3p_bsp *b,struct t3p_meter_bus *bus)
{(void)b;bus->delay=fast_delay;return scenario==4?-1:0;}
int t3p_meter_identify(struct t3p_meter_bus *bus){(void)bus;return 0;}
int t3p_meter_sample(struct t3p_meter_bus *bus,uint16_t *mv,int16_t *ma)
{(void)bus;*mv=5000;*ma=1000;return 0;}
void t3p_temperature_bus_init(struct t3p_bsp *b,struct t3p_meter_bus *bus)
{
    (void)b;t3p_temperature_diagnostics=(struct t3p_temperature_diag){.version=3,.sensor_kind=2};
    *bus=(struct t3p_meter_bus){.temperature_diag=&t3p_temperature_diagnostics};
}
static void sensor_transaction(struct t3p_meter_bus *bus,uint8_t reg,unsigned write)
{
    assert(on_system&&!listening&&charger_state!=CHARGER_ARMED&&scenario!=4);
    assert(bus->delay==fast_delay);
    /* Conversion completes with time, even while firmware is gated away.
     * Progress before a TEMP_OUT read so that draining old data clears TR. */
    if(running&&clock_ms-triggered>=5){tr=1;running=0;regs[6]&=~0x10u;}
    ++sensor_ops;
    struct t3p_temperature_diag *d=bus->temperature_diag;
    ++d->transactions;d->reg=reg;d->direction=write;d->stage=TEMP_BUS_DONE;
}
int t3p_ens220_read(struct t3p_meter_bus *bus,uint8_t reg,uint8_t *data,unsigned n)
{
    sensor_transaction(bus,reg,0);
    if(reg==0){assert(n==2);data[0]=0x21;data[1]=3;}
    else if(reg==0x1a){assert(n==2);data[0]=0x13;data[1]=0x95;tr=0;}
    else if(reg==0x14){assert(n==1);*data=(uint8_t)tr;}
    else {assert(n==1&&(reg==6||reg==8||reg==9));*data=regs[reg];}
    return 0;
}
int t3p_ens220_write(struct t3p_meter_bus *bus,uint8_t reg,uint8_t value)
{
    sensor_transaction(bus,reg,1);
    if(reg==6&&value==8){memset(regs,0,sizeof regs);running=tr=0;}
    else {regs[reg]=value;if(reg==6&&value==0x12){++starts;triggered=clock_ms;running=1;}}
    return 0;
}
void t3p_pd_poll_capture(struct t3p_pd_capture *pd,struct t3p_meter_bus *bus,uint32_t now)
{
    (void)bus;listening=scenario==2&&now>=1100&&now<1600;
    pd->state=listening?T3P_PD_LISTENING:T3P_PD_READY;
}
void t3p_pd_start_capture(struct t3p_pd_capture *pd,struct t3p_meter_bus *bus,
                          uint32_t now,enum t3p_pd_mode mode)
{(void)pd;(void)bus;(void)now;(void)mode;abort();}
void t3p_pd_read_cable(struct t3p_pd_capture *pd,struct t3p_meter_bus *bus,struct t3p_bsp *b)
{(void)pd;(void)bus;(void)b;abort();}
enum t3p_charger_state t3p_charger_arm(struct t3p_meter_bus *bus){(void)bus;abort();}
enum t3p_charger_state t3p_charger_cancel(struct t3p_meter_bus *bus,enum t3p_charger_state state)
{(void)bus;(void)state;abort();}
enum t3p_charger_state t3p_charger_scan(struct t3p_bsp *b,struct t3p_meter_bus *bus)
{(void)b;(void)bus;abort();}

int main(int argc,char **argv)
{
    unsigned baseline=argc==2&&!strcmp(argv[1],"--expect-starvation");
    for(scenario=0;scenario<(baseline?1u:5u);++scenario){
        clock_ms=triggered=snapshots=starts=sensor_ops=listening=running=tr=0;on_system=1;
        capture_pending=0;charger_state=CHARGER_READY;memset(regs,0,sizeof regs);
        if(!setjmp(done))t3p_board_start();
        struct t3p_temperature_diag *d=&t3p_temperature_diagnostics;
        printf("{\"scenario\":%u,\"attempts\":%u,\"successes\":%u,\"failures\":%u,"
               "\"transactions\":%u,\"phase\":%u,\"result\":%u,\"mask\":%u}\n",
               scenario,d->attempts,d->successes,d->failures,d->transactions,d->phase,d->result,d->cal0);
        if(baseline){
            assert(d->attempts==16&&d->successes==0&&d->failures==15&&d->transactions==60);
            assert(d->phase==TEMP_CONFIGURE&&d->result==TEMP_OK&&!d->cal0);return 0;
        }
        if(scenario==4)assert(!sensor_ops&&!d->attempts);
        else assert(d->successes>=13&&!d->failures&&system_stats.temp_centi==2500);
    }
    return 0;
}
