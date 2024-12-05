// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCREEN_CAPTURE_UTILS_CRTC_H_
#define SCREEN_CAPTURE_UTILS_CRTC_H_

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file.h>

#include "screen-capture-utils/ptr_util.h"

namespace screenshot {

enum class ColorEncoding {
  kYCbCrBT601,
  kYCbCrBT709,
  kYCbCrBT2020,
  kUnknown,
};

enum class ColorRange {
  kYCbCrLimited,
  kYCbCrFull,
  kUnknown,
};

struct PlaneConfiguration {
  float crop_x;
  float crop_y;
  float crop_w;
  float crop_h;

  // |x|, |y|, |w|, and |h| describe the destination rectangle.
  int32_t x;
  int32_t y;
  uint32_t w;
  uint32_t h;

  // |color_encoding| is optional: it will be kUnknown if the driver does not
  // provide the COLOR_ENCODING DRM property for the plane. It can also be
  // kUnknown if the driver reports it, but the value can't be associated with a
  // ColorEncoding value.
  ColorEncoding color_encoding;

  // |color_range| is optional: it will be kUnknown if the driver does not
  // provide the COLOR_RANGE DRM property for the plane. It can also be kUnknown
  // if the driver reports it, but the value can't be associated with a
  // ColorRange value.
  ColorRange color_range;
};

enum class PanelRotation { k0, k90, k180, k270 };

class Crtc {
 public:
  using PlaneInfo = std::pair<ScopedDrmModeFB2Ptr, PlaneConfiguration>;

  Crtc(base::File file,
       ScopedDrmModeConnectorPtr connector,
       ScopedDrmModeEncoderPtr encoder,
       ScopedDrmModeCrtcPtr crtc,
       ScopedDrmModeFB2Ptr fb2,
       ScopedDrmPlaneResPtr plane_res,
       PanelRotation panel_orientation);

  Crtc(const Crtc&) = delete;
  Crtc& operator=(const Crtc&) = delete;

  const base::File& file() const { return file_; }
  drmModeConnector* connector() const { return connector_.get(); }
  drmModeEncoder* encoder() const { return encoder_.get(); }
  drmModeCrtc* crtc() const { return crtc_.get(); }

  drmModeFB2* fb2() const { return fb2_.get(); }

  uint32_t width() const { return crtc_->width; }
  uint32_t height() const { return crtc_->height; }

  PanelRotation panel_orientation() const { return panel_orientation_; }

  bool IsInternalDisplay() const;
  std::vector<Crtc::PlaneInfo> GetConnectedPlanes() const;

 private:
  // File descriptor for the DRM device.
  base::File file_;
  ScopedDrmModeConnectorPtr connector_;
  ScopedDrmModeEncoderPtr encoder_;
  ScopedDrmModeCrtcPtr crtc_;
  ScopedDrmModeFB2Ptr fb2_;
  ScopedDrmPlaneResPtr plane_res_;
  const PanelRotation panel_orientation_;
};

class CrtcFinder final {
 public:
  enum class Spec {
    kAnyDisplay = 0,
    kInternalDisplay,
    kExternalDisplay,
    kById,
  };

  CrtcFinder() = default;
  ~CrtcFinder() = default;

  inline void SetSpec(Spec spec) { spec_ = spec; }
  inline void SetCrtcId(uint32_t crtc_id) { crtc_id_ = crtc_id; }

  std::unique_ptr<Crtc> Find() const;

 private:
  bool MatchesSpec(const Crtc* crtc) const;

  Spec spec_ = Spec::kAnyDisplay;
  uint32_t crtc_id_;
};

}  // namespace screenshot

#endif  // SCREEN_CAPTURE_UTILS_CRTC_H_
