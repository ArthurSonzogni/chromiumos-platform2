// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/fake_display_util.h"

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <base/check.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

FakeDisplayUtil::FakeDisplayUtil()
    : embedded_display_info_(mojom::EmbeddedDisplayInfo::New()) {}

FakeDisplayUtil::~FakeDisplayUtil() = default;

std::optional<uint32_t> FakeDisplayUtil::GetEmbeddedDisplayConnectorID() {
  return embedded_display_connector_id_;
}

std::vector<uint32_t> FakeDisplayUtil::GetExternalDisplayConnectorIDs() {
  return external_display_connector_ids_;
}

void FakeDisplayUtil::FillPrivacyScreenInfo(const uint32_t connector_id,
                                            bool* privacy_screen_supported,
                                            bool* privacy_screen_enabled) {
  auto it = privacy_screen_info_.find(connector_id);
  CHECK(it != privacy_screen_info_.end());
  *privacy_screen_supported = it->second.supported;
  *privacy_screen_enabled = it->second.enabled;
}

mojom::ExternalDisplayInfoPtr FakeDisplayUtil::GetExternalDisplayInfo(
    const uint32_t connector_id) {
  auto it = external_display_info_.find(connector_id);
  CHECK(it != external_display_info_.end());
  return it->second.Clone();
}

mojom::EmbeddedDisplayInfoPtr FakeDisplayUtil::GetEmbeddedDisplayInfo() {
  return embedded_display_info_.Clone();
}

void FakeDisplayUtil::SetEmbeddedDisplayConnectorID(
    std::optional<uint32_t> value) {
  embedded_display_connector_id_ = value;
}

void FakeDisplayUtil::SetExternalDisplayConnectorIDs(
    const std::vector<uint32_t>& value) {
  external_display_connector_ids_ = value;
}

void FakeDisplayUtil::SetPrivacyScreenInfo(uint32_t connector_id,
                                           FakePrivacyScreenInfo value) {
  privacy_screen_info_[connector_id] = value;
}

void FakeDisplayUtil::SetExternalDisplayInfo(
    uint32_t connector_id, mojom::ExternalDisplayInfoPtr value) {
  external_display_info_[connector_id] = std::move(value);
}

void FakeDisplayUtil::SetEmbeddedDisplayInfo(
    mojom::EmbeddedDisplayInfoPtr value) {
  CHECK(value);
  embedded_display_info_ = std::move(value);
}

}  // namespace diagnostics
