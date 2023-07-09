compile_args="-Wl,--hash-style=both -Wl,-soname unikraft-vdso.so.1 -shared -fPIC -O2 -nostdlib -Wl,-T vdso.lds"
gcc vdso.c -c -o libvdso.o -fPIC -O2 -nostdlib
MAGIC_NUMBER=0x369C217132B8C1AB
objcopy --add-symbol __kernel_vsyscall=$MAGIC_NUMBER,global,function libvdso.o
ld libvdso.o -o libvdso.so --hash-style=both -soname unikraft-vdso.so.1 -shared -T vdso.lds
python3 bin2c.py libvdso.so vdso-image.c
