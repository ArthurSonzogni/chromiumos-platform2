// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/dbus_adaptor.h"

#include <utility>
#include <vector>

#include <base/location.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>
#include <hps/daemon/filters/filter_factory.h>

namespace hps {

constexpr char kErrorPath[] = "org.chromium.Hps.GetFeatureResultError";

namespace {

std::vector<uint8_t> HpsResultToSerializedBytes(HpsResult result) {
  HpsResultProto result_proto;
  result_proto.set_value(result);

  std::vector<uint8_t> serialized;
  serialized.resize(result_proto.ByteSizeLong());
  result_proto.SerializeToArray(serialized.data(),
                                static_cast<int>(serialized.size()));
  return serialized;
}

}  // namespace

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus,
                         std::unique_ptr<HPS> hps,
                         uint32_t poll_time_ms)
    : org::chromium::HpsAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(::hps::kHpsServicePath)),
      hps_(std::move(hps)),
      poll_time_ms_(poll_time_ms) {
  ShutDown();
}

void DBusAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

void DBusAdaptor::PollTask() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If the HPS module is not running even though we haven't shut it down, the
  // system was probably suspended and resumed, resetting HPS as a side effect.
  // Reboot the module and restore the enabled features so we can continue
  // polling.
  //
  // Note that it's possible for a system suspend and resume to happen at any
  // point, including while we're inside the loop below. That means we'll either
  // report unknown feature results or in the worst case abort with a fault if
  // HPS is in an unexpected state. This means DBUS clients need to handle hpsd
  // restarting at arbitrary times.
  if (!hps_->IsRunning()) {
    LOG(INFO) << "HPS reset detected";
    ShutDown();
    BootIfNeeded();
  }

  for (uint8_t i = 0; i < kFeatures; ++i) {
    const auto& feature = features_[i];
    if (feature.enabled) {
      FeatureResult result = this->hps_->Result(i);
      DCHECK(feature.filter);
      DCHECK(feature.committed);
      const auto res =
          feature.filter->ProcessResult(result.inference_result, result.valid);
      VLOG(2) << "Poll: Feature: " << static_cast<int>(i)
              << " Valid: " << result.valid
              << " Result: " << static_cast<int>(result.inference_result)
              << " Filter: " << static_cast<int>(res);
    }
  }
}

void DBusAdaptor::BootIfNeeded() {
  if (hps_booted_) {
    return;
  }
  if (!hps_->Boot()) {
    LOG(FATAL) << "Failed to boot";
  }
  if (!CommitUpdates()) {
    LOG(FATAL) << "Failed to restore features";
  }
  hps_booted_ = true;
}

void DBusAdaptor::ShutDown() {
  poll_timer_.Stop();
  if (!hps_->ShutDown()) {
    LOG(FATAL) << "Failed to shutdown";
  }
  hps_booted_ = false;
  for (uint8_t i = 0; i < kFeatures; i++) {
    features_[i].committed = !features_[i].enabled;
    features_[i].filter.reset();
  }
}

bool DBusAdaptor::CommitUpdates() {
  bool result = true;
  for (uint8_t i = 0; i < kFeatures; i++) {
    auto& feature = features_[i];
    if (feature.committed)
      continue;

    if (feature.enabled && !feature.filter) {
      feature.filter = CreateFilter(feature.config, feature.callback);
    }

    if (feature.enabled && !hps_->Enable(i)) {
      feature.enabled = false;
      feature.filter.reset();
      feature.config = {};
      feature.callback = StatusCallback{};
      result = false;
    } else if (!feature.enabled && !hps_->Disable(i)) {
      feature.enabled = true;
      result = false;
    }
    feature.committed = true;
  }

  size_t active_features =
      std::count_if(features_.begin(), features_.end(),
                    [](const auto& f) { return f.enabled && f.committed; });

  if (!active_features && hps_booted_) {
    ShutDown();
  } else if (active_features && !poll_timer_.IsRunning()) {
    poll_timer_.Start(
        FROM_HERE, base::Milliseconds(poll_time_ms_),
        base::BindRepeating(&DBusAdaptor::PollTask, base::Unretained(this)));
  }
  return result;
}

bool DBusAdaptor::EnableFeature(brillo::ErrorPtr* error,
                                const hps::FeatureConfig& config,
                                uint8_t feature,
                                StatusCallback callback) {
  CHECK_LT(feature, kFeatures);
  BootIfNeeded();
  features_[feature].filter.reset();
  features_[feature].config = config;
  features_[feature].callback = callback;
  features_[feature].enabled = true;
  features_[feature].committed = false;
  if (!CommitUpdates()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kErrorPath, "hpsd: Unable to enable feature");
    return false;
  }
  return true;
}

bool DBusAdaptor::DisableFeature(brillo::ErrorPtr* error, uint8_t feature) {
  CHECK_LT(feature, kFeatures);
  features_[feature].filter.reset();
  features_[feature].config = {};
  features_[feature].callback = StatusCallback{};
  features_[feature].enabled = false;
  features_[feature].committed = false;
  if (!CommitUpdates()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kErrorPath, "hpsd: Unable to disable feature");
    return false;
  }
  return true;
}

bool DBusAdaptor::GetFeatureResult(brillo::ErrorPtr* error,
                                   HpsResultProto* result,
                                   uint8_t feature) {
  CHECK_LT(feature, kFeatures);
  if (!features_[feature].enabled) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kErrorPath, "hpsd: Feature not enabled.");

    return false;
  }
  DCHECK(features_[feature].filter);
  result->set_value(features_[feature].filter->GetCurrentResult());
  return true;
}

bool DBusAdaptor::EnableHpsSense(brillo::ErrorPtr* error,
                                 const hps::FeatureConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return EnableFeature(
      error, config, 0,
      base::BindRepeating(&DBusAdaptor::SendHpsSenseChangedSignal,
                          base::Unretained(this)));
}

bool DBusAdaptor::DisableHpsSense(brillo::ErrorPtr* error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (DisableFeature(error, 0)) {
    DBusAdaptor::SendHpsSenseChangedSignal(
        HpsResultToSerializedBytes(HpsResult::UNKNOWN));
    return true;
  }
  return false;
}

bool DBusAdaptor::GetResultHpsSense(brillo::ErrorPtr* error,
                                    HpsResultProto* result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return GetFeatureResult(error, result, 0);
}

bool DBusAdaptor::EnableHpsNotify(brillo::ErrorPtr* error,
                                  const hps::FeatureConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return EnableFeature(
      error, config, 1,
      base::BindRepeating(&DBusAdaptor::SendHpsNotifyChangedSignal,
                          base::Unretained(this)));
}

bool DBusAdaptor::DisableHpsNotify(brillo::ErrorPtr* error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (DisableFeature(error, 1)) {
    DBusAdaptor::SendHpsNotifyChangedSignal(
        HpsResultToSerializedBytes(HpsResult::UNKNOWN));
    return true;
  }
  return false;
}

bool DBusAdaptor::GetResultHpsNotify(brillo::ErrorPtr* error,
                                     HpsResultProto* result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return GetFeatureResult(error, result, 1);
}

}  // namespace hps
