#!/usr/bin/env python

"""Generate build and run scripts for Unikraft application.

Use KrafKit and Make for building. Use QEMU, Firecracker and Krafkit
for running.
"""

import sys
import os
import stat
import yaml

TEMPLATE_BUILD_MAKE = """#!/bin/sh

make distclean
UK_DEFCONFIG=$(pwd)/defconfigs/{} make defconfig
make prepare
make -j $(nproc)
"""

TEMPLATE_BUILD_KRAFT_TARGET = """#!/bin/sh

rm -fr .unikraft
rm -f .config.*
kraft build --log-level debug --log-type basic --target {} --plat {}
"""

TEMPLATE_BUILD_KRAFT_ARCH = """#!/bin/sh

rm -fr .unikraft
rm -f .config.*
kraft build --log-level debug --log-type basic --arch {} --plat {}
"""

TEMPLATE_RUN_QEMU_HEADER = """#!/bin/sh

kernel="{}"
cmd="{}"

if test $# -eq 1; then
    kernel="$1"
fi
"""

RUN_COMMON_NET_COMMANDS = """
# Remove previously created network interfaces.
sudo ip link set dev tap0 down 2> /dev/null
sudo ip link del dev tap0 2> /dev/null
sudo ip link set dev virbr0 down 2> /dev/null
sudo ip link del dev virbr0 2> /dev/null
"""

RUN_QEMU_NET_COMMANDS = """
# Create bridge interface for QEMU networking.
sudo ip link add dev virbr0 type bridge
sudo ip address add 172.44.0.1/24 dev virbr0
sudo ip link set dev virbr0 up
"""

RUN_FIRECRACKER_NET_COMMANDS = """
# Create tap interface for Firecracker networking.
sudo ip tuntap add dev tap0 mode tap
sudo ip address add 172.44.0.1/24 dev tap0
sudo ip link set dev tap0 up
"""

RUN_KRAFT_NET_COMMANDS = """
# Create bridge interface for KraftKit networking.
sudo kraft net create -n 172.44.0.1/24 virbr0
"""

TEMPLATE_RUN_FIRECRACKER_HEADER = """#!/bin/sh

config="{}"

if test $# -eq 1; then
    config="$1"
fi
"""

RUN_FIRECRACKER_PRE_TRAILER = """
# Remove previously created files.
sudo rm -f /tmp/firecracker.log
> /tmp/firecracker.log
sudo rm -f /tmp/firecracker.socket
"""

RUN_FIRECRACKER_COMMAND = """firecracker-x86_64 \\
        --api-sock /tmp/firecracker.socket \\
        --config-file "$config"
"""

TEMPLATE_RUN_KRAFT_HEADER = """#!/bin/sh

cmd="{}"
"""

RUN_CPIO_COMMANDS = """
# Create CPIO archive to be used as the initrd.
old="$PWD"
cd "$rootfs"
find -depth -print | tac | bsdcpio -o --format newc > "$old"/rootfs.cpio
cd "$old"
"""

RUN_KILL_COMMANDS = """
# Clean up any previous instances.
sudo pkill -f qemu-system
sudo pkill -f firecracker
sudo kraft stop --all
sudo kraft rm --all
"""

APPNAME = ""
DEFCONFIGS = "defconfigs"
SCRIPTS = "scripts"
BUILD = os.path.join(SCRIPTS, "build")
RUN = os.path.join(SCRIPTS, "run")
KRAFTCONFIG = "kraft.yaml"

def files(path):
    """Extract regular files in given directory.

    Ignore directories or other file types.
    """

    for file in os.listdir(path):
        if os.path.isfile(os.path.join(path, file)):
            yield file

def generate_build_make(target):
    """Generate build scripts using Make.

    Scripts are generated in scripts/build/ directory and start
    with the `make-` prefix.
    """
    contents = TEMPLATE_BUILD_MAKE.format(target)
    out_file = os.path.join(BUILD, f"make-{target}.sh")
    with open(out_file, "w", encoding="utf8") as stream:
        stream.write(contents)
    st = os.stat(out_file)
    os.chmod(out_file, st.st_mode | stat.S_IEXEC)

