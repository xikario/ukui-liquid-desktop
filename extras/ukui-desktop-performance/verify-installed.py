#!/usr/bin/python3
from pathlib import Path
import subprocess,time
unit='ukui-ftg340-responsiveness.service'
node=Path('/sys/class/devfreq/PHYT0048:00')
def call(*args):subprocess.run(args,check=True)
try:
    call('systemctl','stop',unit)
    assert int((node/'min_freq').read_text())==200000,'Stop did not restore original floor'
finally:
    call('systemctl','start',unit)
assert int((node/'min_freq').read_text())==600000,'Start did not restore AC policy'
call('udevadm','trigger','--action=change','/sys/class/power_supply/AC')
call('udevadm','settle','--timeout=5')
assert int((node/'min_freq').read_text())==600000
assert (node/'governor').read_text().strip()=='simple_ondemand'
assert (node/'polling_interval').read_text().strip()=='150'
print('PASS: real service stop restores 200000; start and AC udev event apply 600000; original governor and polling preserved')
