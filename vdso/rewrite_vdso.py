import os
import sys

KADDR_ukplat_monotonic_clock = None
KADDR_ukplat_wall_clock = None

segment_off = None
segment_vaddr = None
vdso_image_vaddr = None

last_line = ""
for line in sys.stdin.readlines():
    if 'flags r--' in line:
        if segment_off is not None:
            print('Error: Multi ro segment exists')
        segment_off = int(last_line.split()[2], 16)
        segment_vaddr = int(last_line.split()[4], 16)
    if 'vdso_image' in line:
        vdso_image_vaddr = int(line.split()[0], 16)
    if 'ukplat_monotonic_clock' in line:
        KADDR_ukplat_monotonic_clock = line.split()[0]
    if 'ukplat_wall_clock' in line:
        KADDR_ukplat_wall_clock = line.split()[0]
    last_line = line

assert segment_off is not None and segment_vaddr is not None
assert vdso_image_vaddr is not None
assert KADDR_ukplat_monotonic_clock is not None
assert KADDR_ukplat_wall_clock is not None

CONFIG_HZ = os.getenv('CONFIG_HZ')
assert CONFIG_HZ is not None

KERNEL_IMAGE_PATH = os.getenv('KERNEL_IMAGE_PATH')
assert KERNEL_IMAGE_PATH is not None

compile_args = ' '.join([
    '-Wl,-soname unikraft-vdso.so.1',
    '-shared',
    '-fPIC',
    '-O2',
    '-nostdlib',
    '-Wl,-T vdso.lds',
    '-DCONFIG_HZ={}'.format(CONFIG_HZ),
    '-DKADDR_ukplat_monotonic_clock=0x{}'.format(KADDR_ukplat_monotonic_clock),
    '-DKADDR_ukplat_wall_clock=0x{}'.format(KADDR_ukplat_wall_clock)
])

os.system('gcc *.c -o vdso.so {}'.format(compile_args))

with open('vdso.so', 'rb') as r:
    with open(KERNEL_IMAGE_PATH, 'rb+') as w:
        w.seek(vdso_image_vaddr - segment_vaddr + segment_off)
        w.write(r.read())

print('Rewrite success')
