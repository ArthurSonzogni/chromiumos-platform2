// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/system/libdrm_util_impl.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {

namespace {

constexpr uint32_t INVALID_ENCODER_ID = 0;

}  // namespace

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

    if (connector->connector_type == DRM_MODE_CONNECTOR_eDP ||
        connector->connector_type == DRM_MODE_CONNECTOR_VIRTUAL ||
        connector->connector_type == DRM_MODE_CONNECTOR_LVDS ||
        connector->connector_type == DRM_MODE_CONNECTOR_DSI) {
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

std::vector<uint32_t> LibdrmUtilImpl::GetExternalDisplayConnectorID() {
  return dp_connector_ids;
}

void LibdrmUtilImpl::FillPrivacyScreenInfo(const uint32_t connector_id,
                                           bool* privacy_screen_supported,
                                           bool* privacy_screen_enabled) {
  ScopedDrmModeConnectorPtr connector(
      drmModeGetConnector(device_file.GetPlatformFile(), connector_id));
  if (!connector)
    return;

  *privacy_screen_supported = false;
  *privacy_screen_enabled = false;

  ScopedDrmPropertyPtr hw_prop;
  ScopedDrmPropertyPtr sw_prop;
  int hw_idx = GetDrmProperty(connector, "privacy-screen hw-state", &hw_prop);
  int sw_idx = GetDrmProperty(connector, "privacy-screen sw-state", &sw_prop);

  // Both hw-state and sw-state should exist to indicate we support this
  // feature.
  // Only hw-state indicate the status of ePrivacyScreen.
  //
  // Reference: chromium/src/ui/ozone/platform/drm/common/drm_util.cc
  if (hw_idx >= 0 && sw_idx >= 0) {
    *privacy_screen_supported = true;
    auto hw_enum_name = GetEnumName(hw_prop, connector->prop_values[hw_idx]);
    if (hw_enum_name == "Enabled" || hw_enum_name == "Enabled-locked") {
      *privacy_screen_enabled = true;
    }
    return;
  }

  // Fallback to legacy version.
  ScopedDrmPropertyPtr legacy_prop;
  int idx = GetDrmProperty(connector, "privacy-screen", &legacy_prop);
  if (idx >= 0) {
    *privacy_screen_supported = true;
    *privacy_screen_enabled = connector->prop_values[idx] == 1;
  }
}

std::string LibdrmUtilImpl::GetEnumName(const ScopedDrmPropertyPtr& prop,
                                        uint32_t value) {
  if (!prop)
    return std::string();

  for (int i = 0; i < prop->count_enums; ++i)
    if (prop->enums[i].value == value)
      return prop->enums[i].name;

  return std::string();
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

ScopedDrmModeCrtcPtr LibdrmUtilImpl::GetDrmCrtc(const uint32_t connector_id) {
  ScopedDrmModeConnectorPtr connector(
      drmModeGetConnector(device_file.GetPlatformFile(), connector_id));
  // Sometimes there is no crtc info, for example, when the device hibernate,
  // the screen is black, there is no need to render, so the encoder id is
  // invalid as 0.
  if (!connector || connector->encoder_id == INVALID_ENCODER_ID)
    return nullptr;

  ScopedDrmModeEncoderPtr encoder(
      drmModeGetEncoder(device_file.GetPlatformFile(), connector->encoder_id));
  if (!encoder)
    return nullptr;

  return ScopedDrmModeCrtcPtr(
      drmModeGetCrtc(device_file.GetPlatformFile(), encoder->crtc_id));
}

bool LibdrmUtilImpl::FillDisplaySize(const uint32_t connector_id,
                                     uint32_t* width,
                                     uint32_t* height) {
  ScopedDrmModeConnectorPtr connector(
      drmModeGetConnector(device_file.GetPlatformFile(), connector_id));
  if (!connector)
    return false;

  *width = connector->mmWidth;
  *height = connector->mmHeight;
  return true;
}

bool LibdrmUtilImpl::FillDisplayResolution(const uint32_t connector_id,
                                           uint32_t* horizontal,
                                           uint32_t* vertical) {
  auto crtc = GetDrmCrtc(connector_id);
  if (crtc) {
    *horizontal = crtc->mode.hdisplay;
    *vertical = crtc->mode.vdisplay;
    return true;
  } else {
    // Fall back to use the preferred mode info in connector.
    ScopedDrmModeConnectorPtr connector(
        drmModeGetConnector(device_file.GetPlatformFile(), connector_id));
    if (!connector)
      return false;
    for (int i = 0; i < connector->count_modes; ++i) {
      if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
        *horizontal = connector->modes[i].hdisplay;
        *vertical = connector->modes[i].vdisplay;
        return true;
      }
    }
  }

  return false;
}

bool LibdrmUtilImpl::FillDisplayRefreshRate(const uint32_t connector_id,
                                            double* refresh_rate) {
  auto crtc = GetDrmCrtc(connector_id);
  if (crtc && crtc->mode.htotal && crtc->mode.vtotal) {
    // |crtc->mode.vrefresh| indicates the refresh rate, however, it stores in
    // |uint32_t| type which loses the accuracy.
    //
    // The following calculation refers to the implementation in |modetest|
    // command line tool. In Chrome side, they also use the same method to
    // calculate it.
    *refresh_rate =
        crtc->mode.clock * 1000.0 / (crtc->mode.htotal * crtc->mode.vtotal);
    return true;
  } else {
    // Fall back to use the preferred mode info in connector.
    ScopedDrmModeConnectorPtr connector(
        drmModeGetConnector(device_file.GetPlatformFile(), connector_id));
    if (!connector)
      return false;
    for (int i = 0; i < connector->count_modes; ++i) {
      if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
        *refresh_rate =
            connector->modes[i].clock * 1000.0 /
            (connector->modes[i].htotal * connector->modes[i].vtotal);
        return true;
      }
    }
  }

  return false;
}

ScopedDrmPropertyBlobPtr LibdrmUtilImpl::GetDrmPropertyBlob(
    const uint32_t connector_id, const std::string& name) {
  ScopedDrmModeConnectorPtr connector(
      drmModeGetConnector(device_file.GetPlatformFile(), connector_id));
  if (!connector)
    return nullptr;

  ScopedDrmPropertyPtr prop;
  int idx = GetDrmProperty(connector, name, &prop);
  if (idx >= 0 && prop->flags & DRM_MODE_PROP_BLOB) {
    return ScopedDrmPropertyBlobPtr(drmModeGetPropertyBlob(
        device_file.GetPlatformFile(), connector->prop_values[idx]));
  }
  return nullptr;
}

bool LibdrmUtilImpl::FillEdidInfo(const uint32_t connector_id, EdidInfo* info) {
  auto blob = GetDrmPropertyBlob(connector_id, "EDID");
  if (!blob || !blob->length)
    return false;

  std::vector<uint8_t> blob_data(
      reinterpret_cast<uint8_t*>(blob->data),
      reinterpret_cast<uint8_t*>(blob->data) + blob->length);
  auto edid_info = Edid::From(blob_data);
  if (edid_info.has_value()) {
    *info = std::move(edid_info.value());
    return true;
  }

  return false;
}

}  // namespace diagnostics
