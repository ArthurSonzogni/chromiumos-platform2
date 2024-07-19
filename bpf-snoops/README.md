# bpf-snoops - Utilities for resources usage tracking

A collection of `BPF` programs (snoop) for in-depth (user-space/kernel-space)
applications tracing.

# bpf-memsnoop

Intercepts various memory related functions (e.g. `libc` `malloc()`, `free()`,
kernel's `handle_mm_fault()`, etc.)

# bpf-fdsnoop

Intercepts functions that modify processes file-table (e.g. `libc` `open()`,
`dup()`, `close()`, etc.)

# Build and run

1. Your kernel should be compiled with `CONFIG_KPROBES` (`USE=kprobes`)

2. Build and deploy `blazesym-c`
```
emerge-$BOARD blazesym-c
cros deploy $BOARD blazesym-c
```

3. Build and deploy `BPF` snoops
```
emerge-$BOARD bpf-snoops
cros deploy $BOARD bpf-snoops
```

On the DUT (for example):
```
bpf-memsnoop -p $(pidof $APP)
```
