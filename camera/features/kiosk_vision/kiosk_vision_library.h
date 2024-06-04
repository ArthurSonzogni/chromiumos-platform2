// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_LIBRARY_H_
#define CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_LIBRARY_H_

#include <base/files/file_path.h>
#include <base/native_library.h>
#include <cros-camera/libkioskvision/kiosk_audience_measurement_bindings.h>
#include <cros-camera/libkioskvision/kiosk_audience_measurement_types.h>

namespace cros {

// A singleton to load a kiosk vision dynamic library (.so) and access its API.
class KioskVisionLibrary {
 public:
  // Loads a Kiosk Vision dynamic library from |dlc_root_path|.
  // Needs to be called before any |Get| calls.
  // No-op when already loaded.
  static void Load(const base::FilePath& dlc_root_path);

  // Checks that dynamic library and all function pointer are loaded.
  static bool IsLoaded();

  // Returns the singleton object. Should only be used when |IsLoaded| is true.
  static const KioskVisionLibrary& Get();

  ~KioskVisionLibrary() = default;

  [[nodiscard]] cros_kiosk_vision_CreateKioskAudienceMeasurementFn create_fn()
      const {
    return create_fn_;
  }

  [[nodiscard]] cros_kiosk_vision_DeleteKioskAudienceMeasurementFn delete_fn()
      const {
    return delete_fn_;
  }

  [[nodiscard]] cros_kiosk_vision_GetDetectorInputPropertiesFn
  get_properties_fn() const {
    return get_properties_fn_;
  }

  [[nodiscard]] cros_kiosk_vision_ProcessFrameFn process_frame_fn() const {
    return process_frame_fn_;
  }

  [[nodiscard]] cros_kiosk_vision_WaitUntilIdleFn wait_until_idle_fn() const {
    return wait_until_idle_fn_;
  }

  KioskVisionLibrary(KioskVisionLibrary const&) = delete;
  void operator=(KioskVisionLibrary const&) = delete;

 private:
  explicit KioskVisionLibrary(const base::FilePath& library_path);

  void LoadSharedLibrary(const base::FilePath& library_path);
  void LoadFunctions();

  [[nodiscard]] bool AllHandlesLoaded() const;

  base::NativeLibrary library_handle_ = nullptr;

  cros_kiosk_vision_CreateKioskAudienceMeasurementFn create_fn_ = nullptr;
  cros_kiosk_vision_DeleteKioskAudienceMeasurementFn delete_fn_ = nullptr;
  cros_kiosk_vision_GetDetectorInputPropertiesFn get_properties_fn_ = nullptr;
  cros_kiosk_vision_ProcessFrameFn process_frame_fn_ = nullptr;
  cros_kiosk_vision_WaitUntilIdleFn wait_until_idle_fn_ = nullptr;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_LIBRARY_H_
