// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_FAKE_DISPLAY_UTIL_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_FAKE_DISPLAY_UTIL_H_

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "diagnostics/cros_healthd/delegate/utils/display_util.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

class FakeDisplayUtil : public DisplayUtil {
 public:
  struct FakePrivacyScreenInfo {
    bool supported;
    bool enabled;
  };

  FakeDisplayUtil();
  FakeDisplayUtil(const FakeDisplayUtil&) = delete;
  FakeDisplayUtil(FakeDisplayUtil&&) = delete;
  ~FakeDisplayUtil();

  // DisplayUtil overrides:
  std::optional<uint32_t> GetEmbeddedDisplayConnectorID() override;
  std::vector<uint32_t> GetExternalDisplayConnectorIDs() override;
  void FillPrivacyScreenInfo(const uint32_t connector_id,
                             bool* privacy_screen_supported,
                             bool* privacy_screen_enabled) override;
  ::ash::cros_healthd::mojom::ExternalDisplayInfoPtr GetExternalDisplayInfo(
      const uint32_t connector_id) override;
  ::ash::cros_healthd::mojom::EmbeddedDisplayInfoPtr GetEmbeddedDisplayInfo()
      override;

  void SetEmbeddedDisplayConnectorID(std::optional<uint32_t> value);
  void SetExternalDisplayConnectorIDs(const std::vector<uint32_t>& value);
  void SetPrivacyScreenInfo(uint32_t connector_id, FakePrivacyScreenInfo value);
  void SetExternalDisplayInfo(
      uint32_t connector_id,
      ::ash::cros_healthd::mojom::ExternalDisplayInfoPtr value);
  void SetEmbeddedDisplayInfo(
      ::ash::cros_healthd::mojom::EmbeddedDisplayInfoPtr value);

 private:
  std::optional<uint32_t> embedded_display_connector_id_;
  std::vector<uint32_t> external_display_connector_ids_;
  std::map<uint32_t, FakePrivacyScreenInfo> privacy_screen_info_;
  std::map<uint32_t, ::ash::cros_healthd::mojom::ExternalDisplayInfoPtr>
      external_display_info_;
  ::ash::cros_healthd::mojom::EmbeddedDisplayInfoPtr embedded_display_info_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_FAKE_DISPLAY_UTIL_H_
