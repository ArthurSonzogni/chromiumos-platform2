// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
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

uint32_t LibdrmUtilImpl::GetEmbeddedDisplayConnectorID() {
  return edp_connector_id;
}

void LibdrmUtilImpl::FillPrivacyScreenInfo(const uint32_t connector_id,
                                           bool* privacy_screen_supported,
                                           bool* privacy_screen_enabled) {
  ScopedDrmModeConnectorPtr connector(
      drmModeGetConnector(device_file.GetPlatformFile(), connector_id));
  if (!connector)
    return;

  ScopedDrmPropertyPtr privacy_screen_prop;
  int idx = GetDrmProperty(connector, "privacy-screen", &privacy_screen_prop);
  if (idx >= 0) {
    *privacy_screen_supported = true;
    *privacy_screen_enabled = connector->prop_values[idx] == 1;
  }
}

int LibdrmUtilImpl::GetDrmProperty(const ScopedDrmModeConnectorPtr& connector,
                                   const std::string& name,
                                   ScopedDrmPropertyPtr* prop) {
  if (!connector)
    return -1;

  for (int i = 0; i < connector->count_props; ++i) {
    ScopedDrmPropertyPtr tmp(
        drmModeGetProperty(device_file.GetPlatformFile(), connector->props[i]));
    if (!tmp)
      continue;

    if (name == tmp->name) {
      *prop = std::move(tmp);
      return i;
    }
  }

  return -1;
}

}  // namespace diagnostics
