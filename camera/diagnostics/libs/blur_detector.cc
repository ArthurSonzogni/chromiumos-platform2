// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/libs/blur_detector.h"

#include <base/native_library.h>
#include <libblurdetector/blur_detector_bindings.h>

#include "cros-camera/common.h"

namespace {

constexpr char kLibraryName[] = "libblurdetector.so";

base::NativeLibrary g_library = nullptr;
cros_camera_CreateBlurDetectorFn g_create_fn = nullptr;
cros_camera_DeleteBlurDetectorFn g_delete_fn = nullptr;
cros_camera_DirtyLensProbabilityFromNV12Fn g_dirty_lens_probability_nv12_fn =
    nullptr;

bool EnsureLibraryLoaded(const base::FilePath& dlc_root_path) {
  if (g_library) {
    return true;
  }

  CHECK(!dlc_root_path.empty());
  base::FilePath lib_path = dlc_root_path.Append(kLibraryName);
  LOGF(INFO) << "Loading blur detector library: " << lib_path;

  base::NativeLibraryOptions native_library_options;
  base::NativeLibraryLoadError load_error;
  native_library_options.prefer_own_symbols = true;
  g_library = base::LoadNativeLibraryWithOptions(
      lib_path, native_library_options, &load_error);

  if (!g_library) {
    LOGF(ERROR) << "Blur detector library load error: "
                << load_error.ToString();
    return false;
  }

  g_create_fn = reinterpret_cast<cros_camera_CreateBlurDetectorFn>(
      base::GetFunctionPointerFromNativeLibrary(
          g_library, "cros_camera_CreateBlurDetector"));
  g_delete_fn = reinterpret_cast<cros_camera_DeleteBlurDetectorFn>(
      base::GetFunctionPointerFromNativeLibrary(
          g_library, "cros_camera_DeleteBlurDetector"));
  g_dirty_lens_probability_nv12_fn =
      reinterpret_cast<cros_camera_DirtyLensProbabilityFromNV12Fn>(
          base::GetFunctionPointerFromNativeLibrary(
              g_library, "cros_camera_DirtyLensProbabilityFromNV12"));

  bool load_ok = (g_create_fn != nullptr) && (g_delete_fn != nullptr) &&
                 (g_dirty_lens_probability_nv12_fn != nullptr);

  if (!load_ok) {
    DVLOGF(1) << "g_create_fn: " << g_create_fn;
    DVLOGF(1) << "g_delete_fn: " << g_delete_fn;
    DVLOGF(1) << "g_dirty_lens_probability_nv12_fn: "
              << g_dirty_lens_probability_nv12_fn;
    g_library = nullptr;
    return false;
  }

  return true;
}

class BlurDetectorImpl : public cros::BlurDetector {
 public:
  BlurDetectorImpl() = default;

  ~BlurDetectorImpl() override {
    if (blur_detector_ && g_delete_fn) {
      g_delete_fn(blur_detector_);
    }
  }

  bool DirtyLensProbabilityFromNV12(const uint8_t* data,
                                    const uint32_t height,
                                    const uint32_t width,
                                    float* dirty_probability) override {
    CHECK(g_dirty_lens_probability_nv12_fn);
    return g_dirty_lens_probability_nv12_fn(blur_detector_, data, height, width,
                                            dirty_probability);
  }

  bool Initialize(const base::FilePath& dlc_root_path) {
    // Load once. Retrying will not help, if it fails the first time.
    static bool library_loaded = EnsureLibraryLoaded(dlc_root_path);
    if (!library_loaded) {
      LOGF(ERROR) << "Error loading blur detector library";
      return false;
    }
    blur_detector_ = g_create_fn();
    if (blur_detector_ == nullptr) {
      return false;
    }
    return true;
  }

 private:
  void* blur_detector_ = nullptr;
};

}  // namespace

namespace cros {

std::unique_ptr<BlurDetector> BlurDetector::Create(
    const base::FilePath& dlc_root_path) {
  auto blur_detector = std::make_unique<BlurDetectorImpl>();
  if (blur_detector->Initialize(dlc_root_path)) {
    return blur_detector;
  }
  return nullptr;
}

}  // namespace cros
