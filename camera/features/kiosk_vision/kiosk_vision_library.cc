// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "features/kiosk_vision/kiosk_vision_library.h"

#include <base/check_deref.h>
#include <base/native_library.h>
#include <cros-camera/common.h>
#include <cros-camera/libkioskvision/kiosk_audience_measurement_bindings.h>

namespace cros {

namespace {

constexpr char kLibraryName[] = "libkioskvision.so";

KioskVisionLibrary* g_instance = nullptr;

}  // namespace

void KioskVisionLibrary::Load(const base::FilePath& dlc_root_path) {
  if (!g_instance) {
    g_instance = new KioskVisionLibrary(dlc_root_path.Append(kLibraryName));
  }
}

bool KioskVisionLibrary::IsLoaded() {
  if (!g_instance) {
    return false;
  }

  return KioskVisionLibrary::Get().AllHandlesLoaded();
}

const KioskVisionLibrary& KioskVisionLibrary::Get() {
  return CHECK_DEREF(g_instance);
}

KioskVisionLibrary::KioskVisionLibrary(const base::FilePath& library_path) {
  LoadSharedLibrary(library_path);
  LoadFunctions();

  if (!AllHandlesLoaded()) {
    LOGF(ERROR)
        << "Cannot load Kiosk Vision expected library functions. create_fn_: "
        << create_fn_ << "; delete_fn_: " << delete_fn_
        << "; get_properties_fn_: " << get_properties_fn_
        << "; process_frame_fn_: " << process_frame_fn_
        << "; wait_until_idle_fn_: " << wait_until_idle_fn_;
  }
}

void KioskVisionLibrary::LoadSharedLibrary(const base::FilePath& library_path) {
  base::NativeLibraryOptions native_library_options;
  base::NativeLibraryLoadError load_error;
  native_library_options.prefer_own_symbols = true;

  LOGF(INFO) << "Loading Kiosk Vision library from: " << library_path;
  library_handle_ = base::LoadNativeLibraryWithOptions(
      library_path, native_library_options, &load_error);
  if (!library_handle_) {
    LOGF(ERROR) << "Kiosk Vision library load error: " << load_error.ToString();
  }
}

void KioskVisionLibrary::LoadFunctions() {
  if (!library_handle_) {
    return;
  }

  create_fn_ =
      reinterpret_cast<cros_kiosk_vision_CreateKioskAudienceMeasurementFn>(
          base::GetFunctionPointerFromNativeLibrary(
              library_handle_,
              "cros_kiosk_vision_CreateKioskAudienceMeasurement"));
  delete_fn_ =
      reinterpret_cast<cros_kiosk_vision_DeleteKioskAudienceMeasurementFn>(
          base::GetFunctionPointerFromNativeLibrary(
              library_handle_,
              "cros_kiosk_vision_DeleteKioskAudienceMeasurement"));
  get_properties_fn_ =
      reinterpret_cast<cros_kiosk_vision_GetDetectorInputPropertiesFn>(
          base::GetFunctionPointerFromNativeLibrary(
              library_handle_, "cros_kiosk_vision_GetDetectorInputProperties"));
  process_frame_fn_ = reinterpret_cast<cros_kiosk_vision_ProcessFrameFn>(
      base::GetFunctionPointerFromNativeLibrary(
          library_handle_, "cros_kiosk_vision_ProcessFrame"));
  wait_until_idle_fn_ = reinterpret_cast<cros_kiosk_vision_WaitUntilIdleFn>(
      base::GetFunctionPointerFromNativeLibrary(
          library_handle_, "cros_kiosk_vision_WaitUntilIdle"));
}

bool KioskVisionLibrary::AllHandlesLoaded() const {
  return (library_handle_ != nullptr) && (create_fn_ != nullptr) &&
         (delete_fn_ != nullptr) && (get_properties_fn_ != nullptr) &&
         (process_frame_fn_ != nullptr) && (wait_until_idle_fn_ != nullptr);
}

}  // namespace cros
