# libpmt

A C++ library which handles sampling and decoding of the PMT data via sysfs
from the `intel_pmt` class of devices. These devices provide access to
hardware telemetry metrics of the SoC. The semantics of this data is described
via XML-based [medatata] and is exposed via [sysfs].

[Intel PMT]: https://github.com/intel/Intel-PMT
[medatada]: https://github.com/intel/Intel-PMT/tree/HEAD/xml/MTL/0
[sysfs]: https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-class-intel_pmt
