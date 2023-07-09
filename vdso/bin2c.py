#! /usr/bin/python3
import sys
import functools

MAGIC_NUMBER = 0x369C217132B8C1AB

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('Usage: bin2c.py /path/to/libvdso.so /path/to/vdso-image.c')
        exit(1)
    libvdso_path = sys.argv[1]
    vdso_image_path = sys.argv[2]
    with open(libvdso_path, 'rb') as f:
        libvdso_content = list(f.read())
    pos = None
    for i in range(0, len(libvdso_content) - 8, 4):
        if functools.reduce(lambda x, y: x * 256 + y, libvdso_content[i: i + 8][::-1], 0) == MAGIC_NUMBER:
            pos = i
            break
    if pos is None:
        print("Can't find magic number in input file")
        exit(1)
    with open(vdso_image_path, 'w') as w:
        vdso_data_1 = libvdso_content[:pos]
        vdso_data_2 = libvdso_content[pos + 8:]
        w.write("""
/* AUTOMATICALLY GENERATED -- DO NOT EDIT */

struct {{
    const unsigned char vdso_data_1[{}];
    void * __kernel_vsyscall_addr;
    const unsigned char vdso_data_2[{}];
}} vdso_image __attribute__((aligned(4096))) __attribute__((section(".data..ro_after_init"))) = {{
    {{
{}
    }},
        0,
    {{
{}
    }}
}};

extern long __kernel_vsyscall(long, long, long, long, long, long, long, long);
char* vdso_image_addr;

void uk_init_vdso(void) __attribute__((constructor));

void uk_init_vdso(void) {{
    vdso_image.__kernel_vsyscall_addr = &__kernel_vsyscall;
    vdso_image_addr = (char*)&vdso_image;
}}
"""
        .format(
            len(vdso_data_1),
            len(vdso_data_2),
            '\n'.join(['\t\t' + ' '.join(map(lambda x: "0x{:02X},".format(x), vdso_data_1[i: i + 10])) for i in range(0, len(vdso_data_1), 10)]),
            '\n'.join(['\t\t' + ' '.join(map(lambda x: "0x{:02X},".format(x), vdso_data_2[i: i + 10])) for i in range(0, len(vdso_data_2), 10)]),
        ))
