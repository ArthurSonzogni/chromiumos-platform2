// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_CONSTANTS_H_

namespace diagnostics {

inline constexpr char kPropertieVendorFromDB[] = "ID_VENDOR_FROM_DATABASE";
inline constexpr char kPropertieModelFromDB[] = "ID_MODEL_FROM_DATABASE";
inline constexpr char kPropertieProduct[] = "PRODUCT";
inline constexpr char kFileUsbManufacturerName[] = "manufacturer";
inline constexpr char kFileUsbProductName[] = "product";
inline constexpr char kFileUsbDevClass[] = "bDeviceClass";
inline constexpr char kFileUsbDevSubclass[] = "bDeviceSubClass";
inline constexpr char kFileUsbDevProtocol[] = "bDeviceProtocol";
inline constexpr char kFileUsbIFNumber[] = "bInterfaceNumber";
inline constexpr char kFileUsbIFClass[] = "bInterfaceClass";
inline constexpr char kFileUsbIFSubclass[] = "bInterfaceSubClass";
inline constexpr char kFileUsbIFProtocol[] = "bInterfaceProtocol";
inline constexpr char kFileUsbVendor[] = "idVendor";
inline constexpr char kFileUsbProduct[] = "idProduct";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_CONSTANTS_H_
