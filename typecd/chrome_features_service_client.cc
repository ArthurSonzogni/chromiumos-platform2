// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/chrome_features_service_client.h"

#include <memory>

#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>

namespace {
// TODO(b/206518847): See if we can avoid timeouts.
constexpr int kDbusTimeoutMs = 100;
constexpr uint32_t kRetrySleepTimeoutMs = 100;
}  // namespace

namespace typecd {

ChromeFeaturesServiceClient::ChromeFeaturesServiceClient(
    scoped_refptr<dbus::Bus> bus) {
  proxy_ = bus->GetObjectProxy(
      chromeos::kChromeFeaturesServiceName,
      dbus::ObjectPath(chromeos::kChromeFeaturesServicePath));
  if (!proxy_)
    LOG(ERROR) << "Didn't get valid proxy.";
}

bool ChromeFeaturesServiceClient::IsPeripheralDataAccessEnabled() {
  if (!proxy_) {
    LOG(ERROR)
        << "No Chrome proxy created, can't fetch peripheral data setting.";
    return false;
  }

  int retries = 10;
  while (retries--) {
    dbus::MethodCall method_call(
        chromeos::kChromeFeaturesServiceInterface,
        chromeos::kChromeFeaturesServiceIsPeripheralDataAccessEnabledMethod);

    std::unique_ptr<dbus::Response> dbus_response =
        proxy_->CallMethodAndBlock(&method_call, kDbusTimeoutMs);
    if (dbus_response) {
      bool enabled;
      dbus::MessageReader reader(dbus_response.get());
      reader.PopBool(&enabled);
      return enabled;
    }

    LOG(WARNING) << "Chrome features D-Bus retries remaining: " << retries;
    base::PlatformThread::Sleep(
        base::TimeDelta::FromMilliseconds(kRetrySleepTimeoutMs));
  }

  LOG(ERROR)
      << "Failed to get Chrome feature: DevicePciPeripheralDataAccessEnabled.";
  return false;
}

}  // namespace typecd
