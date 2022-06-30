// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_H_

#include <base/containers/flat_map.h>
#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace mojom = chromeos::cros_healthd::mojom;

// Returns a structure with a list of data fields for each of the bus device
// or the error that occurred fetching the information.'
using FetchBusDevicesCallback = base::OnceCallback<void(mojom::BusResultPtr)>;
void FetchBusDevices(Context* context, FetchBusDevicesCallback callback);

// Same as above but returns a map of real sysfs paths to the bus devices. This
// can be used to identify the bus devices by getting the real path of a
// symbolic link to each bus devices.
using FetchSysfsPathsBusDeviceMapCallback = base::OnceCallback<void(
    base::flat_map<base::FilePath, mojom::BusDevicePtr>)>;
void FetchSysfsPathsBusDeviceMap(Context* context,
                                 FetchSysfsPathsBusDeviceMapCallback callback);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_H_
