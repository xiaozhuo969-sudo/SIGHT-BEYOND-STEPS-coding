"""Run pure-C controller regression tests with a native GCC (no hardware I/O)."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
sources={
    'gesture':['gesture'], 'gyro_calibration':['gyro_calibration'],
    'tx_control':['tx_control'], 'flight_keys':['flight_keys','tx_control'],
    'control_output':['control_output','gesture','remote_protocol','tx_control'],
    'remote_protocol':['remote_protocol'], 'hud_protocol':['../../shared/hud_protocol'],
    'hand_feedback':['tx_control'],
}
with tempfile.TemporaryDirectory(prefix='hand-tests-') as directory:
    for name, units in sources.items():
        output=Path(directory)/(name+'.exe' if os.name=='nt' else name)
        extra=[]
        if name=='hud_protocol':
            cjson=root/'managed_components/espressif__cjson/cJSON'
            extra=['-I',str(cjson),str(cjson/'cJSON.c')]
        subprocess.run([os.environ.get('CC','gcc'),'-std=c99','-Wall','-Wextra','-Werror',
            '-I',str(root/'main'),str(root/'tests'/f'test_{name}.c'),
            *[str(root/'main'/f'{unit}.c') for unit in units],*extra,'-lm','-o',str(output)],check=True)
        subprocess.run([str(output)],check=True)
print(f'{len(sources)} native test executables passed')
