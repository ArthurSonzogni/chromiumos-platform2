// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/chrome_features_service_client.h"

#include <chromeos/dbus/service_constants.h>

namespace typecd {

ChromeFeaturesServiceClient::ChromeFeaturesServiceClient(
    scoped_refptr<dbus::Bus> bus) {
  proxy_ = bus->GetObjectProxy(
      chromeos::kChromeFeaturesServiceName,
      dbus::ObjectPath(chromeos::kChromeFeaturesServicePath));
  if (!proxy_)
    LOG(ERROR) << "Didn't get valid proxy.";
}

}  // namespace typecd
