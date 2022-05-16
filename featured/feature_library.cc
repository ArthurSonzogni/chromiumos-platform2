// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "featured/feature_library.h"

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include <brillo/dbus/dbus_connection.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_proxy_util.h>
#include <brillo/errors/error.h>

namespace feature {

PlatformFeatures::PlatformFeatures(scoped_refptr<dbus::Bus> bus,
                                   dbus::ObjectProxy* proxy)
    : bus_(bus), proxy_(proxy) {}

std::unique_ptr<PlatformFeatures> PlatformFeatures::New(
    scoped_refptr<dbus::Bus> bus) {
  auto* proxy = bus->GetObjectProxy(
      chromeos::kChromeFeaturesServiceName,
      dbus::ObjectPath(chromeos::kChromeFeaturesServicePath));
  if (!proxy) {
    LOG(ERROR) << "Failed to create object proxy for "
               << chromeos::kChromeFeaturesServiceName;
    return nullptr;
  }

  return std::unique_ptr<PlatformFeatures>(new PlatformFeatures(bus, proxy));
}

void PlatformFeatures::IsEnabled(const VariationsFeature& feature,
                                 IsEnabledCallback callback) {
  DCHECK(CheckFeatureIdentity(feature)) << feature.name;

  proxy_->WaitForServiceToBeAvailable(base::BindOnce(
      &PlatformFeatures::OnWaitForServiceIsEnabled,
      weak_ptr_factory_.GetWeakPtr(), feature, std::move(callback)));
}

bool PlatformFeatures::IsEnabledBlocking(const VariationsFeature& feature) {
  DCHECK(CheckFeatureIdentity(feature)) << feature.name;

  dbus::MethodCall call(chromeos::kChromeFeaturesServiceInterface,
                        chromeos::kChromeFeaturesServiceIsFeatureEnabledMethod);
  dbus::MessageWriter writer(&call);
  writer.AppendString(feature.name);
  std::unique_ptr<dbus::Response> response = brillo::dbus_utils::CallDBusMethod(
      bus_, proxy_, &call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response) {
    return feature.default_state == FEATURE_ENABLED_BY_DEFAULT;
  }

  dbus::MessageReader reader(response.get());
  bool feature_enabled = false;
  if (!reader.PopBool(&feature_enabled)) {
    LOG(ERROR) << "failed to read bool from dbus result; using default value";
    return feature.default_state == FEATURE_ENABLED_BY_DEFAULT;
  }

  return feature_enabled;
}

void PlatformFeatures::OnWaitForServiceIsEnabled(
    const VariationsFeature& feature,
    IsEnabledCallback callback,
    bool available) {
  if (!available) {
    std::move(callback).Run(feature.default_state ==
                            FEATURE_ENABLED_BY_DEFAULT);
    LOG(ERROR) << "failed to connect to dbus service; using default value";
    return;
  }
  dbus::MethodCall call(chromeos::kChromeFeaturesServiceInterface,
                        chromeos::kChromeFeaturesServiceIsFeatureEnabledMethod);
  dbus::MessageWriter writer(&call);
  writer.AppendString(feature.name);
  proxy_->CallMethod(&call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                     base::BindOnce(&PlatformFeatures::HandleIsEnabledResponse,
                                    weak_ptr_factory_.GetWeakPtr(), feature,
                                    std::move(callback)));
}

void PlatformFeatures::HandleIsEnabledResponse(const VariationsFeature& feature,
                                               IsEnabledCallback callback,
                                               dbus::Response* response) {
  if (!response) {
    LOG(ERROR) << "dbus call failed; using default value";
    std::move(callback).Run(feature.default_state ==
                            FEATURE_ENABLED_BY_DEFAULT);
    return;
  }

  dbus::MessageReader reader(response);
  bool feature_enabled = false;
  if (!reader.PopBool(&feature_enabled)) {
    LOG(ERROR) << "failed to read bool from dbus result; using default value";
    std::move(callback).Run(feature.default_state ==
                            FEATURE_ENABLED_BY_DEFAULT);
    return;
  }

  std::move(callback).Run(feature_enabled);
}

void PlatformFeatures::GetParamsAndEnabled(
    const std::vector<const VariationsFeature* const>& features,
    GetParamsCallback callback) {
  for (const auto& feature : features) {
    DCHECK(CheckFeatureIdentity(*feature)) << feature->name;
  }

  proxy_->WaitForServiceToBeAvailable(base::BindOnce(
      &PlatformFeatures::OnWaitForServiceGetParams,
      weak_ptr_factory_.GetWeakPtr(), features, std::move(callback)));
}

PlatformFeaturesInterface::ParamsResult
PlatformFeatures::GetParamsAndEnabledBlocking(
    const std::vector<const VariationsFeature* const>& features) {
  for (const auto& feature : features) {
    DCHECK(CheckFeatureIdentity(*feature)) << feature->name;
  }

  dbus::MethodCall call(chromeos::kChromeFeaturesServiceInterface,
                        chromeos::kChromeFeaturesServiceGetFeatureParamsMethod);
  dbus::MessageWriter writer(&call);
  EncodeGetParamsArgument(&writer, features);
  std::unique_ptr<dbus::Response> response = brillo::dbus_utils::CallDBusMethod(
      bus_, proxy_, &call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  return ParseGetParamsResponse(response.get(), features);
}

void PlatformFeatures::EncodeGetParamsArgument(
    dbus::MessageWriter* writer,
    const std::vector<const VariationsFeature* const>& features) {
  dbus::MessageWriter array_writer(nullptr);
  writer->OpenArray("s", &array_writer);
  for (const auto& feature : features) {
    array_writer.AppendString(feature->name);
  }
  writer->CloseContainer(&array_writer);
}

PlatformFeaturesInterface::ParamsResult
PlatformFeatures::CreateDefaultGetParamsAndEnabledResponse(
    const std::vector<const VariationsFeature* const>& features) {
  std::map<std::string, ParamsResultEntry> default_response;
  for (const auto& feature : features) {
    default_response[feature->name] = ParamsResultEntry{
        .enabled = feature->default_state == FEATURE_ENABLED_BY_DEFAULT,
    };
  }
  return default_response;
}

void PlatformFeatures::OnWaitForServiceGetParams(
    const std::vector<const VariationsFeature* const>& features,
    GetParamsCallback callback,
    bool available) {
  if (!available) {
    LOG(ERROR) << "failed to connect to dbus service; using default value";
    std::move(callback).Run(CreateDefaultGetParamsAndEnabledResponse(features));
    return;
  }
  dbus::MethodCall call(chromeos::kChromeFeaturesServiceInterface,
                        chromeos::kChromeFeaturesServiceGetFeatureParamsMethod);
  dbus::MessageWriter writer(&call);
  EncodeGetParamsArgument(&writer, features);
  proxy_->CallMethod(&call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                     base::BindOnce(&PlatformFeatures::HandleGetParamsResponse,
                                    weak_ptr_factory_.GetWeakPtr(), features,
                                    std::move(callback)));
}

void PlatformFeatures::HandleGetParamsResponse(
    const std::vector<const VariationsFeature* const>& features,
    GetParamsCallback callback,
    dbus::Response* response) {
  std::move(callback).Run(ParseGetParamsResponse(response, features));
}

PlatformFeatures::ParamsResult PlatformFeatures::ParseGetParamsResponse(
    dbus::Response* response,
    const std::vector<const VariationsFeature* const>& features) {
  // Parse the response, which should be an array containing dict entries which
  // maps feature names to a struct containing:
  // * A bool for whether to use the overridden feature enable state
  // * A bool indicating the enable state (only valid if the first bool is true)
  // * A (possibly-empty) array of dict entries mapping parameter keys to values
  if (!response) {
    LOG(ERROR) << "dbus call failed; using default value";
    return CreateDefaultGetParamsAndEnabledResponse(features);
  }

  dbus::MessageReader reader(response);
  dbus::MessageReader array_reader(nullptr);
  if (!reader.PopArray(&array_reader)) {
    LOG(ERROR) << "failed to read array from dbus result; using default value";
    return CreateDefaultGetParamsAndEnabledResponse(features);
  }

  std::map<std::string, ParamsResultEntry> result;
  while (array_reader.HasMoreData()) {
    ParamsResultEntry entry;

    dbus::MessageReader feature_entry_reader(nullptr);
    if (!array_reader.PopDictEntry(&feature_entry_reader)) {
      LOG(ERROR) << "Failed to read dict from dbus result; using default.";
      return CreateDefaultGetParamsAndEnabledResponse(features);
    }

    // Get name
    std::string feature_name;
    if (!feature_entry_reader.PopString(&feature_name)) {
      LOG(ERROR) << "Failed to read string from dbus result; using default "
                 << "value";
      return CreateDefaultGetParamsAndEnabledResponse(features);
    }

    dbus::MessageReader struct_reader(nullptr);
    if (!feature_entry_reader.PopStruct(&struct_reader)) {
      LOG(ERROR) << "Failed to read struct from dbus result; using default "
                 << "value";
      return CreateDefaultGetParamsAndEnabledResponse(features);
    }

    // Get override state.
    bool use_override = false;
    bool override_value = false;
    if (!struct_reader.PopBool(&use_override) ||
        !struct_reader.PopBool(&override_value)) {
      LOG(ERROR) << "Failed to pop a bool; using default value";
      return CreateDefaultGetParamsAndEnabledResponse(features);
    } else {
      if (use_override) {
        entry.enabled = override_value;
      } else {
        // This is mildly inefficient, but the number of features passed to this
        // method is expected to be small (human magnitude), so it isn't a
        // prohibitive cost.
        for (const auto& feature : features) {
          if (feature->name == feature_name) {
            entry.enabled =
                feature->default_state == FEATURE_ENABLED_BY_DEFAULT;
          }
        }
      }
    }

    // Finally, get params.
    std::map<std::string, std::string> params;
    dbus::MessageReader params_array_reader(nullptr);
    if (!struct_reader.PopArray(&params_array_reader)) {
      LOG(ERROR) << "Failed to read params array; using default value";
      return CreateDefaultGetParamsAndEnabledResponse(features);
    }
    while (params_array_reader.HasMoreData()) {
      dbus::MessageReader entry_reader(nullptr);
      std::string key;
      std::string value;
      if (!params_array_reader.PopDictEntry(&entry_reader) ||
          !entry_reader.PopString(&key) || !entry_reader.PopString(&value)) {
        LOG(ERROR) << "failed to read dict entry; using default value";
        return CreateDefaultGetParamsAndEnabledResponse(features);
      }
      params[key] = value;
    }
    entry.params = std::move(params);

    result[feature_name] = entry;
  }

  return result;
}

bool PlatformFeatures::CheckFeatureIdentity(const VariationsFeature& feature) {
  base::AutoLock auto_lock(lock_);

  auto it = feature_identity_tracker_.find(feature.name);
  if (it == feature_identity_tracker_.end()) {
    // If it's not tracked yet, register it.
    feature_identity_tracker_[feature.name] = &feature;
    return true;
  }
  // Compare address of |feature| to the existing tracked entry.
  return it->second == &feature;
}

void PlatformFeatures::ShutdownBus() {
  bus_->ShutdownAndBlock();
}

// FakePlatformFeatures
void FakePlatformFeatures::IsEnabled(const VariationsFeature& feature,
                                     IsEnabledCallback callback) {
  base::AutoLock auto_lock(enabled_lock_);
  bus_->AssertOnOriginThread();
  auto it = enabled_.find(feature.name);
  bool enabled = feature.default_state == FEATURE_ENABLED_BY_DEFAULT;
  if (it != enabled_.end()) {
    enabled = it->second;
  }
  bus_->GetOriginTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), enabled));
}

