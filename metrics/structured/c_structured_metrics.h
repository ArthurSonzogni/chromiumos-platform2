// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_STRUCTURED_C_STRUCTURED_METRICS_H_
#define METRICS_STRUCTURED_C_STRUCTURED_METRICS_H_

#include <stdint.h>

#include <brillo/brillo_export.h>

#if defined(__cplusplus)
extern "C" {
#endif

// C wrapper for
// metrics::structured::events::bluetooth::BluetoothAdapterStateChanged.
BRILLO_EXPORT void BluetoothAdapterStateChanged(const char* boot_id,
                                                int64_t system_time,
                                                int state);

// C wrapper for
// metrics::structured::events::bluetooth::BluetoothPairingStateChanged.
BRILLO_EXPORT void BluetoothPairingStateChanged(const char* boot_id,
                                                int64_t system_time,
                                                const char* device_id,
                                                int device_type,
                                                int state);

// C wrapper for
// metrics::structured::events::bluetooth::BluetoothAclConnectionStateChanged.
BRILLO_EXPORT void BluetoothAclConnectionStateChanged(const char* boot_id,
                                                      int64_t system_time,
                                                      const char* device_id,
                                                      int device_type,
                                                      int connection_direction,
                                                      int connection_initiator,
                                                      int state_change_type,
                                                      int state);

// C wrapper for
// metrics::structured::events::bluetooth::
// BluetoothProfileConnectionStateChanged.
BRILLO_EXPORT void BluetoothProfileConnectionStateChanged(const char* boot_id,
                                                          int64_t system_time,
                                                          const char* device_id,
                                                          int state_change_type,
                                                          int profile,
                                                          int state);

// C wrapper for
// metrics::structured::events::bluetooth::BluetoothDeviceInfoReport.
BRILLO_EXPORT void BluetoothDeviceInfoReport(const char* boot_id,
                                             int64_t system_time,
                                             const char* device_id,
                                             int device_type,
                                             int device_class,
                                             int device_category,
                                             int vendor_id,
                                             int vendor_id_source,
                                             int product_id,
                                             int product_version);

#if defined(__cplusplus)
}
#endif
#endif  // METRICS_STRUCTURED_C_STRUCTURED_METRICS_H_
