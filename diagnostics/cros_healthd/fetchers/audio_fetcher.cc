// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/audio_fetcher.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <chromeos/dbus/service_constants.h>
#include <cras/dbus-proxies.h>

#include "base/functional/bind.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

class State {
 public:
  State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State() = default;

  // Handle the response of volume state from CRAS.
  void HandleMuteInfo(brillo::Error* err,
                      int32_t output_volume,
                      bool output_mute,
                      bool input_mute,
                      bool output_user_mute);

  // Handle the response of node information from CRAS.
  void HandleNodeInfo(brillo::Error* err,
                      const std::vector<brillo::VariantDictionary>& nodes);

  // Set up the |error_|.
  void SetError(const std::string& message);

  // Send back the AudioResult via |callback|. The result is ProbeError if
  // |error_| is not null or |is_finished| is false, otherwise |info_|.
  void HandleResult(FetchAudioInfoCallback callback, bool is_finished);

 private:
  // The info to be returned.
  mojom::AudioInfoPtr info_ = mojom::AudioInfo::New();
  // The error to be returned.
  mojom::ProbeErrorPtr error_;
};

void State::HandleMuteInfo(brillo::Error* err,
                           int32_t output_volume,
                           bool output_mute,
                           bool input_mute,
                           bool output_user_mute) {
  if (err) {
    SetError("Failed retrieving mute info from cras: " + err->GetMessage());
    return;
  }

  info_->output_mute = output_mute | output_user_mute;
  info_->input_mute = input_mute;
}

void State::HandleNodeInfo(
    brillo::Error* err, const std::vector<brillo::VariantDictionary>& nodes) {
  if (err) {
    SetError("Failed retrieving node info from cras: " + err->GetMessage());
    return;
  }

  // There might be no active output / input device such as Chromebox.
  info_->output_device_name = std::string("No active output device");
  info_->output_volume = 0;
  info_->input_device_name = std::string("No active input device");
  info_->input_gain = 0;
  info_->underruns = 0;
  info_->severe_underruns = 0;

  std::vector<mojom::AudioNodeInfoPtr> output_nodes;
  std::vector<mojom::AudioNodeInfoPtr> input_nodes;
  for (const auto& node : nodes) {
    // Important fields are missing.
    if (!node.contains(cras::kIsInputProperty) ||
        !node.contains(cras::kActiveProperty)) {
      continue;
    }

    auto node_info = mojom::AudioNodeInfo::New();
    node_info->id =
        brillo::GetVariantValueOrDefault<uint64_t>(node, cras::kIdProperty);
    node_info->name = brillo::GetVariantValueOrDefault<std::string>(
        node, cras::kNameProperty);
    node_info->device_name = brillo::GetVariantValueOrDefault<std::string>(
        node, cras::kDeviceNameProperty);
    node_info->active =
        brillo::GetVariantValueOrDefault<bool>(node, cras::kActiveProperty);
    node_info->node_volume = brillo::GetVariantValueOrDefault<uint64_t>(
        node, cras::kNodeVolumeProperty);
    node_info->input_node_gain = brillo::GetVariantValueOrDefault<uint32_t>(
        node, cras::kInputNodeGainProperty);

    if (!brillo::GetVariantValueOrDefault<bool>(node, cras::kIsInputProperty)) {
      if (node_info->active) {
        // Active output node.
        info_->output_device_name = node_info->name;
        info_->output_volume = node_info->node_volume;
        if (node.contains(cras::kNumberOfUnderrunsProperty)) {
          info_->underruns = brillo::GetVariantValueOrDefault<uint32_t>(
              node, cras::kNumberOfUnderrunsProperty);
        }
        if (node.contains(cras::kNumberOfSevereUnderrunsProperty)) {
          info_->severe_underruns = brillo::GetVariantValueOrDefault<uint32_t>(
              node, cras::kNumberOfSevereUnderrunsProperty);
        }
      }
      output_nodes.push_back(std::move(node_info));
    } else {
      if (node_info->active) {
        // Active input node.
        info_->input_device_name = node_info->name;
        info_->input_gain = node_info->input_node_gain;
      }
      input_nodes.push_back(std::move(node_info));
    }
  }

  info_->output_nodes = std::move(output_nodes);
  info_->input_nodes = std::move(input_nodes);
}

void State::SetError(const std::string& message) {
  error_ =
      CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError, message);
}

void State::HandleResult(FetchAudioInfoCallback callback, bool is_finished) {
  if (!is_finished) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to finish all callbacks.");
  }
  if (!error_.is_null()) {
    std::move(callback).Run(mojom::AudioResult::NewError(std::move(error_)));
    return;
  }
  std::move(callback).Run(mojom::AudioResult::NewAudioInfo(std::move(info_)));
}

void FetchMuteInfo(Context* context,
                   CallbackBarrier& barrier,
                   State* state_ptr) {
  auto [on_success, on_error] = SplitDbusCallback(barrier.Depend(
      base::BindOnce(&State::HandleMuteInfo, base::Unretained(state_ptr))));
  context->cras_proxy()->GetVolumeStateAsync(std::move(on_success),
                                             std::move(on_error));
}

void FetchNodeInfo(Context* context,
                   CallbackBarrier& barrier,
                   State* state_ptr) {
  auto [on_success, on_error] = SplitDbusCallback(barrier.Depend(
      base::BindOnce(&State::HandleNodeInfo, base::Unretained(state_ptr))));
  context->cras_proxy()->GetNodeInfosAsync(std::move(on_success),
                                           std::move(on_error));
}

}  // namespace

void FetchAudioInfo(Context* context, FetchAudioInfoCallback callback) {
  auto state = std::make_unique<State>();
  State* state_ptr = state.get();
  CallbackBarrier barrier{base::BindOnce(&State::HandleResult, std::move(state),
                                         std::move(callback))};
  FetchMuteInfo(context, barrier, state_ptr);
  FetchNodeInfo(context, barrier, state_ptr);
}

}  // namespace diagnostics
