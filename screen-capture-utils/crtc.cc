// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen-capture-utils/crtc.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/stl_util.h>

namespace screenshot {

namespace {

constexpr const char kDrmDeviceDir[] = "/dev/dri";
constexpr const char kDrmDeviceGlob[] = "card?";

float FixedPoint1616ToFloat(uint32_t n) {
  float result = (n & 0xFFFF0000) >> 16;
  result += (n & 0xFFFF) / 65536.0f;
  return result;
}

// Translates |property_value| to a name assuming that it corresponds to the
// value of an enum DRM property described by |property_metadata|. This function
// crashes if |property_metadata| does not correspond to an enum property or if
// |property_value| is not a valid enum value.
std::string DRMEnumValueToString(const drmModePropertyRes& property_metadata,
                                 uint64_t property_value) {
  LOG_ASSERT(property_metadata.flags & DRM_MODE_PROP_ENUM)
      << "DRMEnumValueToString() is being called for a non-enum property";

  std::optional<std::string> drm_enum_name;
  for (int i = 0; i < property_metadata.count_enums; i++) {
    const drm_mode_property_enum& drm_enum = property_metadata.enums[i];
    if (drm_enum.value == property_value) {
      drm_enum_name = std::string(drm_enum.name);
      break;
    }
  }

  LOG_ASSERT(drm_enum_name.has_value())
      << "|property_value| does not correspond to a valid enum value";
  return drm_enum_name.value();
}

// Assuming that |property_value| is the value of the COLOR_ENCODING DRM
// property described by |property_metadata|, this function converts that value
// to a ColorEncoding enum value.
ColorEncoding DRMColorEncodingPropertyToEnum(
    const drmModePropertyRes& property_metadata, uint64_t property_value) {
  LOG_ASSERT(strcmp(property_metadata.name, "COLOR_ENCODING") == 0);

  const std::string drm_enum_name =
      DRMEnumValueToString(property_metadata, property_value);
  if (drm_enum_name == "ITU-R BT.601 YCbCr") {
    return ColorEncoding::kYCbCrBT601;
  } else if (drm_enum_name == "ITU-R BT.709 YCbCr") {
    return ColorEncoding::kYCbCrBT709;
  } else if (drm_enum_name == "ITU-R BT.2020 YCbCr") {
    return ColorEncoding::kYCbCrBT2020;
  }

  return ColorEncoding::kUnknown;
}

// Assuming that |property_value| is the value of the COLOR_RANGE DRM property
// described by |property_metadata|, this function converts that value to a
// ColorRange enum value.
ColorRange DRMColorRangePropertyToEnum(
    const drmModePropertyRes& property_metadata, uint64_t property_value) {
  LOG_ASSERT(strcmp(property_metadata.name, "COLOR_RANGE") == 0);

  const std::string drm_enum_name =
      DRMEnumValueToString(property_metadata, property_value);
  if (drm_enum_name == "YCbCr limited range") {
    return ColorRange::kYCbCrLimited;
  } else if (drm_enum_name == "YCbCr full range") {
    return ColorRange::kYCbCrFull;
  }

  return ColorRange::kUnknown;
}

bool PopulatePlaneConfiguration(int fd,
                                uint32_t plane_id,
                                PlaneConfiguration* conf) {
  // TODO(andrescj): Handle rotation.
  static const std::array<std::string, 8> required_props{
      "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H",
      "SRC_X",  "SRC_Y",  "SRC_W",  "SRC_H",
  };
  static const std::array<std::string, 2> optional_props{
      "COLOR_ENCODING",
      "COLOR_RANGE",
  };

  std::map<std::string,
           std::pair<ScopedDrmPropertyPtr /* metadata */, uint64_t /* value */>>
      interesting_props;

  ScopedDrmObjectPropertiesPtr props(
      drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE));
  if (!props) {
    return false;
  }

  size_t num_required_props_found = 0;
  for (int i = 0; i < props->count_props; i++) {
    ScopedDrmPropertyPtr prop(drmModeGetProperty(fd, props->props[i]));
    if (!prop) {
      continue;
    }

    const std::string prop_name(prop->name);

    if (std::find(required_props.cbegin(), required_props.cend(), prop_name) !=
        required_props.cend()) {
      num_required_props_found++;
    } else if (std::find(optional_props.cbegin(), optional_props.cend(),
                         prop_name) == optional_props.cend()) {
      // We don't care about this property as it's neither required nor
      // optional.
      continue;
    }

    if (!interesting_props
             .emplace(prop_name,
                      std::make_pair(/*metadata=*/std::move(prop),
                                     /*value=*/props->prop_values[i]))
             .second) {
      LOG(ERROR) << "Detected a duplicate property";
      return false;
    }
  }

  if (num_required_props_found != required_props.size()) {
    LOG(ERROR) << "Could not find all required properties";
    return false;
  }