bool FakePlatformFeatures::IsEnabledBlocking(const VariationsFeature& feature) {
  base::AutoLock auto_lock(enabled_lock_);
  auto it = enabled_.find(feature.name);
  if (it != enabled_.end()) {
    return it->second;
  }
  return feature.default_state == FEATURE_ENABLED_BY_DEFAULT;
}

void FakePlatformFeatures::GetParamsAndEnabled(
    const std::vector<const VariationsFeature* const>& features,
    GetParamsCallback callback) {
  bus_->AssertOnOriginThread();

  bus_->GetOriginTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback),
                                GetParamsAndEnabledBlocking(features)));
}

PlatformFeaturesInterface::ParamsResult
FakePlatformFeatures::GetParamsAndEnabledBlocking(
    const std::vector<const VariationsFeature* const>& features) {
  base::AutoLock auto_lock(enabled_lock_);
  std::map<std::string, ParamsResultEntry> out;
  for (const auto& feature : features) {
    ParamsResultEntry cur;

    auto enabled_it = enabled_.find(feature->name);
    cur.enabled = enabled_it != enabled_.end() && enabled_it->second;
    if (cur.enabled) {
      // only enabled features have params.
      auto params_it = params_.find(feature->name);
      if (params_it != params_.end()) {
        cur.params = params_it->second;
      }
    }

    out[feature->name] = cur;
  }

  return out;
}

void FakePlatformFeatures::SetEnabled(const std::string& feature,
                                      bool enabled) {
  base::AutoLock auto_lock(enabled_lock_);
  enabled_[feature] = enabled;
}

void FakePlatformFeatures::ClearEnabled(const std::string& feature) {
  base::AutoLock auto_lock(enabled_lock_);
  enabled_.erase(feature);
}

void FakePlatformFeatures::SetParam(const std::string& feature,
                                    const std::string& key,
                                    const std::string& value) {
  base::AutoLock auto_lock(enabled_lock_);
  params_[feature][key] = value;
}

void FakePlatformFeatures::ClearParams(const std::string& feature) {
  base::AutoLock auto_lock(enabled_lock_);
  params_.erase(feature);
}

void FakePlatformFeatures::ShutdownBus() {
  bus_->ShutdownAndBlock();
}
}  // namespace feature
