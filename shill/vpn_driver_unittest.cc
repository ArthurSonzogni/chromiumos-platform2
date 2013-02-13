// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn_driver.h"

#include <base/stl_util.h>
#include <base/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/event_dispatcher.h"
#include "shill/glib.h"
#include "shill/mock_connection.h"
#include "shill/mock_device_info.h"
#include "shill/mock_glib.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_service.h"
#include "shill/mock_store.h"
#include "shill/nice_mock_control.h"
#include "shill/property_store.h"

using std::string;
using testing::_;
using testing::AnyNumber;
using testing::NiceMock;
using testing::Return;
using testing::SetArgumentPointee;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {

const char kHostProperty[] = "VPN.Host";
const char kOTPProperty[] = "VPN.OTP";
const char kPINProperty[] = "VPN.PIN";
const char kPSKProperty[] = "VPN.PSK";
const char kPasswordProperty[] = "VPN.Password";
const char kPortProperty[] = "VPN.Port";

const char kPIN[] = "5555";
const char kPassword[] = "random-password";
const char kPort[] = "1234";
const char kStorageID[] = "vpn_service_id";

}  // namespace

class VPNDriverUnderTest : public VPNDriver {
 public:
  VPNDriverUnderTest(EventDispatcher *dispatcher, Manager *manager);
  virtual ~VPNDriverUnderTest() {}

  // Inherited from VPNDriver.
  MOCK_METHOD2(ClaimInterface, bool(const string &link_name,
                                    int interface_index));
  MOCK_METHOD2(Connect, void(const VPNServiceRefPtr &service, Error *error));
  MOCK_METHOD0(Disconnect, void());
  MOCK_METHOD0(OnConnectionDisconnected, void());
  MOCK_CONST_METHOD0(GetProviderType, string());

 private:
  static const Property kProperties[];

  DISALLOW_COPY_AND_ASSIGN(VPNDriverUnderTest);
};

// static
const VPNDriverUnderTest::Property VPNDriverUnderTest::kProperties[] = {
  { kHostProperty, 0 },
  { kOTPProperty, Property::kEphemeral },
  { kPINProperty, Property::kWriteOnly },
  { kPSKProperty, Property::kCredential },
  { kPasswordProperty, Property::kCredential },
  { kPortProperty, 0 },
  { flimflam::kProviderNameProperty, 0 },
};

VPNDriverUnderTest::VPNDriverUnderTest(
    EventDispatcher *dispatcher, Manager *manager)
    : VPNDriver(dispatcher, manager, kProperties, arraysize(kProperties)) {}

class VPNDriverTest : public Test {
 public:
  VPNDriverTest()
      : device_info_(&control_, &dispatcher_, &metrics_, &manager_),
        metrics_(&dispatcher_),
        manager_(&control_, &dispatcher_, &metrics_, &glib_),
        driver_(&dispatcher_, &manager_) {}

  virtual ~VPNDriverTest() {}

 protected:
  EventDispatcher *dispatcher() { return driver_.dispatcher_; }
  void set_dispatcher(EventDispatcher *dispatcher) {
    driver_.dispatcher_ = dispatcher;
  }

  const base::CancelableClosure &connect_timeout_callback() {
    return driver_.connect_timeout_callback_;
  }

  bool IsConnectTimeoutStarted() { return driver_.IsConnectTimeoutStarted(); }
  int connect_timeout_seconds() { return driver_.connect_timeout_seconds(); }

  void StartConnectTimeout(int timeout_seconds) {
    driver_.StartConnectTimeout(timeout_seconds);
  }

  void StopConnectTimeout() { driver_.StopConnectTimeout(); }

  void SetArg(const string &arg, const string &value) {
    driver_.args()->SetString(arg, value);
  }

  KeyValueStore *GetArgs() { return driver_.args(); }

  bool GetProviderProperty(const PropertyStore &store,
                           const string &key,
                           string *value);

  NiceMockControl control_;
  NiceMock<MockDeviceInfo> device_info_;
  EventDispatcher dispatcher_;
  MockMetrics metrics_;
  MockGLib glib_;
  MockManager manager_;
  VPNDriverUnderTest driver_;
};