  conf->x = static_cast<int32_t>(interesting_props["CRTC_X"].second);
  conf->y = static_cast<int32_t>(interesting_props["CRTC_Y"].second);
  conf->w = static_cast<uint32_t>(interesting_props["CRTC_W"].second);
  conf->h = static_cast<uint32_t>(interesting_props["CRTC_H"].second);
  conf->crop_x = FixedPoint1616ToFloat(interesting_props["SRC_X"].second);
  conf->crop_y = FixedPoint1616ToFloat(interesting_props["SRC_Y"].second);
  conf->crop_w = FixedPoint1616ToFloat(interesting_props["SRC_W"].second);
  conf->crop_h = FixedPoint1616ToFloat(interesting_props["SRC_H"].second);

  // While the COLOR_ENCODING and COLOR_RANGE properties are optional, we do
  // expect consistency: either both are present or both are absent.
  if (interesting_props.count("COLOR_ENCODING") !=
      interesting_props.count("COLOR_RANGE")) {
    LOG(ERROR)
        << "Detected an inconsistency between the COLOR_ENCODING and the "
           "COLOR_RANGE properties";
    return false;
  }

  conf->color_encoding = ColorEncoding::kUnknown;
  if (interesting_props.count("COLOR_ENCODING")) {
    const auto color_encoding_prop_it =
        interesting_props.find("COLOR_ENCODING");
    conf->color_encoding = DRMColorEncodingPropertyToEnum(
        /*property_metadata=*/*color_encoding_prop_it->second.first,
        /*property_value=*/color_encoding_prop_it->second.second);
  }

  conf->color_range = ColorRange::kUnknown;
  if (interesting_props.count("COLOR_RANGE")) {
    const auto color_range_prop_it = interesting_props.find("COLOR_RANGE");
    conf->color_range = DRMColorRangePropertyToEnum(
        /*property_metadata=*/*color_range_prop_it->second.first,
        /*property_value=*/color_range_prop_it->second.second);
  }

  return true;
}

PanelRotation GetPanelOrientation(int fd, uint32_t connector_id) {
  ScopedDrmObjectPropertiesPtr props(
      drmModeObjectGetProperties(fd, connector_id, DRM_MODE_OBJECT_CONNECTOR));
  if (!props) {
    return PanelRotation::k0;
  }

  for (uint32_t p = 0; p < props->count_props; p++) {
    ScopedDrmPropertyPtr prop(drmModeGetProperty(fd, props->props[p]));
    if (!prop) {
      continue;
    }

    if (strcmp("panel orientation", prop->name) == 0) {
      /* enum is internal to the kernel and not exposed.
      enum drm_panel_orientation {
        DRM_MODE_PANEL_ORIENTATION_UNKNOWN = -1,
        DRM_MODE_PANEL_ORIENTATION_NORMAL = 0,
        DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP,
        DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
        DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
      };
      */

      switch (props->prop_values[p]) {
        case 0:
          VLOG(1) << "panel orientation 0 degrees.";
          return PanelRotation::k0;
        case 1:
          VLOG(1) << "panel orientation 180 degrees.";
          return PanelRotation::k180;
        case 2:
          VLOG(1) << "panel orientation 270 degrees.";
          return PanelRotation::k270;
        case 3:
          VLOG(1) << "panel orientation 90 degrees.";
          return PanelRotation::k90;
        default:
          VLOG(1) << "unable to detect panel orientation, using 0 degrees.";
          return PanelRotation::k0;
      }
    }
  }

  return PanelRotation::k0;
}

std::vector<std::unique_ptr<Crtc>> GetConnectedCrtcs() {
  std::vector<std::unique_ptr<Crtc>> crtcs;

  std::vector<base::FilePath> paths;
  {
    base::FileEnumerator lister(base::FilePath(kDrmDeviceDir), false,
                                base::FileEnumerator::FILES, kDrmDeviceGlob);
    for (base::FilePath name = lister.Next(); !name.empty();
         name = lister.Next()) {
      paths.emplace_back(name);
    }
  }
  std::sort(paths.begin(), paths.end());

  for (base::FilePath path : paths) {
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                              base::File::FLAG_WRITE);
    if (!file.IsValid()) {
      continue;
    }

    // Set CAP_ATOMIC so we can query all planes and plane properties.
    // TODO(b/290543296): Revisit if we still need this check after Hana EOL.
    bool atomic_modeset =
        drmSetClientCap(file.GetPlatformFile(), DRM_CLIENT_CAP_ATOMIC, 1) == 0;

    ScopedDrmModeResPtr resources(drmModeGetResources(file.GetPlatformFile()));
    if (!resources) {
      continue;
    }

    for (int index_connector = 0; index_connector < resources->count_connectors;
         ++index_connector) {
      ScopedDrmModeConnectorPtr connector(drmModeGetConnector(
          file.GetPlatformFile(), resources->connectors[index_connector]));
      if (!connector || connector->encoder_id == 0) {
        continue;
      }

      ScopedDrmModeEncoderPtr encoder(
          drmModeGetEncoder(file.GetPlatformFile(), connector->encoder_id));
      if (!encoder || encoder->crtc_id == 0) {
        continue;
      }

      ScopedDrmModeCrtcPtr crtc(
          drmModeGetCrtc(file.GetPlatformFile(), encoder->crtc_id));
      if (!crtc || !crtc->mode_valid || crtc->buffer_id == 0) {
        continue;
      }

      ScopedDrmModeFB2Ptr fb2(
          drmModeGetFB2(file.GetPlatformFile(), crtc->buffer_id),
          file.GetPlatformFile());

      if (!fb2) {
        LOG(ERROR) << "getfb2 failed";
        continue;
      }

      const PanelRotation panel_orientation = GetPanelOrientation(
          file.GetPlatformFile(), resources->connectors[index_connector]);

      std::unique_ptr<Crtc> res_crtc;

      // Keep around a file for next display if needed.
      base::File file_dup = file.Duplicate();
      if (!file_dup.IsValid()) {
        continue;
      }

      // Multiplane is only supported when atomic_modeset is available. Obtain
      // the |plane_res_| for later use.
      if (atomic_modeset) {
        ScopedDrmPlaneResPtr plane_res(
            drmModeGetPlaneResources(file.GetPlatformFile()));
        CHECK(plane_res) << " Failed to get plane resources";
        res_crtc = std::make_unique<Crtc>(std::move(file), std::move(connector),
                                          std::move(encoder), std::move(crtc),
                                          std::move(fb2), std::move(plane_res),
                                          panel_orientation);
      } else {
        res_crtc = std::make_unique<Crtc>(
            std::move(file), std::move(connector), std::move(encoder),
            std::move(crtc), std::move(fb2), nullptr, panel_orientation);
      }

      file = std::move(file_dup);
      crtcs.emplace_back(std::move(res_crtc));
    }
  }

