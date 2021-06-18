// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_CONSTANTS_H_

namespace diagnostics {

inline constexpr char kPathSysPci[] = "sys/bus/pci/devices/";

inline constexpr char kFileDriver[] = "driver";

inline constexpr char kFilePciClass[] = "class";
inline constexpr char kFilePciDevice[] = "device";
inline constexpr char kFilePciVendor[] = "vendor";

#define GET_BYTE_(val, id) ((val >> (id * 8)) & 0xFF)
#define GET_PCI_CLASS(val) GET_BYTE_(val, 2)
#define GET_PCI_SUBCLASS(val) GET_BYTE_(val, 1)
#define GET_PCI_PROG_IF(val) GET_BYTE_(val, 0)

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_CONSTANTS_H_
