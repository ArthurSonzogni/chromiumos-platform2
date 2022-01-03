// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/system/libdrm_util_impl.h"

namespace diagnostics {

LibdrmUtilImpl::LibdrmUtilImpl() {}

LibdrmUtilImpl::~LibdrmUtilImpl() {}

bool LibdrmUtilImpl::Initialize() {
  // Find valid device.
  ScopedDrmModeResPtr resource;
  base::FileEnumerator lister(base::FilePath("/dev/dri"), false,
                              base::FileEnumerator::FILES, "card?");
  for (base::FilePath path = lister.Next(); !path.empty();
       path = lister.Next()) {
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                              base::File::FLAG_WRITE);
    if (!file.IsValid())
      continue;

    resource.reset(drmModeGetResources(file.GetPlatformFile()));
    // Usually, there is only one card with valid drm resources.
    // In Chrome side, |drm_util.cc| also uses |ioctl| to find the first card
    // with valid drm resources.
    if (resource) {
      device_file = std::move(file);
      break;
    }
  }

  if (!resource)
    return false;

  // Find connected connectors.
  for (int i = 0; i < resource->count_connectors; ++i) {
    ScopedDrmModeConnectorPtr connector(drmModeGetConnector(
        device_file.GetPlatformFile(), resource->connectors[i]));

    if (!connector || connector->connection == DRM_MODE_DISCONNECTED)
      continue;

    if (connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
      edp_connector_id = resource->connectors[i];
    } else {
      dp_connector_ids.push_back(resource->connectors[i]);
    }
  }

  return true;
}

}  // namespace diagnostics