def generate_build_kraft(name, arch, plat):
    """Generate build scripts using Kraftkit.

    Scripts are generated in scripts/build/ directory and start
    with the `kraft-` prefix.
    """

    if name:
        contents = TEMPLATE_BUILD_KRAFT_TARGET.format(name, plat)
        suffix = name.replace(APPNAME+"-", "")
    else:
        contents = TEMPLATE_BUILD_KRAFT_ARCH.format(arch, plat)
        suffix = f"{plat}-{arch}"
    out_file = os.path.join(BUILD, f"kraft-{suffix}.sh")
    with open(out_file, "w", encoding="utf8") as stream:
        stream.write(contents)
    st = os.stat(out_file)
    os.chmod(out_file, st.st_mode | stat.S_IEXEC)

def generate_build():
    """Generate build scripts.

    Scripts are generated in scripts/build/ directory.
    """

    if not os.path.exists(BUILD):
        os.mkdir(BUILD)

    # Generate make-based build scripts from defconfigs.
    for file in files(DEFCONFIGS):
        generate_build_make(file)

    # Generate KraftKit-based build scripts from kraft.yaml targets.
    with open(KRAFTCONFIG, "r", encoding="utf8") as stream:
        data = yaml.safe_load(stream)
        if 'targets' not in data.keys():
            printf(f"No targets in {KRAFTCONFIG}.")
            return
        for target in data['targets']:
            if 'platform' not in target.keys():
                printf(f"No platform found.")
                continue
            if 'architecture' not in target.keys():
                printf(f"No architecture found.")
                continue
            if 'name' in target.keys():
                name = target['name']
            else:
                name = None
            generate_build_kraft(name, target['architecture'], target['platform'])

def generate_run_fc(run, arch, fs):
    """Generate running scripts using Firecracker.

    Scripts are generated in scripts/run/ directory and start
    with the `fc-` prefix. The corresponding JSON file is generated.
    """

    if fs:
        if 'name' in run.keys():
            json_name = os.path.join(RUN, f"fc-{arch}-{fs}-{run['name']}.json")
        else:
            json_name = os.path.join(RUN, f"fc-{arch}-{fs}.json")
    else:
        if 'name' in run.keys():
            json_name = os.path.join(RUN, f"fc-{arch}-{run['name']}.json")
        else:
            json_name = os.path.join(RUN, f"fc-{arch}.json")
    if 'command' in run.keys():
        command = run["command"]
    else:
        command = "placeholder"
    with open(json_name, "w", encoding="utf8") as stream:
        stream.write("{\n")
        stream.write("  \"boot-source\": {\n")
        stream.write(f"    \"kernel_image_path\": \"workdir/build/{APPNAME}_fc-{arch}\",\n")
        if run["networking"]:
            stream.write(f"    \"boot_args\": \"{APPNAME}_fc-{arch} ")
            stream.write("netdev.ipv4_addr=172.44.0.2 netdev.ipv4_gw_addr=172.44.0.1 ")
            stream.write(f"netdev.ipv4_subnet_mask=255.255.255.0 -- {command}\"")
        else:
            stream.write(f"    \"boot_args\": \"{APPNAME}_fc-{arch} {command}\"")
        if "rootfs" in run.keys():
            stream.write(",\n    \"initrd_path\": \"rootfs.cpio\"")
        stream.write("\n  },\n")
        stream.write(f"""  "drives": [],
  "machine-config": {{
    "vcpu_count": 1,
    "mem_size_mib": {run['memory']},
    "smt": false,
    "track_dirty_pages": false
  }},
  "cpu-config": null,
  "balloon": null,
""")
        if run["networking"]:
            stream.write("""  "network-interfaces": [
    {
      "iface_id": "net1",
      "guest_mac":  "06:00:ac:10:00:02",
      "host_dev_name": "tap0"
    }
  ],
""")
        stream.write("""  "vsock": null,
  "logger": {
    "log_path": "/tmp/firecracker.log",
    "level": "Debug",
    "show_level": true,
    "show_log_origin": true
  },
  "metrics": null,
  "mmds-config": null,
  "entropy": null
}
""")

    header = TEMPLATE_RUN_FIRECRACKER_HEADER.format(json_name)
    if fs:
        if 'name' in run.keys():
            out_file = os.path.join(RUN, f"fc-{arch}-{fs}-{run['name']}.sh")
        else:
            out_file = os.path.join(RUN, f"fc-{arch}-{fs}.sh")
    else:
        if 'name' in run.keys():
            out_file = os.path.join(RUN, f"fc-{arch}-{run['name']}.sh")
        else:
            out_file = os.path.join(RUN, f"fc-{arch}.sh")
    with open(out_file, "w", encoding="utf8") as stream:
        stream.write(header)
        if "rootfs" in run.keys():
            stream.write(f"\nrootfs=\"{run['rootfs']}\"\n")
        stream.write(RUN_KILL_COMMANDS)
        if "rootfs" in run.keys() and fs == "initrd":
            stream.write(RUN_CPIO_COMMANDS)
        if run["networking"]:
            stream.write(RUN_COMMON_NET_COMMANDS)
            stream.write(RUN_FIRECRACKER_NET_COMMANDS)
        stream.write(RUN_FIRECRACKER_PRE_TRAILER)
        if run["networking"]:
            stream.write("sudo ")
        stream.write(RUN_FIRECRACKER_COMMAND)
        st = os.stat(out_file)
        os.chmod(out_file, st.st_mode | stat.S_IEXEC)

