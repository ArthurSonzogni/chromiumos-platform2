// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "featured/feature_library.h"

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>

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

void PlatformFeatures::IsEnabled(const Feature& feature,
                                 IsEnabledCallback callback) {
  DCHECK(CheckFeatureIdentity(feature)) << feature.name;

  proxy_->WaitForServiceToBeAvailable(base::BindOnce(
      &PlatformFeatures::OnWaitForService, weak_ptr_factory_.GetWeakPtr(),
      feature, std::move(callback)));
}

bool PlatformFeatures::IsEnabledBlocking(const Feature& feature) {
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

void PlatformFeatures::OnWaitForService(const Feature& feature,
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

void PlatformFeatures::HandleIsEnabledResponse(const Feature& feature,
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

bool PlatformFeatures::CheckFeatureIdentity(const Feature& feature) {
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

}  // namespace feature
