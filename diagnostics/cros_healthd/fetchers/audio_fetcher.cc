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

AudioFetcher::AudioFetcher(Context* context) : context_(context) {
  DCHECK(context_);
}

AudioFetcher::~AudioFetcher() = default;

mojo_ipc::AudioResultPtr AudioFetcher::FetchAudioInfo() {
  mojo_ipc::AudioInfo info;

  auto error = PopulateMuteInfo(&info);
  if (error.has_value()) {
    return mojo_ipc::AudioResult::NewError(std::move(error.value()));
  }

  error = PopulateActiveOutputNodeInfo(&info);
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

base::Optional<mojo_ipc::ProbeErrorPtr>
AudioFetcher::PopulateActiveOutputNodeInfo(mojo_ipc::AudioInfo* info) {
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

  for (const auto& node : nodes) {
    // Active output node
    if (node.find(cras::kIsInputProperty) != node.end() &&
        node.find(cras::kActiveProperty) != node.end() &&
        !brillo::GetVariantValueOrDefault<bool>(node, cras::kIsInputProperty) &&
        brillo::GetVariantValueOrDefault<bool>(node, cras::kActiveProperty)) {
      info->output_device_name = brillo::GetVariantValueOrDefault<std::string>(
          node, cras::kNameProperty);
      info->output_volume = brillo::GetVariantValueOrDefault<uint64_t>(
          node, cras::kNodeVolumeProperty);
    }
  }

  return base::nullopt;
}

}  // namespace diagnostics