def generate_run_qemu(run, arch, fs):
    """Generate running scripts using Firecracker.

    Scripts are generated in scripts/run/ directory and start
    with the `qemu-` prefix.
    """

    kernel = os.path.join(os.path.join("workdir", "build"), f"{APPNAME}_qemu-{arch}")
    if 'command' in run.keys():
        header = TEMPLATE_RUN_QEMU_HEADER.format(kernel, run["command"])
    else:
        header = TEMPLATE_RUN_QEMU_HEADER.format(kernel, "placeholder")
    if fs:
        if 'name' in run.keys():
            out_file = os.path.join(RUN, f"qemu-{arch}-{fs}-{run['name']}.sh")
        else:
            out_file = os.path.join(RUN, f"qemu-{arch}-{fs}.sh")
    else:
        if 'name' in run.keys():
            out_file = os.path.join(RUN, f"qemu-{arch}-{run['name']}.sh")
        else:
            out_file = os.path.join(RUN, f"qemu-{arch}.sh")
    with open(out_file, "w", encoding="utf8") as stream:
        stream.write(header)
        if "rootfs" in run.keys():
            stream.write(f"\nrootfs=\"{run['rootfs']}\"\n")
        stream.write(RUN_KILL_COMMANDS)
        if "rootfs" in run.keys() and fs == "initrd":
            stream.write(RUN_CPIO_COMMANDS)
        if run["networking"]:
            stream.write(RUN_COMMON_NET_COMMANDS)
            stream.write(RUN_QEMU_NET_COMMANDS)
        stream.write("\n")
        if run["networking"]:
            stream.write("sudo ")
        if arch == "x86_64":
            stream.write("qemu-system-x86_64 \\\n")
            if 'accel' in run.keys():
                if run['accel']:
                    stream.write("    -accel kvm \\\n")
        else:
            stream.write("qemu-system-aarch64 \\\n")
            stream.write("    -machine virt \\\n")
        stream.write("    -kernel \"$kernel\" \\\n")
        stream.write("    -nographic \\\n")
        stream.write(f"    -m {run['memory']}M \\\n")
        if run["networking"]:
            stream.write("    -netdev bridge,id=en0,br=virbr0 ")
            stream.write("-device virtio-net-pci,netdev=en0 \\\n")
            stream.write("    -append \"netdev.ipv4_addr=172.44.0.2 ")
            stream.write("netdev.ipv4_gw_addr=172.44.0.1 ")
            stream.write("netdev.ipv4_subnet_mask=255.255.255.0 -- $cmd\" \\\n")
        else:
            stream.write("    -append \"$cmd\" \\\n")
        if "rootfs" in run.keys():
            if fs == "initrd":
                stream.write("    -initrd \"$PWD\"/rootfs.cpio \\\n")
            if fs == "9pfs":
                stream.write("    -fsdev local,id=myid,path=\"$rootfs\",security_model=none \\\n")
                stream.write("    -device virtio-9p-pci,fsdev=myid,mount_tag=fs1,")
                stream.write("disable-modern=on,disable-legacy=off \\\n")
        stream.write("    -cpu max\n")
        st = os.stat(out_file)
        os.chmod(out_file, st.st_mode | stat.S_IEXEC)

