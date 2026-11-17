"""Read cached T3P temperature V1/V2 data. Never initiates I2C, saves or flash writes."""
import argparse
import json
from pathlib import Path
import struct
import zlib
from capture_export import CaptureReader

PRODUCT="T3P Temp Diag V1"
PROBE_PRODUCT="T3P Temp Probe V2"
ENS_PRODUCT="T3P ENS220 Temp V3"
NORMAL_PRODUCT="T3P Capture V6"
REVISED_NORMAL_PRODUCT="T3P Capture V6.1"
MAGIC=0x31444d54
WORDS=110
PHASES=("idle","ID","pre-status","calibration","trigger","wait","post-status","raw","compensate","done","configure","cleanup")
RESULTS=("OK","transport failure","unexpected ID","sensor busy/im_update","invalid calibration",
         "conversion timeout","skipped raw","compensation range","cancelled","configuration mismatch")
STAGES=("none","idle/release","initial START","write address ACK","register ACK",
        "repeated START","read address ACK","data/ACK","STOP","done")
ERRORS=("none","delay timer timeout","SCL did not rise","SDA held low","NACK","invalid arguments")
REGS=("IN","OUT","DIR","DIE","DIEH","PU0","PU1","PD0","PD1","HD0","HD1","SPL","CON","TIMER1_COUNT")


def label(names,value):
    return names[value] if value<len(names) else f"unknown ({value})"


def decode(data,trailer,address=0x77):
    f=struct.unpack(f"<{WORDS}I",data)
    t=struct.unpack("<10I",trailer)
    ens=f[1]==3 and f[39]==2
    if f[0]!=MAGIC or f[1] not in (1,3) or (f[1]==3 and not ens) or t[:4]!=(MAGIC,f[1],WORDS,f[2]) or any(t[5:]):
        raise ValueError("Unknown or mixed temperature snapshot")
    if zlib.crc32(data)!=t[4]:raise ValueError("Temperature snapshot CRC mismatch")
    if ens:address=0x20
    if (f[18]&~31 or f[28]&~31 or (f[39] if not ens else 0) or f[30] not in (0,1) or
        f[31:33] != ((0x40,0x20) if f[30]==0 else (4,2)) or
        f[33] not in (0,1) or f[34]!=f[30] or not 0<=address<128):
        raise ValueError("Unknown validity flags or bus routing")
    if ens and (f[30]!=1 or f[18]&4 or f[19]>0xffff or f[21]>0xffff or f[22]&~15 or
                f[23]&255>4 or (f[23]>>8)&255>=len(ERRORS) or
                (f[23]>>16)&255>=len(STAGES) or f[23]>>24>=len(ERRORS)):
        raise ValueError("Invalid ENS220 diagnostic fields")
    pins={}
    init="PA" if f[33]==0 else "PB"; active="PA" if f[34]==0 else "PB"
    for i,name in enumerate((init+"_before_init",init+"_after_init",active+"_released",active+"_failure_before_STOP",active+"_after_STOP")):
        if not f[28]&(1<<i):pins[name]=None;continue
        values=f[40+i*14:40+(i+1)*14]
        pa=(f[33] if i<2 else f[34])==0
        mask=0x60 if pa else 6
        pins[name]=dict(registers={k:f"{v:08X}" for k,v in zip(REGS,values)},
            masked={k:f"{v&mask:02X}" for k,v in zip(REGS[:-1],values)},
            scl=bool(values[0]&(0x40 if pa else 4)),
            sda=bool(values[0]&(0x20 if pa else 2)))
    temperature=f[35] if f[35]<0x80000000 else f[35]-0x100000000
    if f[36] not in (0,1) or (f[36] and not -4000<=temperature<=8500):
        raise ValueError("Invalid temperature validity/range")
    result=dict(format=f"T3P temperature diagnostic V{f[1]}",snapshot_crc32=f"{t[4]:08X}",
        sequence=f[2],attempts=f[3],successes=f[4],failures=f[5],
        gate=dict(mask=f[6],off_system=bool(f[6]&1),timer_unavailable=bool(f[6]&2),
                  PD_listening=bool(f[6]&4),charger_armed=bool(f[6]&8)),
        phase=label(PHASES,f[7]),result=label(RESULTS,f[8]),transactions=f[9],
        register=f"{f[10]:02X}" if f[9] else None,direction=("write" if f[11] else "read") if f[9] else None,
        stage=label(STAGES,f[12]),bus_error=label(ERRORS,f[13]),
        byte_index=f[14] if f[9] else None,bit_index=f[15] if f[9] else None,
        ACKs={name:(bool(f[16]&(1<<stage)) if f[17]&(1<<stage) else None)
              for stage,name in ((3,"write_address"),(4,"register"),(6,"read_address"),(7,"write_data"))},
        ID=f"{f[19]:0{4 if ens else 2}X}" if f[18]&1 else None,status=f"{f[20]:02X}" if f[18]&2 else None,
        raw_ADC=f[21] if f[18]&16 and not ens else None,
        calibration=(struct.pack("<II",f[22],f[23])[:6].hex() if f[18]&4 else None),
        control_written=f"{f[24]:02X}" if f[18]&8 else None,
        temperature_C=temperature/100 if f[36] else None,
        updated_ms=f[37] if f[4] else None,attempted_ms=f[38] if f[3] else None,
        timer=dict(ready=bool(f[25]),CON=f"{f[26]:08X}",COUNT=f[27]),
        STOP_error=label(ERRORS,f[29]),bus=("PA6 SCL / PA5 SDA" if f[30]==0 else "PB2 SCL / PB1 SDA")+
            f"; wire {address*2:02X}/{address*2+1:02X}; 7-bit {address:02X}",
        GPIO=pins,raw_words=[f"{w:08X}" for w in f],
        observation="Cached sequential register samples, not simultaneous logic-analyzer measurements")
    if ens:
        result.update(sensor="ENS220",raw_temperature_counts=f[21] if f[18]&16 else None,
            raw_temperature_unit="1/128 K",configuration_readback_mask=f"{f[22]:02X}",
            cleanup=dict(state=("not needed","required/deferred","idle verified","transport failure","mode mismatch")[f[23]&255],
                bus_error=label(ERRORS,(f[23]>>8)&255),stage=label(STAGES,(f[23]>>16)&255),STOP_error=label(ERRORS,f[23]>>24)))
    return result


