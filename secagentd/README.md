# Secagentd

## Overview

Secagentd is a daemon responsible for detecting and reporting security related
events through ERP (Encrypted Reporting Pipeline) for forensic analysis.

## Build and installation instructions

Since this feature is under active development it will not be enabled by default
when building an image.

To build all the packages for a board with this feature enabled you must

```
USE="bpf_extras secagent" build_packages --board="${BOARD}"
```

If you wish to upgrade an existing board image to include this feature you
should emerge the kernel

e.g:

```
USE="bpf_extras" emerge-amd64-generic chromeos-kernel-5_15
```

then you can successfully build and install secagentd

```
emerge-amd64-generic secagentd
```
