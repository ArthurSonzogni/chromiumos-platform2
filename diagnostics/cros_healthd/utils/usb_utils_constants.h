// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_CONSTANTS_H_

namespace diagnostics {

inline constexpr char kPropertieVendor[] = "ID_VENDOR_FROM_DATABASE";
inline constexpr char kPropertieProduct[] = "ID_MODEL_FROM_DATABASE";
inline constexpr char kFileUsbManufacturerName[] = "manufacturer";
inline constexpr char kFileUsbProductName[] = "product";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_CONSTANTS_H_
