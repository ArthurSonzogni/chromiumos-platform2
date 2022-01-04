// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_IMPL_H_

#include <memory>
#include <string>
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

struct DrmModePropertyDeleter {
  void operator()(drmModePropertyRes* prop) { drmModeFreeProperty(prop); }
};

class LibdrmUtilImpl : public LibdrmUtil {
 public:
  LibdrmUtilImpl();
  LibdrmUtilImpl(const LibdrmUtilImpl& oth) = delete;
  LibdrmUtilImpl(LibdrmUtilImpl&& oth) = delete;
  ~LibdrmUtilImpl() override;

  bool Initialize() override;
  uint32_t GetEmbeddedDisplayConnectorID() override;
  void FillPrivacyScreenInfo(const uint32_t connector_id,
                             bool* privacy_screen_supported,
                             bool* privacy_screen_enabled) override;

 private:
  using ScopedDrmModeResPtr = std::unique_ptr<drmModeRes, DrmModeResDeleter>;
  using ScopedDrmModeConnectorPtr =
      std::unique_ptr<drmModeConnector, DrmModeConnectorDeleter>;
  using ScopedDrmPropertyPtr =
      std::unique_ptr<drmModePropertyRes, DrmModePropertyDeleter>;

 private:
  // This function iterates all the properties in |connector| and find the
  // property with |name|. When it finds it, it stores the property into |prop|
  // and return its index. If it fails to find it, -1 is returned.
  int GetDrmProperty(const ScopedDrmModeConnectorPtr& connector,
                     const std::string& name,
                     ScopedDrmPropertyPtr* prop);

  base::File device_file;
  uint32_t edp_connector_id;
  std::vector<uint32_t> dp_connector_ids;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_IMPL_H_