bool VPNDriverTest::GetProviderProperty(const PropertyStore &store,
                                        const string &key,
                                        string *value) {
  KeyValueStore provider_properties;
  Error error;
  EXPECT_TRUE(store.GetKeyValueStoreProperty(
      flimflam::kProviderProperty, &provider_properties, &error));
  if (!provider_properties.ContainsString(key)) {
    return false;
  }
  if (value != NULL) {
    *value = provider_properties.GetString(key);
  }
  return true;
}

TEST_F(VPNDriverTest, Load) {
  MockStore storage;
  GetArgs()->SetString(kHostProperty, "1.2.3.4");
  GetArgs()->SetString(kPSKProperty, "1234");
  EXPECT_CALL(storage, GetString(kStorageID, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(storage, GetString(_, kOTPProperty, _)).Times(0);
  EXPECT_CALL(storage, GetCryptedString(_, kOTPProperty, _)).Times(0);
  EXPECT_CALL(storage, GetString(kStorageID, kPortProperty, _))
      .WillOnce(DoAll(SetArgumentPointee<2>(string(kPort)), Return(true)));
  EXPECT_CALL(storage, GetString(kStorageID, kPINProperty, _))
      .WillOnce(DoAll(SetArgumentPointee<2>(string(kPIN)), Return(true)));
  EXPECT_CALL(storage, GetCryptedString(kStorageID, kPSKProperty, _))
      .WillOnce(Return(false));
  EXPECT_CALL(storage, GetCryptedString(kStorageID, kPasswordProperty, _))
      .WillOnce(DoAll(SetArgumentPointee<2>(string(kPassword)), Return(true)));
  EXPECT_TRUE(driver_.Load(&storage, kStorageID));
  EXPECT_EQ(kPort, GetArgs()->LookupString(kPortProperty, ""));
  EXPECT_EQ(kPIN, GetArgs()->LookupString(kPINProperty, ""));
  EXPECT_EQ(kPassword, GetArgs()->LookupString(kPasswordProperty, ""));

  // Properties missing from the persistent store should be deleted.
  EXPECT_FALSE(GetArgs()->ContainsString(kHostProperty));
  EXPECT_FALSE(GetArgs()->ContainsString(kPSKProperty));
}

TEST_F(VPNDriverTest, Save) {
  SetArg(flimflam::kProviderNameProperty, "");
  SetArg(kPINProperty, kPIN);
  SetArg(kPortProperty, kPort);
  SetArg(kPasswordProperty, kPassword);
  SetArg(kOTPProperty, "987654");
  MockStore storage;
  EXPECT_CALL(storage,
              SetString(kStorageID, flimflam::kProviderNameProperty, ""))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, SetString(kStorageID, kPortProperty, kPort))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, SetString(kStorageID, kPINProperty, kPIN))
      .WillOnce(Return(true));
  EXPECT_CALL(storage,
              SetCryptedString(kStorageID, kPasswordProperty, kPassword))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, SetCryptedString(_, kOTPProperty, _)).Times(0);
  EXPECT_CALL(storage, SetString(_, kOTPProperty, _)).Times(0);
  EXPECT_CALL(storage, DeleteKey(kStorageID, flimflam::kProviderNameProperty))
      .Times(0);
  EXPECT_CALL(storage, DeleteKey(kStorageID, kPSKProperty)).Times(1);
  EXPECT_CALL(storage, DeleteKey(kStorageID, kHostProperty)).Times(1);
  EXPECT_TRUE(driver_.Save(&storage, kStorageID, true));
}

TEST_F(VPNDriverTest, SaveNoCredentials) {
  SetArg(kPasswordProperty, kPassword);
  SetArg(kPSKProperty, "");
  MockStore storage;
  EXPECT_CALL(storage, SetString(_, kPasswordProperty, _)).Times(0);
  EXPECT_CALL(storage, SetCryptedString(_, kPasswordProperty, _)).Times(0);
  EXPECT_CALL(storage, DeleteKey(kStorageID, _)).Times(AnyNumber());
  EXPECT_CALL(storage, DeleteKey(kStorageID, kPasswordProperty)).Times(1);
  EXPECT_CALL(storage, DeleteKey(kStorageID, kPSKProperty)).Times(1);
  EXPECT_TRUE(driver_.Save(&storage, kStorageID, false));
}