def generate_run_kraft(run, name, arch, plat, fs):
    """Generate running scripts using KraftKit.

    Scripts are generated in scripts/run/ directory and start
    with the `kraft-` prefix.
    """

    if plat == "firecracker":
        plat = "fc"
    if name:
        suffix = name.replace(APPNAME+"-", "")
    else:
        suffix = f"{plat}-{arch}"
    if 'name' in run.keys():
        out_file = os.path.join(RUN, f"kraft-{suffix}-{run['name']}.sh")
    else:
        out_file = os.path.join(RUN, f"kraft-{suffix}.sh")
    if 'command' in run.keys():
        header = TEMPLATE_RUN_KRAFT_HEADER.format(run['command'])
    else:
        header = TEMPLATE_RUN_KRAFT_HEADER.format("placeholder")
    with open(out_file, "w", encoding="utf8") as stream:
        stream.write(header)
        if "rootfs" in run.keys():
            stream.write(f"rootfs=\"{run['rootfs']}\"\n")
        stream.write(RUN_KILL_COMMANDS)
        if run["networking"]:
            stream.write(RUN_COMMON_NET_COMMANDS)
            stream.write(RUN_KRAFT_NET_COMMANDS)
        stream.write("\n")
        if run["networking"]:
            stream.write("sudo ")
        stream.write("kraft run \\\n")
        if 'accel' not in run.keys():
            stream.write("    -W \\\n")
        elif not run['accel']:
            stream.write("    -W \\\n")
        stream.write("    --log-level debug --log-type basic \\\n")
        if run["networking"]:
            stream.write("    --network bridge:virbr0 \\\n")
        if name:
            stream.write(f"    --target {name} --plat {plat} \\\n")
        else:
            stream.write(f"    --arch {arch} --plat {plat} \\\n")
        if "rootfs" in run.keys():
            if fs == "initrd":
                stream.write("    --initrd \"$rootfs\" \\\n")
            if fs == "9pfs":
                stream.write("    -v \"$rootfs\":/ \\\n")
        if run["networking"]:
            stream.write("    -a netdev.ipv4_addr=172.44.0.2 -a netdev.ipv4_gw_addr=172.44.0.1 \
-a netdev.ipv4_subnet_mask=255.255.255.0 \\\n")
        stream.write("    -- $cmd \\\n")

        st = os.stat(out_file)
        os.chmod(out_file, st.st_mode | stat.S_IEXEC)

def generate_run():
    """Generate running scripts.

    Scripts are generated in scripts/run/ directory.
    """

    if not os.path.exists(RUN):
        os.mkdir(RUN)

    # Obtain configurations for running applications.
    with open(os.path.join(SCRIPTS, "run.yaml"), "r", encoding="utf8") as stream:
        data = yaml.safe_load(stream)
        runs = data['runs']

    # Obtain targets for basic QEMU and Firecracker-based runs from defconfigs.
    def_targets = []
    for file in files(DEFCONFIGS):
        tokens = file.split('-')
        if len(tokens) == 2:
            item = (tokens[0], tokens[1], None)
        elif len(tokens) >= 3:
            item = (tokens[0], tokens[1], tokens[2])
        else:
            print(f"Unknown tokens in deconfig file {file}")
        def_targets.append(item)
    def_targets = set(def_targets)

    print(def_targets)
    # Obtain targets for KraftKit runs form kraft.yaml.
    with open(KRAFTCONFIG, "r", encoding="utf8") as stream:
        data = yaml.safe_load(stream)
        if 'targets' not in data.keys():
            printf(f"No targets in {KRAFTCONFIG}.")
            return

        # Walk through all configured applications.
        for run in runs:
            # Generate QEMU/Firecrakcer based scripts based on defconfigs.
            for plat, arch, fs in def_targets:
                if plat == "fc":
                    generate_run_fc(run, arch, fs)
                if plat == "qemu":
                    generate_run_qemu(run, arch, fs)

            # Generate KraftKit-based run scripts from kraft.yaml targets.
            for target in data['targets']:
                fs = None
                if 'kconfig' in target:
                    if "CONFIG_LIBVFSCORE_ROOTFS=\"9pfs\"" in target['kconfig']:
                        fs = "9pfs"
                    elif "CONFIG_LIBVFSCORE_ROOTFS=\"initrd\"" in target['kconfig']:
                        fs = "initrd"
                if 'name' in target.keys():
                    name = target['name']
                else:
                    name = None
                generate_run_kraft(run, name, target['architecture'], target['platform'], fs)

def get_appname():
    """Get the application name from the current directory.

    Application name is the last part in the directory path.
    """

    global APPNAME

    APPNAME = os.path.basename(os.getcwd())

def main():
    """The main program function calls generate functions.

    In effect, this triggers the generation of build and run scripts.
    """

    get_appname()
    generate_build()
    generate_run()

if __name__ == "__main__":
    sys.exit(main())
