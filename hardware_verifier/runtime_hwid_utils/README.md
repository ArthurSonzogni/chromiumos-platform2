# Runtime HWID Utils

C++ library that provides utilities for managing Runtime HWID.

## Overview

Runtime HWID is a HWID generated at runtime by [Hardware Verifier](/README.md).
Unlike the HWID encoded during manufacturing (i.e. Factory HWID), the Runtime
HWID can reflect the current state of the hardware, which might change due to
repairs or component replacements.

The Runtime HWID is saved in the file
`/var/cache/hardware_verifier/runtime_hwid`. The file is only created when there
is a hardware change. If the file is missing or corrupted, this library will
fall back to using Factory HWID as Runtime HWID.

## Design

See the [design document](http://go/cros-runtime-hwid) for the details.
