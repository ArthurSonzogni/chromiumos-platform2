// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/dbus_properties_proxy.h"

#include <utility>

#include <base/memory/weak_ptr.h>
#include <base/test/test_future.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "shill/dbus/fake_properties_proxy.h"
#include "shill/store/key_value_store.h"

namespace {

const char kInterface[] = "Modem";
const char kProperty1[] = "State";
const char kProperty2[] = "Model";
const brillo::VariantDictionary kTestDictionary = {
    {kProperty1, brillo::Any(1)},
    {kProperty2, brillo::Any("2")},
};

}  // namespace

namespace shill {

class DBusPropertiesProxyTest : public testing::Test {
 public:
  DBusPropertiesProxyTest()
      : dbus_properties_proxy_(
            DBusPropertiesProxy::CreateDBusPropertiesProxyForTesting(
                std::make_unique<FakePropertiesProxy>())) {
    static_cast<FakePropertiesProxy*>(
        dbus_properties_proxy_->GetDBusPropertiesProxyForTesting())
        ->SetDictionaryForTesting(kInterface, kTestDictionary);
  }
  ~DBusPropertiesProxyTest() override = default;

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  std::unique_ptr<DBusPropertiesProxy> dbus_properties_proxy_;
  FakePropertiesProxy* fake_properties_proxy_;
  base::WeakPtrFactory<DBusPropertiesProxyTest> weak_factory_{this};
};

TEST_F(DBusPropertiesProxyTest, GetAll) {
  KeyValueStore properties = dbus_properties_proxy_->GetAll(kInterface);
  EXPECT_EQ(properties.properties(), kTestDictionary);
}

TEST_F(DBusPropertiesProxyTest, GetAllAsync) {
  KeyValueStore properties;
  base::test::TestFuture<const KeyValueStore&> success_future;
  base::test::TestFuture<const Error&> error_future;
  dbus_properties_proxy_->GetAllAsync(kInterface, success_future.GetCallback(),
                                      error_future.GetCallback());
  EXPECT_EQ(success_future.Take().properties(), kTestDictionary);
  EXPECT_FALSE(error_future.IsReady());
}

TEST_F(DBusPropertiesProxyTest, Get) {
  brillo::Any property1 = dbus_properties_proxy_->Get(kInterface, kProperty1);
  EXPECT_EQ(property1, kTestDictionary.at(kProperty1));
  brillo::Any property2 = dbus_properties_proxy_->Get(kInterface, kProperty2);
  EXPECT_EQ(property2, kTestDictionary.at(kProperty2));
}

TEST_F(DBusPropertiesProxyTest, GetFailed) {
  const char kBadInterface[] = "bad interface";
  const char kBadProperty[] = "bad property";
  brillo::Any property =
      dbus_properties_proxy_->Get(kBadInterface, kBadProperty);
  EXPECT_TRUE(property.IsEmpty());
}

TEST_F(DBusPropertiesProxyTest, GetAsync) {
  base::test::TestFuture<const brillo::Any&> success_future;
  base::test::TestFuture<const Error&> error_future;

  dbus_properties_proxy_->GetAsync(kInterface, kProperty1,
                                   success_future.GetCallback(),
                                   error_future.GetCallback());
  EXPECT_EQ(success_future.Take(), kTestDictionary.at(kProperty1));
  EXPECT_FALSE(error_future.IsReady());
}

TEST_F(DBusPropertiesProxyTest, GetAsyncFailed) {
  const char kBadInterface[] = "bad interface";
  const char kBadProperty[] = "bad property";
  base::test::TestFuture<const brillo::Any&> success_future;
  base::test::TestFuture<const Error&> error_future;
  dbus_properties_proxy_->GetAsync(kBadInterface, kBadProperty,
                                   success_future.GetCallback(),
                                   error_future.GetCallback());
  EXPECT_EQ(error_future.Take().type(), Error::kOperationFailed);
  EXPECT_FALSE(success_future.IsReady());
}

}  // namespace shill
