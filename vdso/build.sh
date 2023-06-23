export CONFIG_HZ=100
export KERNEL_IMAGE_PATH=$PWD/../build/app-elfloader_qemu-x86_64.dbg
objdump -x $KERNEL_IMAGE_PATH | python3 rewrite_vdso.py
strip -s --output-target elf32-i386 $PWD/../build/app-elfloader_qemu-x86_64.dbg -o $PWD/../build/app-elfloader_qemu-x86_64
