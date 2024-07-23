# bpf-mons - Utilities for resources usage tracking

A collection of `BPF` programs (mon) for in-depth (user-space/kernel-space)
applications tracing.

# bpf-memmon

Intercepts various memory related functions (e.g. `libc` `malloc()`, `free()`,
kernel's `handle_mm_fault()`, etc.)

# bpf-fdmon

Intercepts functions that modify processes file-table (e.g. `libc` `open()`,
`dup()`, `close()`, etc.)

# Build and run

1. Your kernel should be compiled with `CONFIG_KPROBES` (`USE=kprobes`)

2. Build and deploy `blazesym-c`
```
emerge-$BOARD dev-rust/blazesym-c
cros deploy $BOARD dev-rust/blazesym-c
```

3. Build and deploy `BPF` mons
```
emerge-$BOARD dev-util/bpf-mons
cros deploy $BOARD dev-util/bpf-mons
```

On the DUT (for example):
```
bpf-memmon -p $(pidof $APP)
```
