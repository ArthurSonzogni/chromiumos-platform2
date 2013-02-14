// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_WIFI_
#define SHILL_MOCK_WIFI_

#include <map>
#include <string>

#include <base/memory/ref_counted.h>
#include <gmock/gmock.h>

#include "shill/key_value_store.h"
#include "shill/refptr_types.h"
#include "shill/wifi.h"
#include "shill/wifi_endpoint.h"
#include "shill/wifi_service.h"

namespace shill {

class ControlInterface;
class Error;
class EventDispatcher;

class MockWiFi : public WiFi {
 public:
  MockWiFi(ControlInterface *control_interface,
           EventDispatcher *dispatcher,
           Metrics *metrics,
           Manager *manager,
           const std::string &link_name,
           const std::string &address,
           int interface_index);
  virtual ~MockWiFi();

  MOCK_METHOD2(Start, void(Error *error,
                           const EnabledStateChangedCallback &callback));
  MOCK_METHOD2(Stop, void(Error *error,
                          const EnabledStateChangedCallback &callback));
  MOCK_METHOD1(Scan, void(Error *error));
  MOCK_METHOD1(DisconnectFrom, void(WiFiService *service));
  MOCK_METHOD1(ClearCachedCredentials, void(const WiFiService *service));
  MOCK_METHOD2(ConnectTo,
               void(WiFiService *service,
                    std::map<std::string, ::DBus::Variant> service_params));
  MOCK_CONST_METHOD0(IsIdle, bool());
  MOCK_METHOD1(NotifyEndpointChanged,
               void(const WiFiEndpointConstRefPtr &endpoint));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockWiFi);
};

}  // namespace shill

#endif  // SHILL_MOCK_WIFI_