  return crtcs;
}

}  // namespace

Crtc::Crtc(base::File file,
           ScopedDrmModeConnectorPtr connector,
           ScopedDrmModeEncoderPtr encoder,
           ScopedDrmModeCrtcPtr crtc,
           ScopedDrmModeFB2Ptr fb2,
           ScopedDrmPlaneResPtr plane_res,
           PanelRotation panel_orientation)
    : file_(std::move(file)),
      connector_(std::move(connector)),
      encoder_(std::move(encoder)),
      crtc_(std::move(crtc)),
      fb2_(std::move(fb2)),
      plane_res_(std::move(plane_res)),
      panel_orientation_(panel_orientation) {}

bool Crtc::IsInternalDisplay() const {
  switch (connector_->connector_type) {
    case DRM_MODE_CONNECTOR_eDP:
    case DRM_MODE_CONNECTOR_LVDS:
    case DRM_MODE_CONNECTOR_DSI:
    case DRM_MODE_CONNECTOR_VIRTUAL:
      return true;
    default:
      return false;
  }
}

std::unique_ptr<Crtc> CrtcFinder::Find() const {
  auto crtcs = GetConnectedCrtcs();
  for (auto& crtc : crtcs) {
    if (MatchesSpec(crtc.get())) {
      return std::move(crtc);
    }
  }
  return nullptr;
}

bool CrtcFinder::MatchesSpec(const Crtc* crtc) const {
  switch (spec_) {
    case Spec::kAnyDisplay:
      return true;
    case Spec::kInternalDisplay:
      return crtc->IsInternalDisplay();
    case Spec::kExternalDisplay:
      return !crtc->IsInternalDisplay();
    case Spec::kById:
      return crtc->crtc()->crtc_id == crtc_id_;
  }
  NOTREACHED_IN_MIGRATION() << "Invalid spec";
  return false;
}

std::vector<Crtc::PlaneInfo> Crtc::GetConnectedPlanes() const {
  CHECK(fb2())
      << "This code path is supported only if drmModeGetFB2() succeeded "
         "for the CRTC.";
  std::vector<Crtc::PlaneInfo> planes;
  if (!plane_res_.get()) {
    // Return the empty list if we decided not to query the plane resources or
    // if doing so failed.
    return planes;
  }
  for (uint32_t i = 0; i < plane_res_->count_planes; i++) {
    ScopedDrmPlanePtr plane(
        drmModeGetPlane(file_.GetPlatformFile(), plane_res_->planes[i]));
    if (plane->crtc_id != crtc_->crtc_id) {
      continue;
    }

    PlaneConfiguration conf{};
    bool res = PopulatePlaneConfiguration(file_.GetPlatformFile(),
                                          plane->plane_id, &conf);
    if (!res) {
      LOG(WARNING) << "Failed to query plane position, skipping.\n";
      continue;
    }
    ScopedDrmModeFB2Ptr fb_info(
        drmModeGetFB2(file_.GetPlatformFile(), plane->fb_id),
        file_.GetPlatformFile());
    if (!fb_info) {
      LOG(WARNING) << "Failed to query plane fb info, skipping.\n";
      continue;
    }
    planes.emplace_back(std::make_pair(std::move(fb_info), conf));
  }
  return planes;
}

}  // namespace screenshot
