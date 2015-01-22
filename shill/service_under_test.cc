// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/service_under_test.h"

#include <string>

#include "shill/mock_adaptors.h"
#include "shill/property_accessor.h"

using std::string;

namespace shill {

// static
const char ServiceUnderTest::kKeyValueStoreProperty[] = "key_value_store";
const char ServiceUnderTest::kRpcId[] = "/mock_device_rpc";
const char ServiceUnderTest::kStringsProperty[] = "strings";
const char ServiceUnderTest::kStorageId[] = "service";

ServiceUnderTest::ServiceUnderTest(ControlInterface *control_interface,
                                   EventDispatcher *dispatcher,
                                   Metrics *metrics,
                                   Manager *manager)
    : Service(control_interface, dispatcher, metrics, manager,
              Technology::kUnknown) {
  mutable_store()->RegisterStrings(kStringsProperty, &strings_);
  mutable_store()->RegisterDerivedKeyValueStore(
      kKeyValueStoreProperty,
      KeyValueStoreAccessor(
          new CustomAccessor<ServiceUnderTest, KeyValueStore>(
              this, &ServiceUnderTest::GetKeyValueStore,
              &ServiceUnderTest::SetKeyValueStore)));
}

ServiceUnderTest::~ServiceUnderTest() {}

string ServiceUnderTest::GetRpcIdentifier() const {
  return ServiceMockAdaptor::kRpcId;
}

string ServiceUnderTest::GetDeviceRpcId(Error */*error*/) const {
  return kRpcId;
}

string ServiceUnderTest::GetStorageIdentifier() const { return kStorageId; }

bool ServiceUnderTest::SetKeyValueStore(
    const KeyValueStore &value, Error *error) {
  key_value_store_.Clear();
  key_value_store_.CopyFrom(value);
  return true;
}

KeyValueStore ServiceUnderTest::GetKeyValueStore(Error *error) {
  return key_value_store_;
}

}  // namespace shill