class TemperatureReader(CaptureReader):
    metadata_ops=(0,2,3,4,5)
    def temperature(self):
        data=b"".join(self.exchange(4,page) for page in range(WORDS//10))
        return decode(data,self.exchange(4,WORDS//10))
    def probes(self):
        data=b"".join(self.exchange(5,page) for page in range(97))
        return decode_probes(data,self.exchange(5,97))


PROBE_TARGETS=((1,0x40,0xff,2),(1,0x22,0x01,1),(1,0x76,0xd0,1),(1,0x77,0xd0,1),
               (0,0x19,0x0f,1),(1,0x20,0x00,2),(0,0x76,0xd0,1),(0,0x77,0xd0,1))
PROBE_NAMES=("meter die ID","PD controller ID","BMP alternative address","BMP stock address",
             "accelerometer ID","ENS220 PART_ID","BMP alternative address","BMP stock address")


def decode_probes(data,trailer):
    f=struct.unpack("<970I",data);t=struct.unpack("<10I",trailer)
    if (f[:5]!=(0x32525054,2,970,120,8) or f[5]>8 or f[7]>1 or f[8]>1 or f[9]>1 or
        t[:4]!=(0x32525054,2,970,f[6]) or any(t[5:]) or zlib.crc32(data)!=t[4]):
        raise ValueError("Unknown, corrupt or mixed probe snapshot")
    records=[]
    for i in range(f[5]):
        at=10+i*120;m=f[at:at+10];d=f[at+10:at+120]
        if (m[:4]!=PROBE_TARGETS[i] or m[5]>1 or m[9]>2 or
            (m[0]==1 and m[9]!=0) or d[30]!=m[0] or d[33]!=m[0] or d[34]!=m[0]):
            raise ValueError("Unexpected probe target, GPIO bank or restoration state")
        trace=struct.pack("<110I",*d)
        tt=struct.pack("<10I",MAGIC,1,110,d[2],zlib.crc32(trace),0,0,0,0,0)
        transport=decode(trace,tt,m[1])
        result=m[4] if m[4]<0x80000000 else m[4]-0x100000000
        records.append(dict(index=i,target=PROBE_NAMES[i],port="PA" if m[0]==0 else "PB",
            address_7bit=f"{m[1]:02X}",register=f"{m[2]:02X}",bytes=m[3],result=result,
            value=f"{m[6]:0{m[3]*2}X}" if m[5] else None,started_ms=m[7],finished_ms=m[8],
            PA_restored=(None if m[9]==0 else m[9]==1),trace=transport))
        if i==5:
            records[-1]['part_ID_little_endian']=(f"{((m[6]&255)<<8)|(m[6]>>8):04X}" if m[5] else None)
    return dict(format="T3P finite ID probe V2",completed=f[5],total=8,generation=f[6],
        enabled=bool(f[7]),timer_ready=bool(f[8]),halted=bool(f[9]),snapshot_crc32=f"{t[4]:08X}",records=records)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output",type=Path,help="Optional JSON file; existing files are never replaced")
    p.add_argument("--summary",action="store_true",help="Print a short summary; --output still saves the complete diagnostic JSON")
    a=p.parse_args()
    if a.output and a.output.exists():raise FileExistsError(a.output)
    import hid
    devices=[d for d in hid.enumerate(0x483,0x5750) if d.get("product_string") in (PRODUCT,PROBE_PRODUCT,ENS_PRODUCT,NORMAL_PRODUCT,REVISED_NORMAL_PRODUCT)
             and d.get("usage_page")==0xff31 and d.get("usage")==1]
    if len(devices)!=1:raise SystemExit(f"Expected one T3P temperature diagnostic/probe device; found {len(devices)}")
    dev=hid.device()
    try:
        dev.open_path(devices[0]["path"]);r=TemperatureReader(dev)
        result=dict(device={k:devices[0].get(k) for k in ("product_string","usage_page","usage","interface_number")},
                    capture=r.info(),capture_diagnostics=r.diagnostics(),capture_timings=r.timings(),
                    temperature=r.temperature())
        if devices[0]["product_string"] in (PROBE_PRODUCT,ENS_PRODUCT):result['probes']=r.probes()
        text=json.dumps(result,indent=2)+"\n"
        if a.output:
            a.output.parent.mkdir(parents=True,exist_ok=True)
            with a.output.open("x",encoding="utf-8") as f:f.write(text)
        if a.summary:
            t=result['temperature']
            short=dict(device=result['device']['product_string'],temperature={k:t.get(k) for k in
                ('sensor','ID','temperature_C','successes','failures','phase','result','stage','bus_error',
                 'raw_temperature_counts','configuration_readback_mask','cleanup')})
            if 'probes' in result:
                q=result['probes'];short['probes']=dict(completed=q['completed'],halted=q['halted'],
                    results=[{k:r.get(k) for k in ('target','port','address_7bit','value','PA_restored')} for r in q['records']])
            if a.output:short['saved']=str(a.output)
            print(json.dumps(short,indent=2),flush=True)
        else:print(text,flush=True)
    finally:dev.close()


if __name__=="__main__":main()
