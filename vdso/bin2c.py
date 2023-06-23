import sys
import functools

MAGIC_NUMBER = 0x369C2171

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print('Usage: bin2c.py /path/to/vdso_mapping.conf /path/to/libvdso.so /path/to/vdso-image.c')
        exit(1)
    vdso_mapping_path = sys.argv[1]
    libvdso_path = sys.argv[2]
    vdso_image_path = sys.argv[3]

    vdso_mappings = []

    with open(vdso_mapping_path) as f:
        for line in f:
            line = line.strip().split()
            vdso_mappings.append((line[0], line[1]))
    with open(libvdso_path, 'rb') as f:
        libvdso_content = list(f.read())
    symbol_pos = []
    for i in range(0, len(libvdso_content) - 8, 4):
        if functools.reduce(lambda x, y: x * 256 + y, libvdso_content[i: i + 4][::-1], 0) == MAGIC_NUMBER:
            symbol_pos.append([i - 4, functools.reduce(lambda x, y: x * 256 + y, libvdso_content[i - 4: i][::-1], 0), None])
    assert(len(symbol_pos) % 2 == 0)
    symbol_pos = symbol_pos[:len(symbol_pos) // 2]
    if len(symbol_pos) != len(vdso_mappings):
        print("Error: Found {} magic numbers, and {} ones to map, count mismatch.".format(len(symbol_pos), len(vdso_mappings)))
        exit(1)
    symbol_pos.sort(key = lambda x: x[1])
    for i in range(len(symbol_pos)):
        symbol_pos[i][2] = i
    symbol_pos.sort(key = lambda x: x[0])
    with open(vdso_image_path, 'w') as w:
        generated_declaration = ""
        generated_assignment = ""
        last_offset = 0
        for idx, pos in enumerate(symbol_pos):
            func_id = pos[2]
            pos = pos[0]
            vdso_data = libvdso_content[last_offset:pos]
            generated_declaration += "\tconst unsigned char vdso_data_{}[{}];\n".format(idx, pos - last_offset)
            generated_assignment += "\t{{\n{}\n\t}},\n".format(
                '\n'.join(['\t\t' + ' '.join(map(lambda x: "0x{:02X},".format(x), vdso_data[i: i + 10]))
                           for i in range(0, len(vdso_data), 10)])
            )
            generated_declaration += "\tvoid *__vdso_addr_{};\n".format(vdso_mappings[func_id][0])
            generated_assignment += "\t\t0,\n"
            last_offset = pos + 8
        generated_declaration += "\tconst unsigned char vdso_data_{}[{}];\n".format(len(symbol_pos),
                                                                                    len(libvdso_content) - last_offset)
        vdso_data = libvdso_content[last_offset:]
        generated_assignment += "\t{{\n{}\n\t}}".format(
            '\n'.join(['\t\t' + ' '.join(map(lambda x: "0x{:02X},".format(x), vdso_data[i: i + 10]))
                       for i in range(0, len(vdso_data), 10)])
        )
        w.write("""
/* AUTOMATICALLY GENERATED -- DO NOT EDIT */

struct {{
{}
}} vdso_image = {{
{}
}};

char* vdso_image_addr;
void uk_init_vdso(void) __attribute__((constructor));

{}

void uk_init_vdso(void) {{
    vdso_image_addr = (char*)&vdso_image;
{}
}}
"""
        .format(
            generated_declaration,
            generated_assignment,
            '\n'.join(['extern void {}();'.format(i[1]) for i in vdso_mappings]),
            '\n'.join(['\tvdso_image.__vdso_addr_{} = (void*)((char*)&{} - vdso_image_addr);'.format(i[0], i[1]) for i in vdso_mappings]),
        ))
