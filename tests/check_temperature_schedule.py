"""Reproduce repaint starvation against an archived board and check its fix.

Uses the actual C board loop and ENS220 state machine, with synthetic LCD,
sensor and time boundaries. No physical USB/I2C device is accessed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parent.parent


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',required=True)
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/capture-v6-1')
    p.add_argument('--baseline-checkpoint',type=Path)
    a=p.parse_args();out=a.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    common=['-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'native'),
            '-DT3P_SYSTEM','-DT3P_TEMPERATURE_DIAGNOSTICS',
            '-DT3P_TEMPERATURE_ENS220','-DT3P_CAPTURE_ENS220']
    sources=[str(ROOT/name) for name in ('tests/board_temperature_schedule.c',
             'native/t3p_temperature.c','native/t3p_ens220.c')]
    def run(name,flags,arguments):
        exe=out/(name+'.exe')
        subprocess.run([a.cc,*common,*flags,*sources,'-o',str(exe)],cwd=ROOT,check=True)
        result=subprocess.run([str(exe),*arguments],capture_output=True,text=True)
        print(result.stdout,end='',flush=True)
        if result.returncode:
            print(result.stderr,end='',file=sys.stderr)
            result.check_returncode()
        rows=[json.loads(line) for line in result.stdout.splitlines()]
        return dict(rows=rows,executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
    baseline=None
    if a.baseline_checkpoint:
        checkpoint=a.baseline_checkpoint.resolve()
        with zipfile.ZipFile(checkpoint) as z:source=z.read('native/board.c')
        board=out/'board-before.c';board.write_bytes(source)
        baseline=run('scheduler-reproduced',[f'-DT3P_TEST_BOARD_SOURCE="{board.as_posix()}"'],
                     ['--expect-starvation'])
        baseline.update(checkpoint=str(checkpoint),
            checkpoint_sha256=hashlib.sha256(checkpoint.read_bytes()).hexdigest(),
            board_sha256=hashlib.sha256(source).hexdigest())
    fixed=run('scheduler-fixed',[],[])
    report=dict(format='Synthetic board-loop/time/sensor regression; not an electrical timing measurement',
        frame_ms=780,strip_ms=26,baseline=baseline,fixed=fixed,
        scenarios=['periodic repaint','SYSTEM exit within a frame','PD listening within a frame',
                   'CHARGER armed within a frame','fast timer unavailable'],
        unchanged_deadline_ms=200,
        current_board_sha256=hashlib.sha256((ROOT/'native/board.c').read_bytes()).hexdigest(),
        harness_sha256=hashlib.sha256((ROOT/'tests/board_temperature_schedule.c').read_bytes()).hexdigest())
    (out/'OFFLINE-temperature-schedule.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print('Archived starvation reproduced and corrected board scenarios passed.' if baseline else
          'Corrected board scenarios passed.')


if __name__=='__main__':main()
