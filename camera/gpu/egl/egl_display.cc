/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/egl/egl_display.h"

#include <string>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>

#include "cros-camera/common.h"

namespace {

PFNEGLQUERYDEVICESEXTPROC g_eglQueryDevicesEXT = nullptr;
PFNEGLQUERYDEVICESTRINGEXTPROC g_eglQueryDeviceStringEXT = nullptr;
PFNEGLGETPLATFORMDISPLAYEXTPROC g_eglGetPlatformDisplayEXT = nullptr;

bool IsQueryDevicesSupported() {
  static bool supported = []() {
    g_eglQueryDevicesEXT = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
        eglGetProcAddress("eglQueryDevicesEXT"));
    return (g_eglQueryDevicesEXT != nullptr);
  }();
  return supported;
}

bool IsQueryDeviceStringSupported() {
  static bool supported = []() {
    g_eglQueryDeviceStringEXT =
        reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(
            eglGetProcAddress("eglQueryDeviceStringEXT"));
    return (g_eglQueryDeviceStringEXT != nullptr);
  }();
  return supported;
}

bool IsGetPlatformDisplaySupported() {
  static bool supported = []() {
    g_eglGetPlatformDisplayEXT =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    return (g_eglGetPlatformDisplayEXT != nullptr);
  }();
  return supported;
}

bool IsRunningOnVM() {
  static bool is_in_qemu_vm = []() {
    const base::FilePath sys_vendor_path(
        "/sys/devices/virtual/dmi/id/sys_vendor");
    std::string raw_str, vendor_str;
    constexpr size_t kMaxStrSize = 128;
    base::ReadFileToStringWithMaxSize(sys_vendor_path, &raw_str, kMaxStrSize);
    base::TrimWhitespaceASCII(raw_str, base::TRIM_ALL, &vendor_str);
    return vendor_str == "QEMU";
  }();

  static bool is_in_gce_vm = []() {
    const base::FilePath board_name_path(
        "/sys/devices/virtual/dmi/id/board_name");
    std::string raw_str, board_name_str;
    constexpr size_t kMaxStrSize = 128;
    base::ReadFileToStringWithMaxSize(board_name_path, &raw_str, kMaxStrSize);
    base::TrimWhitespaceASCII(raw_str, base::TRIM_ALL, &board_name_str);
    return board_name_str == "Google Compute Engine";
  }();

  return is_in_qemu_vm || is_in_gce_vm;
}

}  // namespace

namespace cros {

std::vector<EGLDeviceEXT> QueryDevices() {
  if (!IsQueryDevicesSupported()) {
    return std::vector<EGLDeviceEXT>();
  }

  EGLint num_devices;
  bool status = g_eglQueryDevicesEXT(/*max_devices=*/0, /*devices=*/nullptr,
                                     &num_devices);
  if (status != EGL_TRUE) {
    LOGF(ERROR) << "eglQueryDevicesEXT failed.";
    return std::vector<EGLDeviceEXT>();
  } else if (num_devices == 0) {
    LOGF(ERROR) << "eglQueryDevicesEXT returned 0 devices.";
    return std::vector<EGLDeviceEXT>();
  }

  std::vector<EGLDeviceEXT> devices(num_devices);
  status = g_eglQueryDevicesEXT(/*max_devices=*/num_devices, devices.data(),
                                &num_devices);
  if (status != EGL_TRUE) {
    LOGF(ERROR) << "eglQueryDevicesEXT failed.";
    return std::vector<EGLDeviceEXT>();
  }
  if (IsQueryDeviceStringSupported()) {
    for (auto* d : devices) {
      const char* drm_device =
          g_eglQueryDeviceStringEXT(d, EGL_DRM_DEVICE_FILE_EXT);
      DVLOGF(1) << "EGL Device: "
                << g_eglQueryDeviceStringEXT(d, EGL_EXTENSIONS)
                << " DRM device: " << (drm_device ? drm_device : "n/a");
    }
  }
  return devices;
}

EGLDisplay GetPlatformDisplayForDevice(EGLDeviceEXT device) {
  if (!IsGetPlatformDisplaySupported()) {
    return EGL_NO_DISPLAY;
  }

  EGLDisplay display =
      g_eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, device,
                                 /*attrib_list=*/nullptr);
  if (eglGetError() == EGL_SUCCESS && display != EGL_NO_DISPLAY) {
    return display;
  } else {
    LOGF(ERROR) << "eglGetPlatformDisplayEXT() failed on device: " << device;
    return EGL_NO_DISPLAY;
  }
}

std::vector<EGLDisplay> QueryPlatformDisplays() {
  const std::vector<EGLDeviceEXT> devices = QueryDevices();
  std::vector<EGLDisplay> displays;
  displays.reserve(devices.size());
  for (const EGLDeviceEXT device : devices) {
    displays.push_back(GetPlatformDisplayForDevice(device));
    if (IsQueryDeviceStringSupported()) {
      const char* drm_device =
          g_eglQueryDeviceStringEXT(device, EGL_DRM_DEVICE_FILE_EXT);
      DVLOGF(1) << "EGL Device: "
                << g_eglQueryDeviceStringEXT(device, EGL_EXTENSIONS)
                << " DRM device: " << (drm_device ? drm_device : "n/a")
                << " EGL display: " << displays.back();
    }
  }
  return displays;
}

EGLDisplay GetInitializedEglDisplay() {
  auto get_initialized_default_display = [&]() -> EGLDisplay {
    // Attempt to initialize the default display.
    EGLDisplay egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglInitialize(egl_display, /*major=*/nullptr, /*minor=*/nullptr) ==
        EGL_TRUE) {
      DVLOGF(1) << "Initialized default EGL display";
      return egl_display;
    }
    return EGL_NO_DISPLAY;
  };

  auto get_initialized_platform_display = [&]() -> EGLDisplay {
    // Iterate over all platform displays and attempt to initialize one of them.
    for (EGLDisplay egl_display : QueryPlatformDisplays()) {
      if (eglInitialize(egl_display, /*major=*/nullptr, /*minor=*/nullptr) ==
          EGL_TRUE) {
        DVLOGF(1) << "Initialized EGL display: " << egl_display;
        return egl_display;
      }
    }
    return EGL_NO_DISPLAY;
  };

  EGLDisplay egl_display = EGL_NO_DISPLAY;
  if (IsRunningOnVM()) {
    // For some reason the default EGL display on VM would open the primary
    // device and take DRM master, which can stop Chrome from starting.
    LOGF(INFO) << "Running on VM; try using platform display first";
    egl_display = get_initialized_platform_display();
    if (egl_display == EGL_NO_DISPLAY) {
      LOGF(INFO)
          << "Cannot initialize platform platform; fallback to default display";
      egl_display = get_initialized_default_display();
    }
  } else {
    // For non-VM devices, initialize the default display and fall back to
    // platform display.
    egl_display = get_initialized_default_display();
    if (egl_display == EGL_NO_DISPLAY) {
      egl_display = get_initialized_platform_display();
    }
  }

  if (egl_display == EGL_NO_DISPLAY) {
    LOGF(ERROR) << "Failed to initialize any EGL display.";
  }
  return egl_display;
}

}  // namespace cros
