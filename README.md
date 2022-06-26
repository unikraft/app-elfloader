# App ELF Loader

A Unikraft ELF loader app for running Linux binaries in binary compatibility mode.

For more information see [Run App ELF Loader](https://github.com/unikraft/run-app-elfloader).


## Debugging ELF apps

1. Make sure your ELF app is built with debug information.

2. Enable `dbg` logs for `appelfloader`.

3. Start app-elfloader and find the start address the ELF app is loaded to:

    ```
    [...]
    [    5.241754] dbg:  [appelfloader] <elf_exec.c @   70> [...]/app-elfloader_kvm-x86_64.dbg: start: 0x3fe01000
    [...]
    ```

    Here, the start address is `0x3fe01000`.

4. Explicitly add the debug symbols from the ELF app:

    ```
    add-symbol-file filename -o offset
    ```

Note that writes to `stdout` might not be visible immediately, if not flushed explicitly (`fflush(stdout)`).


### Duplicate symbols

Common symbols like `main` might exist in both the KVM image and the ELF application.

You can be specific by referring to their respective source file if debug information is present: `helloworld.c:main` instead of just `main`.

Alternatively you can use [`info functions [regexp]`](https://sourceware.org/gdb/current/onlinedocs/gdb/Symbols.html) to find the address of your symbol.
For example, `info function ^main$` prints the address of the `main` function.
