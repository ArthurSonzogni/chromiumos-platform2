/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_GPU_EGL_EGL_DISPLAY_H_
#define CAMERA_GPU_EGL_EGL_DISPLAY_H_

#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace cros {

// Retrieves a list of EGL devices (abstract GPUs) present on the system.
//
// Returns the empty vector if there are none (or the query fails).
std::vector<EGLDeviceEXT> QueryDevices();

// Returns the EGLDisplay corresponding to |device|. It will not be initialized.
// The client must initialize it with eglInitialize(display, &major, &minor).
//
// Returns EGL_NO_DISPLAY on failure.
EGLDisplay GetPlatformDisplayForDevice(EGLDeviceEXT device);

// Returns a vector of all platform displays. It will not be initialized.
// The client must initialize it with eglInitialize(display, &major, &minor).
//
// This function is equivalent to
// [GetPlatformDisplayForDevice(d) for d in QueryDevices()].
std::vector<EGLDisplay> QueryPlatformDisplays();

// Utility function attempts to return an initialized EGL display or
// EGL_NO_DISPLAY on failure.
//
// On non-VM environment, this function first attempts to initialize the default
// display (eglGetDisplay(EGL_DEFAULT_DISPLAY)). If it fails, will attempt to
// initialize the displays in QueryPlatformDisplays() and return the first that
// succeeds. If none of the above succeeds, returns EGL_NO_DISPLAY.
//
// On VM environment, this function attempts to initialize the display from
// QueryPlatformDisplays() and falls back to the default display. This is to
// avoid opening the primary DRM device and taking DRM master, which can block
// Chrome from starting.
//
// On success, eglInitialize will have been called on the returned EGLDisplay.
// Note that further calls to eglInitialize() on an already initialized
// EGLDisplay is a noop.
//
// Unlike other EGL functions, eglInitialize and eglTerminate do not perform
// internal reference counting and the latter will immediately destroy the
// display connection. The EGL_KHR_display_reference extension fixes this but
// is not yet fully supported.
//
// We recommend simply leaking the EGLDisplay because unless you have specific
// lifetime requirements, it is ok to leak the EGLDisplay.
EGLDisplay GetInitializedEglDisplay();

}  // namespace cros

#endif  // CAMERA_GPU_EGL_EGL_DISPLAY_H_
