# Unikraft ELF Loader

## Quick Setup (aka TLDR)

For a quick setup, run the commands below.
Note that you still need to install the [requirements](#requirements).

For building and running everything, follow the steps below.
We will use the system `ls` command for running, and we will assume it is located at `/usr/bin/ls`

```console
git clone https://github.com/unikraft/app-elfloader/
cd app-elfloader/
git clone https://github.com/unikraft/unikraft .unikraft/unikraft
git clone https://github.com/unikraft/lib-lwip .unikraft/libs/lwip
git clone https://github.com/unikraft/lib-libelf .unikraft/libs/libelf
UK_DEFCONFIG=$(pwd)/.config.elfloader_qemu-x86_64 make defconfig
make -j $(nproc)
sudo /usr/bin/qemu-system-x86_64 \
    -fsdev local,id=myid,path="/",security_model=none \
    -device virtio-9p-pci,fsdev=myid,mount_tag=fs0,disable-modern=on,disable-legacy=off \
    -kernel build/elfloader_qemu-x86_64 -nographic \
    -enable-kvm -cpu host \
    -append /bin/ls
```

This will configure and build the `app-elfloader`.
After that, it will run the `/bin/ls` `ELF` file on top of the `elfloader` image:

```text
SeaBIOS (version rel-1.16.2-0-gea1b7a073390-prebuilt.qemu.org)

iPXE (http://ipxe.org) 00:03.0 CA00 PCI2.10 PnP PMM+06FD0FC0+06F30FC0 CA00

Booting from ROM..Powered by
o.   .o       _ _               __ _
Oo   Oo  ___ (_) | __ __  __ _ ' _) :_
oO   oO ' _ `| | |/ /  _)' _` | |_|  _)
oOo oOO| | | | |   (| | | (_) |  _) :_
 OoOoO ._, ._:_:_,\_._,  .__,_:_, \___)
                  Atlas 0.13.1~21ce1acf
bin  boot  cdrom  dev  etc  home  lib  lib32  lib64  libx32  lost+found  media  mnt  opt  proc  root  run  sbin  snap  srv  sys  tmp  usr  var
```

Information about every step is detailed below.

## Requirements

In order to set up, configure, build and run `app-elfloader` on Unikraft, the following packages are required:

* `build-essential` / `base-devel` / `@development-tools` (the meta-package that includes `make`, `gcc` and other development-related packages)
* `sudo`
* `flex`
* `bison`
* `git`
* `wget`
* `uuid-runtime`
* `qemu-system-x86`
* `qemu-kvm`
* `sgabios`

On Ubuntu/Debian or other `apt`-based distributions, run the following command to install the requirements:

```console
sudo apt install -y --no-install-recommends \
  build-essential \
  sudo \
  libncurses-dev \
  libyaml-dev \
  flex \
  bison \
  git \
  wget \
  uuid-runtime \
  qemu-kvm \
  qemu-system-x86 \
  sgabios
