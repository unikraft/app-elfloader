# Unikraft ELF Loader

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
