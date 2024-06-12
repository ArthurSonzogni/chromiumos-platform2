# bpf-snoops - Utilities for resources usage tracking

A collection of BSP programs (snoop) for in-depth (user-space/kernel-space)
applications tracing.

# bpf-memsnoop

Intercepts various memory related functions (e.g. libc malloc(), free(),
kernel's handle_mm_fault(), etc.)

# bpf-fdsnoop

Interepts functions that modify processes file-table (e.g. libc open(),
dup(), close(), etc.)
