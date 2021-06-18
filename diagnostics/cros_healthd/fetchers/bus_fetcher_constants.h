// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_CONSTANTS_H_

namespace diagnostics {

inline constexpr char kPathSysPci[] = "sys/bus/pci/devices/";
inline constexpr char kPathSysUsb[] = "sys/bus/usb/devices/";

inline constexpr char kFileDriver[] = "driver";

inline constexpr char kFilePciClass[] = "class";
inline constexpr char kFilePciDevice[] = "device";
inline constexpr char kFilePciVendor[] = "vendor";

#define GET_BYTE_(val, id) ((val >> (id * 8)) & 0xFF)
#define GET_PCI_CLASS(val) GET_BYTE_(val, 2)
#define GET_PCI_SUBCLASS(val) GET_BYTE_(val, 1)
#define GET_PCI_PROG_IF(val) GET_BYTE_(val, 0)

inline constexpr char kFileUsbDevClass[] = "bDeviceClass";
inline constexpr char kFileUsbDevSubclass[] = "bDeviceSubClass";
inline constexpr char kFileUsbDevProtocol[] = "bDeviceProtocol";
inline constexpr char kFileUsbIFNumber[] = "bInterfaceNumber";
inline constexpr char kFileUsbIFClass[] = "bInterfaceClass";
inline constexpr char kFileUsbIFSubclass[] = "bInterfaceSubClass";
inline constexpr char kFileUsbIFProtocol[] = "bInterfaceProtocol";
inline constexpr char kFileUsbVendor[] = "idVendor";
inline constexpr char kFileUsbProduct[] = "idProduct";
inline constexpr char kFileUsbProductName[] = "product";
inline constexpr char kPropertieVendor[] = "ID_VENDOR_FROM_DATABASE";
inline constexpr char kPropertieProduct[] = "ID_MODEL_FROM_DATABASE";

// The classes of pci / usb ids. See https://github.com/gentoo/hwids.
// clang-format off
namespace pci_ids {
  namespace network {  // NOLINT(runtime/indentation_namespace)
    inline constexpr uint8_t kId = 0x02;
    namespace ethernet {  // NOLINT(runtime/indentation_namespace)
      inline constexpr uint8_t kId = 0x00;
    }
    namespace network {  // NOLINT(runtime/indentation_namespace)
      inline constexpr uint8_t kId = 0x80;
    }
  }
  namespace display {  // NOLINT(runtime/indentation_namespace)
    inline constexpr uint8_t kId = 0x03;
  }
}  // namespace pci_ids

namespace usb_ids {
  namespace wireless {  // NOLINT(runtime/indentation_namespace)
    inline constexpr uint8_t kId = 0xe0;
    namespace radio_frequency {  // NOLINT(runtime/indentation_namespace)
      inline constexpr uint8_t kId = 0x01;
      namespace bluetooth {  // NOLINT(runtime/indentation_namespace)
        inline constexpr uint8_t kId = 0x01;
      }
    }
  }
}  // namespace usb_ids
// clang-format on

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_CONSTANTS_H_
