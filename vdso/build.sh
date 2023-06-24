export CONFIG_HZ=100
export KERNEL_IMAGE_PATH=$PWD/../build/app-elfloader_qemu-x86_64.dbg

CONFIG_HZ=$CONFIG_HZ
if [ -z "$CONFIG_HZ" ]; then
    echo "Error: CONFIG_HZ environment variable is not set."
    exit 1
fi

KERNEL_IMAGE_PATH=$KERNEL_IMAGE_PATH
if [ -z "$KERNEL_IMAGE_PATH" ]; then
    echo "Error: KERNEL_IMAGE_PATH environment variable is not set."
    exit 1
fi

KADDR_ukplat_monotonic_clock=""
KADDR_ukplat_wall_clock=""

segment_off=""
segment_vaddr=""
vdso_image_vaddr=""

objdump_output=$(objdump -x "$KERNEL_IMAGE_PATH")

last_line=""
while IFS= read -r line; do
    if [[ $line == *"flags r--"* ]]; then
        if [ -n "$segment_off" ]; then
            echo "Error: Multi ro segment exists"
            exit 1
        fi
        segment_off=$(echo "$last_line" | awk '{print $3}' | tr -d '[:space:]')
        segment_vaddr=$(echo "$last_line" | awk '{print $5}' | tr -d '[:space:]')
    fi
    if [[ $line == *"vdso_image"* ]]; then
        vdso_image_vaddr=$(echo "$line" | awk '{print "0x" $1}' | tr -d '[:space:]')
    fi
    if [[ $line == *"ukplat_monotonic_clock"* ]]; then
        KADDR_ukplat_monotonic_clock=$(echo "$line" | awk '{print $1}' | tr -d '[:space:]')
    fi
    if [[ $line == *"ukplat_wall_clock"* ]]; then
        KADDR_ukplat_wall_clock=$(echo "$line" | awk '{print $1}' | tr -d '[:space:]')
    fi
    last_line="$line"
done <<< "$objdump_output"

if [ -z "$segment_off" ] || [ -z "$segment_vaddr" ]; then
    echo "Error: segment_off or segment_vaddr is not set."
    exit 1
fi

if [ -z "$vdso_image_vaddr" ]; then
    echo "Error: vdso_image_vaddr is not set."
    exit 1
fi

if [ -z "$KADDR_ukplat_monotonic_clock" ] || [ -z "$KADDR_ukplat_wall_clock" ]; then
    echo "Error: KADDR_ukplat_monotonic_clock or KADDR_ukplat_wall_clock is not set."
    exit 1
fi

compile_args="-Wl,-soname unikraft-vdso.so.1 -shared -fPIC -O2 -nostdlib -Wl,-T vdso.lds -DCONFIG_HZ=$CONFIG_HZ -DKADDR_ukplat_monotonic_clock=0x$KADDR_ukplat_monotonic_clock -DKADDR_ukplat_wall_clock=0x$KADDR_ukplat_wall_clock"

gcc *.c -o vdso.so $compile_args

vdso_size=$(stat -c%s vdso.so)
kernel_offset=$(( $vdso_image_vaddr - $segment_vaddr + $segment_off ))

dd if=vdso.so of="$KERNEL_IMAGE_PATH" bs=1 seek=$kernel_offset conv=notrunc

echo "Rewrite success"


strip -s --output-target elf32-i386 $PWD/../build/app-elfloader_qemu-x86_64.dbg -o $PWD/../build/app-elfloader_qemu-x86_64-vdso