```

## Set Up

The following repositories are required for `app-elfloader`:

* The application repository (this repository): [`app-elfloader`](https://github.com/unikraft/app-elfloader)
* The Unikraft core repository: [`unikraft`](https://github.com/unikraft/unikraft)
* Library repositories:
  * The networking stack library: [`lib-lwip`](https://github.com/unikraft/lib-lwip)
  * The `ELF` Tool Chain library: [`lib-libelf`](https://github.com/unikraft/lib-libelf)

Follow the steps below for the setup:

  1. First clone the [`app-elfloader` repository](https://github.com/unikraft/app-elfloader) in the `elfloader/` directory:

     ```console
     git clone https://github.com/unikraft/app-elfloader elfloader
     ```

     Enter the `elfloader/` directory:

     ```console
     cd elfloader/

     ls -F
     ```

     This will show you the contents of the repository:

     ```text
     arch_prctl.c  brk.c  Config.uk  elf_ctx.c  elf_load.c  elf_prog.h  example/  exportsyms.uk  libelf_helper.h  main.c  Makefile  Makefile.uk  README.md  support/
     ```
     ```

  1. While inside the `elfloader/` directory, create the `.unikraft/` directory:

     ```console
     mkdir .unikraft
     ```

     Enter the `.unikraft/` directory:

     ```console
     cd .unikraft/
     ```

  1. While inside the `.unikraft` directory, clone the [`unikraft` repository](https://github.com/unikraft/unikraft):

     ```console
     git clone https://github.com/unikraft/unikraft unikraft
     ```

  1. While inside the `.unikraft/` directory, create the `libs/` directory:

     ```console
     mkdir libs
     ```

  1. While inside the `.unikraft/` directory, clone the library repositories in the `libs/` directory:

     ```console
     git clone https://github.com/unikraft/lib-lwip libs/lwip

     git clone https://github.com/unikraft/lib-libelf libs/libelf
     ```

  1. Get back to the application directory:

     ```console
     cd ../
     ```

     Use the `tree` command to inspect the contents of the `.unikraft/` directory.
     It should print something like this:

     ```console
     tree -F -L 2 .unikraft/
     ```

     The layout of the `.unikraft/` directory should look something like this:

     ```text
     .unikraft/
     |-- libs/
     |   |-- lwip/
     |   |-- libelf/
     `-- unikraft/
         |-- arch/
         |-- Config.uk
         |-- CONTRIBUTING.md
         |-- COPYING.md
         |-- include/
         |-- lib/
         |-- Makefile
         |-- Makefile.uk
         |-- plat/
         |-- README.md
         |-- support/
         `-- version.mk

     9 directories, 7 files
     ```

## Configure

Configuring, building and running a Unikraft application depends on our choice of platform and architecture.
Currently, supported platform and architecture for `app-elfloader` are QEMU (KVM), x86_64.
Use the `.config.elfloader_qemu-x86_64` configuration file together with `make defconfig` to create the configuration file:

```console
UK_DEFCONFIG=$(pwd)/.config.elfloader_qemu-x86_64 make defconfig
```

This results in the creation of the `.config` file:

```console
ls .config
.config
```

The `.config` file will be used in the build step.

## Build

Building uses as input the `.config` file from above, and results in a unikernel image as output.
The unikernel output image, together with intermediary build files, are stored in the `build/` directory.

### Clean Up

Before building after some changes had been made, you may need to clean up the build output.

Cleaning up is done with 3 possible commands:

* `make clean`: cleans all actual build output files (binary files, including the unikernel image)
* `make properclean`: removes the entire `build/` directory
* `make distclean`: removes the entire `build/` directory **and** the `.config` file

Typically, you would use `make properclean` to remove all build artifacts, but keep the configuration file.

### QEMU x86_64

Building for QEMU x86_64 assumes you did the QEMU x86_64 configuration step above.
Build the Unikraft elfloader image for QEMU x86_64 by using the command below:

```console
make -j $(nproc)
```

You can see a list of all the files processed by the build system:

```text
[...]
  LD      elfloader_qemu-x86_64.dbg
  UKBI    elfloader_qemu-x86_64.dbg.bootinfo
  SCSTRIP elfloader_qemu-x86_64
  GZ      elfloader_qemu-x86_64.gz
make[1]: Leaving directory '/tmp/apps/app-elfloader/.unikraft/unikraft'
```

At the end of the build command, the `elfloader_qemu-x86_64` unikernel image is generated.
This image is to be used in the run step.

## Executing ELF binaries

The `elfloader` currently supports statically-linked and dynamically-linked applications for Linux on x86_64, as long as they are compiled position independent (PIE).
In most cases, such an application can be loaded from any virtual file system that is supported by Unikraft.
For example, the application binary can be packaged with a CPIO initramdisk or handed over via a 9pfs host share.
Please note that we use 9pfs in the following how-to.
To load the application from another file system (e.g., initramdisk), you will need to follow equivalent steps.

Before we can launch an application we need to prepare a root file system that contains the ELF binary along with its library dependencies.
You can use `ldd` (or probably `musl-ldd` for applications linked with [`musl`](https://www.musl-libc.org/)) to list the shared libraries on which the application depends.
Please note that the vDSO (here: `linux-vdso.so.1`) is a kernel-provided library that is not present on the host filesystem.
Please ignore this file.

For a helloworld example application (here: [`/example/helloworld`](./example/helloworld), compiled on Debian 11), `ldd` will likely look like the following:

```sh
$ ldd helloworld
	linux-vdso.so.1 (0x00007ffdd695d000)
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007efed259f000)
	/lib64/ld-linux-x86-64.so.2 (0x00007efed2787000)
```

Copy the library dependencies to the same subdirectories as reported by `ldd`.
Please remember to also copy any additional and required configuration files to the root file system.
In this example, the populated root filesystem will look like this:

```text
rootfs/
├── lib
│   └── x86_64-linux-gnu
│       └── libc.so.6
├── lib64
│   └── ld-linux-x86-64.so.2
└── helloworld
```

Because the official dynamic loader maps the application and libraries into memory, the `elfloader` unikernel must be configured with `posix-mmap`, `ukvmem`, and `vfscore`.
For 9pfs, also make sure that you configured `vfscore` to automatically mount a host shared filesystem: Under `Library Configuration -> vfscore: Configuration` select `Automatically mount a root filesystem`, set `Default root filesystem` to `9PFS`, and ensure that `Default root device` is to `fs0`.
This last option simplifies the use of the `-e` parameter of [`qemu-guest`](https://github.com/unikraft/unikraft/tree/staging/support/scripts).

The application can then be started with:

```sh
# qemu-guest -k elfloader_kvm-x86_64 -e rootfs/ \
            -a "/helloworld <application arguments>"
```

*NOTE:* This command line example expects that you built your unikernel with `Application Options -> Application name/path via command line` (`APPELFLOADER_CUSTOMAPPNAME`).

*HINT:* Environment variables can be set through `lib/posix-environ` and `lib/uklibparam`.
For this purpose, enable `Library Configuration -> posix-environ` and activate `Parse kernel command line arguments`.
The variables can be handed over via the kernel command line with the Unikraft library parameter `env.vars`, for example:

```sh
# qemu-guest -k elfloader_kvm-x86_64 -e rootfs/ \
            -a "env.vars=[ LD_LIBRARY_PATH=/lib LD_SHOW_AUXV=1 ] -- /helloworld <application arguments>"
```

*NOTE:* At the moment, a program exit will not yet cause a shutdown of the elfloader unikernel. You need to manually terminate it.
In case of `qemu-guest`, you can use `CTRL` + `C`.

## Debugging

### `strace`-like Output

Unikraft's [`syscall_shim`](https://github.com/unikraft/unikraft/tree/staging/lib/syscall_shim) provides the ability to print a strace-like message for every processed binary system call request on the kernel output.
This option can be useful for understanding what code a system call handler returns to the application, and how the application interacts with the kernel.
The setting can be found under `Library Configuration -> syscall_shim -> Debugging`: `'strace'-like messages for binary system calls`.

### GNU Debugger (gdb)

It is possible to debug `elfloader` together with the loaded application, and use the full set of debugging facilities for kernel and application at the same time.
In principle, `gdb` must only be made aware of the runtime memory layout of `elfloader` with the loaded application.
Thanks to the single address space layout, we gain easy debugability and full transparency.

#### Static-PIE ELF

As a first step, `gdb` is started with loading the symbols from the `dbg` image of the `elfloader`.
We map the symbols of ELF application with the gdb command `add-symbol-file` by specifying the application (with debug symbols) and the base load address.
If `info` messages are enabled in `ukdebug`, this base address will be messaged by the loader like this:

```
ELF program loaded to 0x400101000-0x4001c2a08 (793096 B), entry at 0x40010ad50
```

To this address (here: `0x400101000`) you have to add the offset of the `.text` segment.
You can use `readelf -S` to find it out. In our example it is `0x92a0` (output shortened):

```
$ readelf -S helloworld_static
Section Headers:
  [Nr] Name              Type             Address           Offset
       Size              EntSize          Flags  Link  Info  Align
  [11] .text             PROGBITS         00000000000092a0  000092a0
       0000000000086eb0  0000000000000000  AX       0     0     32
```

The resulting address here is `0x40010a2a0`. The symbols of the static helloworld program can then be loaded from `gdb` with the following command:

```
(gdb) add-symbol-file -readnow helloworld_static 0x40010A2A0
```

From this point you have symbol resolution in your debugger, for both the Unikraft elfloader and the loaded application.

*NOTE:* You can only set regular breakpoints within the application (`break` with GDB) after it got loaded into memory by elfloader (otherwise they will be ignored).
The recommended procedure is:

1. Set a breakpoint just after the application was loaded (e.g., the first system call that the application executes),
2. Let the execution continue until the breakpoint is reached.
3. Set the interesting breakpoints within application space.

#### Dynamically-linked ELF

The principle of runtime address space layout for dynamically linked executables is the same as for statically linked executables.
The differences are that we have additionally loaded a dynamic loader together with the application and we have to load the symbols of each dependent dynamic library as well.
For this purpose, we recommend to enable `strace`-like output in `syscall_shim` (read subsection: [`strace`-like output](#strace-like-output)).
It is the dynamic loader that will for each library:

1. Open the library.
2. Parse the ELF header.
3. Memory-map all needed sections into memory.
4. Close the library file again.

For our Helloworld example compiled with `glibc`, this looks like the following for libc:

```cpp
openat(AT_FDCWD, "/libc.so.6", O_RDONLY|O_CLOEXEC) = fd:3
read(fd:3, <out>"\x7FELF\x02\x01\x01\x03\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00>\x00\x01\x00\x00\x00"..., 832) = 832
fstat(fd:3, va:0x40006ef18) = OK
mmap(NULL, 1918592, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, fd:3, 0) = va:0x8000005000
mmap(va:0x8000027000, 1417216, PROT_EXEC|PROT_READ, MAP_PRIVATE|MAP_DENYWRITE|MAP_FIXED, fd:3, 139264) = va:0x8000027000
mmap(va:0x8000181000, 323584, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE|MAP_FIXED, fd:3, 1556480) = va:0x8000181000
mmap(va:0x80001d0000, 24576, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_DENYWRITE|MAP_FIXED, fd:3, 1875968) = va:0x80001d0000
mmap(va:0x80001d6000, 13952, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, fd:-1, 0) = va:0x80001d6000
close(fd:3) = OK
```

The virtual address returned by the first `mmap` operation is the virtual base address of the application that we need to note down.
In this case, the virtual base address of `libc.so.6` is `0x8000005000`.
Please do the same for each dynamically loaded library.

To load the application and library symbols appropriately, as described in the previous subsection, you must add the segment offset of the `.text` section to the virtual base address.
This allows you to load the symbols from `gdb` with `add-symbol-file`.

*NOTE:* Regular breakpoints in shared libraries can only be set after the libraries have been loaded into memory.
Since these are loaded by the dynamic loader and not directly by the `elfloader`, this is done with `mmap` system calls as shown in the console snippet above.
The corresponding `close` system call (`break uk_syscall_r_close`) is a safe place to hop to before setting the actual breakpoints within a shared library.

#### Recommendation: `gdb` Setup Script

Because of the calculations, we recommend scripting the `gdb` setup so that any subsequent `gdb` debug session will be ready quickly.
You can get inspiration from the following `bash` script, which provides a function that automatically determines the `.text` offset and adds it to a given base address.
The advantage is that only the base addresses have to be noted from the Unikraft console output.

```bash
#!/bin/bash
# Host and port of the GDB server port (qemu)
GDBSRV=":1234"

# Generate a GDB command line for loading the symbols of an ELF executable/
# shared library.
# Usage: gdb-add-symbols "<ELF executable/library>" \
#                        "<base load address (hex, no leading '0x')>"
gdb-add-symbols()
{
	local LOAD_ELF="$1"
	local LOAD_ADDR="${2}"
	local LOAD_TADDR=
	local TEXT_OFFSET=

	# Hacky way to figure out the .text offset
	TEXT_OFFSET=$( readelf -S "${LOAD_ELF}" | grep '.text' | awk '{ print $5 }' )

	# Compute offset of .text section with base address
	LOAD_TADDR=$( printf 'obase=16;ibase=16;%s+%s\n' "${LOAD_ADDR^^}" "${TEXT_OFFSET^^}" | bc )

	# Generate GDB command
	printf 'add-symbol-file -readnow %s 0x%s' "${LOAD_ELF}" "${LOAD_TADDR}"
}

# Connect to $GDBSRV and set up gdb
# NOTE: The first block of instructions connects to the gdb port of the qemu
#       process and follows the CPU mode change while the guest is booting.
#       This is currently a requirement for using gdb with qemu for x86_64. For
#       other platforms and architectures, this may be different.
#       This block assumes that the guest is started in paused state (`-P` for
#       `qemu-guest`).
# NOTE: Please note that regular breakpoints within the application or a shared
#       library can only be set after they have been loaded into memory. Usually
#       this is done by elfloader for the application and dynamic loader or by
#       the dynamic loader for shared libraries.
# HINT: You can use the `directory` command to specify additional paths that
#       `gdb` will use to search for source files.
#       For example, if you run your dynamically linked application with
#       Debian's libc, you can install (`apt install glibc-source`) and
#       extract the glibc sources under /usr/src/glibc.
#         --eval-command="directory /usr/src/glibc/glibc-2.31"
exec gdb \
	--eval-command="target remote $GDBSRV" \
	--eval-command="hbreak _ukplat_entry" \
	--eval-command="continue" \
	--eval-command="disconnect" \
	--eval-command="set arch i386:x86-64:intel" \
	--eval-command="target remote $GDBSRV" \
	\
	--eval-command="$( gdb-add-symbols "rootfs/helloworld" "8000000000" )" \
	--eval-command="$( gdb-add-symbols "rootfs/libc.so.6" "8000005000" )"
```

#### Hint: Debug symbols of libraries installed from packages

If you run your dynamically-linked application with libraries installed via a package manager, you can check if a debug package is also available for installation.
For example, Debian provides the debug symbols automatically for `gdb` with the following installation (root privileges required):

```sh
# apt install libc6-dbg
```

As soon as the Debian's libc.so.6 is loaded by `gdb`, the debugger will load the symbols provided by the Debian debug package.

#### Hint: Sources of libraries installed from packages

If you run your dynamically linked application with libraries installed via a package manager, you can check if a source package is available for installation.
For example, the libc sources are available under Debian with the package `glibc-source`. After you extracted the installed sources archive under `/usr/src/glibc`, you can make these sources visible to `gdb` with (given that you use Debian's libc also for the application with `elfloader`):

```
(gdb) directory /usr/src/glibc/glibc-2.31
```
