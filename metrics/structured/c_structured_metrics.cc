// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// C wrapper to structured metrics code
//

#include "base/feature_list.h"

#include "metrics/structured/c_structured_metrics.h"
#include "metrics/structured/structured_events.h"

namespace bluetooth = metrics::structured::events::bluetooth;

// TODO(b/181724341): Remove this experimental once the feature is rolled out.
const base::Feature kBluetoothSessionizedMetrics{
    "BluetoothSessionizedMetrics", base::FEATURE_DISABLED_BY_DEFAULT};

extern "C" void BluetoothAdapterStateChanged(int64_t system_time, int state) {
  if (base::FeatureList::IsEnabled(kBluetoothSessionizedMetrics)) {
    bluetooth::BluetoothAdapterStateChanged()
        .SetSystemTime(system_time)
        .SetAdapterState(state)
        .Record();
  }
}

extern "C" void BluetoothPairingStateChanged(int64_t system_time,
                                             const char* device_id,
                                             int device_type,
                                             int state) {
  if (base::FeatureList::IsEnabled(kBluetoothSessionizedMetrics)) {
    bluetooth::BluetoothPairingStateChanged()
        .SetSystemTime(system_time)
        .SetDeviceId(device_id)
        .SetDeviceType(device_type)
        .SetPairingState(state)
        .Record();
  }
}

extern "C" void BluetoothAclConnectionStateChanged(int64_t system_time,
                                                   const char* device_id,
                                                   int device_type,
                                                   int state_change_type,
                                                   int state) {
  if (base::FeatureList::IsEnabled(kBluetoothSessionizedMetrics)) {
    bluetooth::BluetoothAclConnectionStateChanged()
        .SetSystemTime(system_time)
        .SetDeviceId(device_id)
        .SetDeviceType(device_type)
        .SetStateChangeType(state_change_type)
        .SetAclConnectionState(state)
        .Record();
  }
}

extern "C" void BluetoothProfileConnectionStateChanged(int64_t system_time,
                                                       const char* device_id,
                                                       int state_change_type,
                                                       int profile,
                                                       int state) {
  if (base::FeatureList::IsEnabled(kBluetoothSessionizedMetrics)) {
    bluetooth::BluetoothProfileConnectionStateChanged()
        .SetSystemTime(system_time)
        .SetDeviceId(device_id)
        .SetStateChangeType(state_change_type)
        .SetProfile(profile)
        .SetProfileConnectionState(state)
        .Record();
  }
}

extern "C" void BluetoothDeviceInfoReport(int64_t system_time,
                                          const char* device_id,
                                          int device_type,
                                          int device_class,
                                          int vendor_id,
                                          int vendor_id_source,
                                          int product_id,
                                          int product_version) {
  if (base::FeatureList::IsEnabled(kBluetoothSessionizedMetrics)) {
    bluetooth::BluetoothDeviceInfoReport()
        .SetSystemTime(system_time)
        .SetDeviceId(device_id)
        .SetDeviceType(device_type)
        .SetDeviceClass(device_class)
        .SetVendorId(vendor_id)
        .SetVendorIdSource(vendor_id_source)
        .SetProductId(product_id)
        .SetProductVersion(product_version)
        .Record();
  }
}
