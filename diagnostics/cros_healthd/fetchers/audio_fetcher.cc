// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <chromeos/dbus/service_constants.h>
#include "diagnostics/cros_healthd/fetchers/audio_fetcher.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

#include <utility>

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

mojo_ipc::AudioResultPtr AudioFetcher::FetchAudioInfo() {
  mojo_ipc::AudioInfo info;

  auto error = PopulateMuteInfo(&info);
  if (error.has_value()) {
    return mojo_ipc::AudioResult::NewError(std::move(error.value()));
  }

  error = PopulateActiveNodeInfo(&info);
  if (error.has_value()) {
    return mojo_ipc::AudioResult::NewError(std::move(error.value()));
  }

  return mojo_ipc::AudioResult::NewAudioInfo(info.Clone());
}

base::Optional<mojo_ipc::ProbeErrorPtr> AudioFetcher::PopulateMuteInfo(
    mojo_ipc::AudioInfo* info) {
  int32_t unused_output_volume;
  bool output_mute = false;  // Mute by other system daemons.
  bool input_mute = false;
  bool output_user_mute = false;  // Mute by users.
  brillo::ErrorPtr error;
  if (!context_->cras_proxy()->GetVolumeState(&unused_output_volume,
                                              &output_mute, &input_mute,
                                              &output_user_mute, &error)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kSystemUtilityError,
        "Failed retrieving mute info from cras: " + error->GetMessage());
  }

  info->output_mute = output_mute | output_user_mute;
  info->input_mute = input_mute;

  return base::nullopt;
}

base::Optional<mojo_ipc::ProbeErrorPtr> AudioFetcher::PopulateActiveNodeInfo(
    mojo_ipc::AudioInfo* info) {
  std::vector<brillo::VariantDictionary> nodes;
  brillo::ErrorPtr error;
  if (!context_->cras_proxy()->GetNodeInfos(&nodes, &error)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kSystemUtilityError,
        "Failed retrieving node info from cras: " + error->GetMessage());
  }

  // There might be no active output device such as Chromebox.
  info->output_device_name = std::string("No active output device");
  info->output_volume = 0;
  info->underruns = 0;
  info->severe_underruns = 0;

  for (const auto& node : nodes) {
    // Skip inactive node, or important fields are missing.
    if (node.find(cras::kIsInputProperty) == node.end() ||
        node.find(cras::kActiveProperty) == node.end() ||
        !brillo::GetVariantValueOrDefault<bool>(node, cras::kActiveProperty)) {
      continue;
    }
    if (!brillo::GetVariantValueOrDefault<bool>(node, cras::kIsInputProperty)) {
      // Output node
      info->output_device_name = brillo::GetVariantValueOrDefault<std::string>(
          node, cras::kNameProperty);
      info->output_volume = brillo::GetVariantValueOrDefault<uint64_t>(
          node, cras::kNodeVolumeProperty);
      if (node.find(cras::kNumberOfUnderrunsProperty) != node.end()) {
        info->underruns = brillo::GetVariantValueOrDefault<uint32_t>(
            node, cras::kNumberOfUnderrunsProperty);
      }
      if (node.find(cras::kNumberOfSevereUnderrunsProperty) != node.end()) {
        info->severe_underruns = brillo::GetVariantValueOrDefault<uint32_t>(
            node, cras::kNumberOfSevereUnderrunsProperty);
      }
    } else {
      // Input node
      info->input_device_name = brillo::GetVariantValueOrDefault<std::string>(
          node, cras::kNameProperty);
      info->input_gain = brillo::GetVariantValueOrDefault<uint32_t>(
          node, cras::kInputNodeGainProperty);
    }
  }

  return base::nullopt;
}

}  // namespace diagnostics
