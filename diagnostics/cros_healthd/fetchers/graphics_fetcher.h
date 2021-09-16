// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_GRAPHICS_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_GRAPHICS_FETCHER_H_

#include <memory>

#include <base/optional.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

// Include here to avoid notoriously problematic X code library. In X code, they
// define |Status| as |int|, it breaks many code.
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace diagnostics {

class EglManager {
 public:
  EglManager(const EglManager&) = delete;
  EglManager& operator=(const EglManager&) = delete;
  ~EglManager();

  static std::unique_ptr<EglManager> Create();
  chromeos::cros_healthd::mojom::GLESInfoPtr FetchGLESInfo();

 private:
  EglManager() = default;

 private:
  EGLDisplay egl_display_;
  EGLContext egl_context_;
};

// The GraphicsFetcher class is responsible for gathering graphics info reported
// by cros_healthd.
class GraphicsFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns a structure with either the device's graphics data or the error
  // that occurred fetching the information.
  chromeos::cros_healthd::mojom::GraphicsResultPtr FetchGraphicsInfo(
      std::unique_ptr<EglManager> egl_manager = nullptr);

 private:
  using OptionalProbeErrorPtr =
      base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>;

  OptionalProbeErrorPtr FetchGraphicsInfo(
      std::unique_ptr<EglManager> egl_manager,
      chromeos::cros_healthd::mojom::GLESInfoPtr* out_gles_info);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_GRAPHICS_FETCHER_H_
