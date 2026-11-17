/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_temperature.h"
#ifdef T3P_TEMPERATURE_DIAGNOSTICS
struct t3p_temperature_diag t3p_temperature_diagnostics;
#define DIAG(code) do { if (d) { code; } } while (0)
/* USB page zero freezes the RAM cache. Pages 1..10 and the CRC trailer (11)
 * never access GPIO/I2C and do not refresh that frozen snapshot. */
int t3p_temperature_diag_page(uint32_t page, uint32_t out[10])
{
    static struct t3p_temperature_diag frozen;
    static unsigned valid;
    if (page>T3P_TEMP_DIAG_WORDS/10) return -1;
    if (!page) { frozen=t3p_temperature_diagnostics; valid=1; }
    if (!valid) return -1;
    const uint8_t *bytes=(const uint8_t *)&frozen;
    if (page==T3P_TEMP_DIAG_WORDS/10) {
        uint32_t crc=~0u;
        for (unsigned i=0;i<sizeof frozen;i++) {
            crc^=bytes[i];
            for (unsigned j=0;j<8;j++) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
        }
        for(unsigned i=0;i<10;i++) out[i]=0;
        out[0]=frozen.magic;out[1]=frozen.version;out[2]=T3P_TEMP_DIAG_WORDS;
        out[3]=frozen.sequence;out[4]=~crc;
    } else for (unsigned i=0;i<10;i++) {
        unsigned at=(page*10+i)*4;
        out[i]=(uint32_t)bytes[at]|(uint32_t)bytes[at+1]<<8|
               (uint32_t)bytes[at+2]<<16|(uint32_t)bytes[at+3]<<24;
    }
    return 0;
}
#else
#define DIAG(code) ((void)0)
#endif

/* BMP280 DS001-26 fixed-point compensation, with widened intermediates.
 * This is the pressure sensor's die temperature, not calibrated ambient. */
int t3p_temperature_compensate(const uint8_t c[6], uint32_t adc, int32_t *centi)
{
    unsigned t1 = (unsigned)c[0] | (unsigned)c[1] << 8;
    int16_t t2 = (int16_t)((unsigned)c[2] | (unsigned)c[3] << 8);
    int16_t t3 = (int16_t)((unsigned)c[4] | (unsigned)c[5] << 8);
    if (!centi || !t1 || t1 == 0xffff || adc > 0xfffff || adc == 0x80000) return -1;
    int64_t x = (int64_t)(adc >> 3) - 2 * (int64_t)t1;
    int64_t v1 = (x * t2) >> 11;
    x = (int64_t)(adc >> 4) - t1;
    int64_t v2 = (((x * x) >> 12) * t3) >> 14;
    int64_t temperature = ((v1 + v2) * 5 + 128) >> 8;
    if (temperature < -4000 || temperature > 8500) return -1;
    *centi = (int32_t)temperature;
    return 0;
}

void t3p_temperature_poll(struct t3p_temperature *t, struct t3p_meter_bus *bus,
                          struct t3p_system_stats *s, uint32_t now, unsigned enabled)
{
#ifdef T3P_TEMPERATURE_ENS220
    t3p_ens220_poll(t,bus,s,now,enabled);
#else
#ifdef T3P_TEMPERATURE_DIAGNOSTICS
    struct t3p_temperature_diag *d=bus->temperature_diag;
#endif
    if (!enabled) {
        if(t->pending) DIAG(d->result=TEMP_CANCELLED; ++d->sequence);
        s->temp_valid = 0; t->pending = 0; DIAG(d->temp_valid=0); return;
    }
    if (now - t->updated > 2500) { s->temp_valid = 0; DIAG(d->temp_valid=0); }
    if (!t->pending) {
        if (now - t->attempted < 1000) return;
        t->attempted = now;
        DIAG(t3p_system_count(&d->attempts); ++d->sequence;
             d->valid=0; d->result=TEMP_OK; d->phase=TEMP_ID;d->attempted_ms=now);
        uint8_t id, status;
        if (t3p_temperature_read(bus, 0xd0, &id, 1)) goto io_failed;
        DIAG(d->id=id; d->valid|=TEMP_VALID_ID);
        if (id!=0x58) { DIAG(d->result=TEMP_BAD_ID); goto failed; }
        DIAG(d->phase=TEMP_PRE_STATUS);
        if (t3p_temperature_read(bus,0xf3,&status,1)) goto io_failed;
        DIAG(d->status=status; d->valid|=TEMP_VALID_STATUS);
        if (status&9u) { DIAG(d->result=TEMP_BUSY); goto failed; }
        DIAG(d->phase=TEMP_CALIBRATION);
        if (t3p_temperature_read(bus,0x88,t->calibration,6)) goto io_failed;
        DIAG(d->cal0=(uint32_t)t->calibration[0]|(uint32_t)t->calibration[1]<<8|
                    (uint32_t)t->calibration[2]<<16|(uint32_t)t->calibration[3]<<24;
             d->cal1=(uint32_t)t->calibration[4]|(uint32_t)t->calibration[5]<<8;
             d->valid|=TEMP_VALID_CAL);
        unsigned t1 = (unsigned)t->calibration[0] | (unsigned)t->calibration[1] << 8;
        if (!t1 || t1 == 0xffff) { DIAG(d->result=TEMP_BAD_CALIBRATION); goto failed; }
        /* Temperature x1, pressure skipped, forced conversion. No reset. */
        DIAG(d->phase=TEMP_TRIGGER);
        if (t3p_temperature_write(bus, 0xf4, 0x21)) goto io_failed;
        DIAG(d->control=0x21; d->valid|=TEMP_VALID_CONTROL; d->phase=TEMP_WAIT);
        t->started = now;
        t->pending = 1;
        return;
    }
    if (now - t->started < 20) return;
    uint8_t status, raw[3];
    DIAG(d->phase=TEMP_POST_STATUS; d->valid&=~TEMP_VALID_STATUS);
    if (t3p_temperature_read(bus, 0xf3, &status, 1)) goto io_failed;
    DIAG(d->status=status; d->valid|=TEMP_VALID_STATUS);
    if (status & 1u) { DIAG(d->result=TEMP_BUSY); goto failed; }
    if (status & 8u) {
        if (now - t->started <= 200) { DIAG(d->phase=TEMP_WAIT); return; }
        DIAG(d->result=TEMP_TIMEOUT); goto failed;
    }
    DIAG(d->phase=TEMP_RAW);
    if (t3p_temperature_read(bus, 0xfa, raw, 3)) goto io_failed;
    uint32_t adc = (uint32_t)raw[0] << 12 | (uint32_t)raw[1] << 4 | raw[2] >> 4;
    DIAG(d->adc=adc; d->valid|=TEMP_VALID_RAW; d->phase=TEMP_COMPENSATE);
    if (t3p_temperature_compensate(t->calibration, adc, &s->temp_centi)) {
        DIAG(d->result=adc==0x80000?TEMP_SKIPPED:TEMP_RANGE); goto failed;
    }
    s->temp_valid = 1;
    t->updated = now;
    t->pending = 0;
    DIAG(t3p_system_count(&d->successes); d->phase=TEMP_DONE; ++d->sequence;
         d->temp_centi=s->temp_centi;d->temp_valid=1;d->updated_ms=now);
    return;
io_failed:
    DIAG(d->result=TEMP_IO);
failed:
    DIAG(t3p_system_count(&d->failures); ++d->sequence);
    s->temp_valid = 0;
    DIAG(d->temp_valid=0);
    t->pending = 0;
#endif
}
