// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_IMPL_H_

#include <vector>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <base/files/file.h>

#include "diagnostics/cros_healthd/system/libdrm_util.h"

namespace diagnostics {

struct DrmModeResDeleter {
  void operator()(drmModeRes* resources) { drmModeFreeResources(resources); }
};

struct DrmModeConnectorDeleter {
  void operator()(drmModeConnector* connector) {
    drmModeFreeConnector(connector);
  }
};

class LibdrmUtilImpl : public LibdrmUtil {
 public:
  LibdrmUtilImpl();
  LibdrmUtilImpl(const LibdrmUtilImpl& oth) = delete;
  LibdrmUtilImpl(LibdrmUtilImpl&& oth) = delete;
  ~LibdrmUtilImpl() override;

  bool Initialize() override;

 private:
  using ScopedDrmModeResPtr = std::unique_ptr<drmModeRes, DrmModeResDeleter>;
  using ScopedDrmModeConnectorPtr =
      std::unique_ptr<drmModeConnector, DrmModeConnectorDeleter>;

  base::File device_file;
  uint32_t edp_connector_id;
  std::vector<uint32_t> dp_connector_ids;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_IMPL_H_