TEST_F(VPNDriverTest, UnloadCredentials) {
  SetArg(kOTPProperty, "654321");
  SetArg(kPasswordProperty, kPassword);
  SetArg(kPortProperty, kPort);
  driver_.UnloadCredentials();
  EXPECT_FALSE(GetArgs()->ContainsString(kOTPProperty));
  EXPECT_FALSE(GetArgs()->ContainsString(kPasswordProperty));
  EXPECT_EQ(kPort, GetArgs()->LookupString(kPortProperty, ""));
}

TEST_F(VPNDriverTest, InitPropertyStore) {
  // Figure out if the store is actually hooked up to the driver argument
  // KeyValueStore.
  PropertyStore store;
  driver_.InitPropertyStore(&store);

  // An un-set property should not be readable.
  {
    Error error;
    EXPECT_FALSE(store.GetStringProperty(kPortProperty, NULL, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  EXPECT_FALSE(GetProviderProperty(store, kPortProperty, NULL));

  const string kProviderName = "boo";
  SetArg(kPortProperty, kPort);
  SetArg(kPasswordProperty, kPassword);
  SetArg(flimflam::kProviderNameProperty, kProviderName);
  SetArg(kHostProperty, "");

  // We should not be able to read a property out of the driver args using the
  // key to the args directly.
  {
    Error error;
    EXPECT_FALSE(store.GetStringProperty(kPortProperty, NULL, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }

  // We should instead be able to find it within the "Provider" stringmap.
  {
    string value;
    EXPECT_TRUE(GetProviderProperty(store, kPortProperty, &value));
    EXPECT_EQ(kPort, value);
  }

  // We should be able to read empty properties from the "Provider" stringmap.
  {
    string value;
    EXPECT_TRUE(GetProviderProperty(store, kHostProperty, &value));
    EXPECT_TRUE(value.empty());
  }

  // Properties that start with the prefix "Provider." should be mapped to the
  // name in the Properties dict with the prefix removed.
  {
    string value;
    EXPECT_TRUE(GetProviderProperty(store, flimflam::kNameProperty, &value));
    EXPECT_EQ(kProviderName, value);
  }

  // If we clear a property, we should no longer be able to find it.
  {
    Error error;
    EXPECT_TRUE(store.ClearProperty(kPortProperty, &error));
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_FALSE(GetProviderProperty(store, kPortProperty, NULL));
  }

  // A second attempt to clear this property should return an error.
  {
    Error error;
    EXPECT_FALSE(store.ClearProperty(kPortProperty, &error));
    EXPECT_EQ(Error::kNotFound, error.type());
  }

  // Test write only properties.
  EXPECT_FALSE(GetProviderProperty(store, kPINProperty, NULL));

  // Write properties to the driver args using the PropertyStore interface.
  {
    const string kValue = "some-value";
    Error error;
    EXPECT_TRUE(store.SetStringProperty(kPINProperty, kValue, &error));
    EXPECT_EQ(kValue, GetArgs()->GetString(kPINProperty));
  }
}

TEST_F(VPNDriverTest, ConnectTimeout) {
  EXPECT_EQ(&dispatcher_, dispatcher());
  EXPECT_TRUE(connect_timeout_callback().IsCancelled());
  EXPECT_FALSE(IsConnectTimeoutStarted());
  StartConnectTimeout(0);
  EXPECT_FALSE(connect_timeout_callback().IsCancelled());
  EXPECT_TRUE(IsConnectTimeoutStarted());
  set_dispatcher(NULL);
  StartConnectTimeout(0);  // Expect no crash.
  dispatcher_.DispatchPendingEvents();
  EXPECT_TRUE(connect_timeout_callback().IsCancelled());
  EXPECT_FALSE(IsConnectTimeoutStarted());
}

TEST_F(VPNDriverTest, StartStopConnectTimeout) {
  EXPECT_FALSE(IsConnectTimeoutStarted());
  EXPECT_EQ(0, connect_timeout_seconds());
  const int kTimeout = 123;
  StartConnectTimeout(kTimeout);
  EXPECT_TRUE(IsConnectTimeoutStarted());
  EXPECT_EQ(kTimeout, connect_timeout_seconds());
  StartConnectTimeout(kTimeout - 20);
  EXPECT_EQ(kTimeout, connect_timeout_seconds());
  StopConnectTimeout();
  EXPECT_FALSE(IsConnectTimeoutStarted());
  EXPECT_EQ(0, connect_timeout_seconds());
}

}  // namespace shill
