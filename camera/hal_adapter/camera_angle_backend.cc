/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <base/task/thread_pool.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>

#include "cros-camera/angle_state.h"
#include "cros-camera/common.h"

#include "hal_adapter/camera_angle_backend.h"

namespace {

const struct VariationsFeature kCrOSLateBootCameraAngleBackend = {
    .name = "CrOSLateBootCameraAngleBackend",
    .default_state = FEATURE_ENABLED_BY_DEFAULT,
};
bool angle_is_enabled = cros::AngleEnabled();

void UpdateAngleIsEnabled(bool new_angle_is_enabled) {
  if (new_angle_is_enabled != angle_is_enabled) {
    LOGF(INFO) << "CrOSLateBootCameraAngleBackend changed status, need to "
                  "restart to swap GL library, angle_is_enabled: "
               << std::boolalpha << new_angle_is_enabled;
    if (new_angle_is_enabled) {
      cros::EnableAngle();
    } else {
      cros::DisableAngle();
    }

    exit(0);
  }
}

void OnRefecthNeeded() {
  feature::PlatformFeatures* feature_lib = feature::PlatformFeatures::Get();
  feature_lib->IsEnabled(kCrOSLateBootCameraAngleBackend,
                         base::BindOnce(&UpdateAngleIsEnabled));
}

}  // namespace

void cros::FetchAngleStateAndSetupListener() {
  LOGF(INFO) << "angle_is_enabled: " << std::boolalpha << angle_is_enabled;

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  options.dbus_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

  if (!feature::PlatformFeatures::Initialize(bus)) {
    LOGF(ERROR) << "Failed to Initialize platform features";
    exit(-1);
  }
  feature::PlatformFeatures* feature_lib = feature::PlatformFeatures::Get();

  feature_lib->IsEnabled(kCrOSLateBootCameraAngleBackend,
                         base::BindOnce(&UpdateAngleIsEnabled));

  feature_lib->ListenForRefetchNeeded(base::BindRepeating(&OnRefecthNeeded),
                                      base::DoNothing());
}
