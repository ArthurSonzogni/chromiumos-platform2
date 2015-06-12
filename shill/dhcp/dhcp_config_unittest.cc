// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp/dhcp_config.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/minijail/mock_minijail.h>

#include "shill/dbus_adaptor.h"
#include "shill/dhcp/dhcp_provider.h"
#include "shill/dhcp/mock_dhcp_proxy.h"
#include "shill/event_dispatcher.h"
#include "shill/mock_control.h"
#include "shill/mock_glib.h"
#include "shill/mock_log.h"
#include "shill/mock_metrics.h"
#include "shill/mock_proxy_factory.h"
#include "shill/property_store_unittest.h"
#include "shill/testing.h"

using base::Bind;
using base::FilePath;
using base::ScopedTempDir;
using base::Unretained;
using chromeos::MockMinijail;
using std::string;
using std::unique_ptr;
using std::vector;
using testing::_;
using testing::AnyNumber;
using testing::ContainsRegex;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Return;
using testing::SetArgumentPointee;
using testing::Test;

namespace shill {

namespace {
const char kDeviceName[] = "eth0";
const char kDhcpMethod[] = "dhcp";
const char kLeaseFileSuffix[] = "leasefilesuffix";
const bool kHasLeaseSuffix = true;
}  // namespace

class TestDHCPConfig : public DHCPConfig {
 public:
  TestDHCPConfig(ControlInterface *control_interface,
                 EventDispatcher *dispatcher,
                 DHCPProvider *provider,
                 const std::string &device_name,
                 const std::string &type,
                 const std::string &lease_file_suffix,
                 GLib *glib)
     : DHCPConfig(control_interface,
                  dispatcher,
                  provider,
                  device_name,
                  type,
                  lease_file_suffix,
                  glib) {}

  ~TestDHCPConfig() {}

  void ProcessEventSignal(const std::string &reason,
                          const Configuration &configuration) override {}
  void ProcessStatusChangeSignal(const std::string &status) override {}

  MOCK_METHOD0(ShouldFailOnAcquisitionTimeout, bool());
  MOCK_METHOD0(ShouldKeepLeaseOnDisconnect, bool());
};

typedef scoped_refptr<TestDHCPConfig> TestDHCPConfigRefPtr;

class DHCPConfigTest : public PropertyStoreTest {
 public:
  DHCPConfigTest()
      : proxy_(new MockDHCPProxy()),
        minijail_(new MockMinijail()),
        config_(new TestDHCPConfig(&control_,
                                   dispatcher(),
                                   DHCPProvider::GetInstance(),
                                   kDeviceName,
                                   kDhcpMethod,
                                   kLeaseFileSuffix,
                                   glib())) {}

  virtual void SetUp() {
    config_->proxy_factory_ = &proxy_factory_;
    config_->minijail_ = minijail_.get();
  }

  virtual void TearDown() {
    config_->proxy_factory_ = nullptr;
    config_->minijail_ = nullptr;
  }

  void StopInstance() {
    config_->Stop("In test");
  }

  TestDHCPConfigRefPtr CreateMockMinijailConfig(const string &lease_suffix);

 protected:
  static const int kPID;
  static const unsigned int kTag;

  unique_ptr<MockDHCPProxy> proxy_;
  MockProxyFactory proxy_factory_;
  MockControl control_;
  unique_ptr<MockMinijail> minijail_;
  TestDHCPConfigRefPtr config_;
};

const int DHCPConfigTest::kPID = 123456;
const unsigned int DHCPConfigTest::kTag = 77;

TestDHCPConfigRefPtr DHCPConfigTest::CreateMockMinijailConfig(
    const string &lease_suffix) {
  TestDHCPConfigRefPtr config(new TestDHCPConfig(&control_,
                                                 dispatcher(),
                                                 DHCPProvider::GetInstance(),
                                                 kDeviceName,
                                                 kDhcpMethod,
                                                 lease_suffix,
                                                 glib()));
  config->minijail_ = minijail_.get();

  return config;
}

TEST_F(DHCPConfigTest, InitProxy) {
  static const char kService[] = ":1.200";
  EXPECT_TRUE(proxy_.get());
  EXPECT_FALSE(config_->proxy_.get());
  EXPECT_CALL(proxy_factory_, CreateDHCPProxy(kService))
      .WillOnce(ReturnAndReleasePointee(&proxy_));
  config_->InitProxy(kService);
  EXPECT_FALSE(proxy_.get());
  EXPECT_TRUE(config_->proxy_.get());

  config_->InitProxy(kService);
}

TEST_F(DHCPConfigTest, StartFail) {
  EXPECT_CALL(*minijail_, RunAndDestroy(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(*glib(), ChildWatchAdd(_, _, _)).Times(0);
  EXPECT_FALSE(config_->Start());
  EXPECT_EQ(0, config_->pid_);
}

MATCHER_P(IsDHCPCDArgs, has_lease_suffix, "") {
  if (string(arg[0]) != "/sbin/dhcpcd" ||
      string(arg[1]) != "-B" ||
      string(arg[2]) != "-q") {
    return false;
  }

  int end_offset = 3;

  string device_arg = has_lease_suffix ?
      string(kDeviceName) + "=" + string(kLeaseFileSuffix) : kDeviceName;
  return string(arg[end_offset]) == device_arg &&
         arg[end_offset + 1] == nullptr;
}

TEST_F(DHCPConfigTest, StartWithoutLeaseSuffix) {
  TestDHCPConfigRefPtr config = CreateMockMinijailConfig(kDeviceName);
  EXPECT_CALL(*minijail_, RunAndDestroy(_, IsDHCPCDArgs(!kHasLeaseSuffix), _))
      .WillOnce(Return(false));
  EXPECT_FALSE(config->Start());
}

namespace {

class DHCPConfigCallbackTest : public DHCPConfigTest {
 public:
  virtual void SetUp() {
    DHCPConfigTest::SetUp();
    config_->RegisterUpdateCallback(
        Bind(&DHCPConfigCallbackTest::SuccessCallback, Unretained(this)));
    config_->RegisterFailureCallback(
        Bind(&DHCPConfigCallbackTest::FailureCallback, Unretained(this)));
    ip_config_ = config_;
  }

  MOCK_METHOD2(SuccessCallback,
               void(const IPConfigRefPtr &ipconfig, bool new_lease_acquired));
  MOCK_METHOD1(FailureCallback, void(const IPConfigRefPtr &ipconfig));

  // The mock methods above take IPConfigRefPtr because this is the type
  // that the registered callbacks take.  This conversion of the DHCP
  // config ref pointer eases our work in setting up expectations.
  const IPConfigRefPtr &ConfigRef() { return ip_config_; }

 private:
  IPConfigRefPtr ip_config_;
};

void DoNothing() {}

}  // namespace

TEST_F(DHCPConfigCallbackTest, NotifyFailure) {
  EXPECT_CALL(*this, SuccessCallback(_, _)).Times(0);
  EXPECT_CALL(*this, FailureCallback(ConfigRef()));
  config_->lease_acquisition_timeout_callback_.Reset(base::Bind(&DoNothing));
  config_->lease_expiration_callback_.Reset(base::Bind(&DoNothing));
  config_->NotifyFailure();
  Mock::VerifyAndClearExpectations(this);
  EXPECT_TRUE(config_->properties().address.empty());
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
}

TEST_F(DHCPConfigCallbackTest, StoppedDuringFailureCallback) {
  // Stop the DHCP config while it is calling the failure callback.  We
  // need to ensure that no callbacks are left running inadvertently as
  // a result.
  EXPECT_CALL(*this, FailureCallback(ConfigRef()))
      .WillOnce(InvokeWithoutArgs(this, &DHCPConfigTest::StopInstance));
  config_->NotifyFailure();
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(this));
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
}

TEST_F(DHCPConfigCallbackTest, StoppedDuringSuccessCallback) {
  IPConfig::Properties properties;
  properties.address = "1.2.3.4";
  properties.lease_duration_seconds = 1;
  // Stop the DHCP config while it is calling the success callback.  This
  // can happen if the device has a static IP configuration and releases
  // the lease after accepting other network parameters from the DHCP
  // IPConfig properties.  We need to ensure that no callbacks are left
  // running inadvertently as a result.
  EXPECT_CALL(*this, SuccessCallback(ConfigRef(), true))
      .WillOnce(InvokeWithoutArgs(this, &DHCPConfigTest::StopInstance));
  config_->UpdateProperties(properties, true);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(this));
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
}

TEST_F(DHCPConfigCallbackTest, ProcessAcquisitionTimeout) {
  // Do not fail on acquisition timeout (e.g. ARP gateway is active).
  EXPECT_CALL(*config_.get(), ShouldFailOnAcquisitionTimeout())
      .WillOnce(Return(false));
  EXPECT_CALL(*this, FailureCallback(_)).Times(0);
  config_->ProcessAcquisitionTimeout();
  Mock::VerifyAndClearExpectations(this);
  Mock::VerifyAndClearExpectations(config_.get());

  // Fail on acquisition timeout.
  EXPECT_CALL(*config_.get(), ShouldFailOnAcquisitionTimeout())
      .WillOnce(Return(true));
  EXPECT_CALL(*this, FailureCallback(_)).Times(1);
  config_->ProcessAcquisitionTimeout();
  Mock::VerifyAndClearExpectations(this);
  Mock::VerifyAndClearExpectations(config_.get());
}

TEST_F(DHCPConfigTest, ReleaseIP) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.
  EXPECT_CALL(*proxy_, Release(kDeviceName)).Times(1);
  config_->proxy_.reset(proxy_.release());
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonDisconnect));
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, KeepLeaseOnDisconnect) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.

  // Keep lease on disconnect (e.g. ARP gateway is enabled).
  EXPECT_CALL(*config_.get(), ShouldKeepLeaseOnDisconnect())
      .WillOnce(Return(true));
  EXPECT_CALL(*proxy_, Release(kDeviceName)).Times(0);
  config_->proxy_.reset(proxy_.release());
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonDisconnect));
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, ReleaseLeaseOnDisconnect) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.

  // Release lease on disconnect.
  EXPECT_CALL(*config_.get(), ShouldKeepLeaseOnDisconnect())
      .WillOnce(Return(false));
  EXPECT_CALL(*proxy_, Release(kDeviceName)).Times(1);
  config_->proxy_.reset(proxy_.release());
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonDisconnect));
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, ReleaseIPStaticIPWithLease) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.
  config_->is_lease_active_ = true;
  EXPECT_CALL(*proxy_, Release(kDeviceName));
  config_->proxy_.reset(proxy_.release());
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonStaticIP));
  EXPECT_EQ(nullptr, config_->proxy_.get());
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, ReleaseIPStaticIPWithoutLease) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.
  config_->is_lease_active_ = false;
  EXPECT_CALL(*proxy_, Release(kDeviceName)).Times(0);
  MockDHCPProxy *proxy_pointer = proxy_.get();
  config_->proxy_.reset(proxy_.release());
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonStaticIP));
  // Expect that proxy has not been released.
  EXPECT_EQ(proxy_pointer, config_->proxy_.get());
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, RenewIP) {
  EXPECT_CALL(*minijail_, RunAndDestroy(_, _, _)).WillOnce(Return(false));
  config_->pid_ = 0;
  EXPECT_FALSE(config_->RenewIP());  // Expect a call to Start() if pid_ is 0.
  Mock::VerifyAndClearExpectations(minijail_.get());
  EXPECT_CALL(*minijail_, RunAndDestroy(_, _, _)).Times(0);
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  config_->lease_expiration_callback_.Reset(base::Bind(&DoNothing));
  config_->pid_ = 456;
  EXPECT_FALSE(config_->RenewIP());  // Expect no crash with NULL proxy.
  EXPECT_CALL(*proxy_, Rebind(kDeviceName)).Times(1);
  config_->proxy_.reset(proxy_.release());
  EXPECT_TRUE(config_->RenewIP());
  EXPECT_FALSE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, RequestIP) {
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  config_->pid_ = 567;
  EXPECT_CALL(*proxy_, Rebind(kDeviceName)).Times(1);
  config_->proxy_.reset(proxy_.release());
  EXPECT_TRUE(config_->RenewIP());
  EXPECT_FALSE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  config_->pid_ = 0;
}

TEST_F(DHCPConfigCallbackTest, RequestIPTimeout) {
  EXPECT_CALL(*config_.get(), ShouldFailOnAcquisitionTimeout())
      .WillOnce(Return(true));
  EXPECT_CALL(*this, SuccessCallback(_, _)).Times(0);
  EXPECT_CALL(*this, FailureCallback(ConfigRef()));
  config_->lease_acquisition_timeout_seconds_ = 0;
  config_->pid_ = 567;
  EXPECT_CALL(*proxy_, Rebind(kDeviceName)).Times(1);
  config_->proxy_.reset(proxy_.release());
  config_->RenewIP();
  config_->dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(this);
  Mock::VerifyAndClearExpectations(config_.get());
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, Restart) {
  const int kPID1 = 1 << 17;  // Ensure unknown positive PID.
  const int kPID2 = 987;
  const unsigned int kTag1 = 11;
  const unsigned int kTag2 = 22;
  config_->pid_ = kPID1;
  config_->child_watch_tag_ = kTag1;
  DHCPProvider::GetInstance()->BindPID(kPID1, config_);
  EXPECT_CALL(*glib(), SourceRemove(kTag1)).WillOnce(Return(true));
  EXPECT_CALL(*minijail_, RunAndDestroy(_, _, _)).WillOnce(
      DoAll(SetArgumentPointee<2>(kPID2), Return(true)));
  EXPECT_CALL(*glib(), ChildWatchAdd(kPID2, _, _)).WillOnce(Return(kTag2));
  EXPECT_TRUE(config_->Restart());
  EXPECT_EQ(kPID2, config_->pid_);
  EXPECT_EQ(config_.get(), DHCPProvider::GetInstance()->GetConfig(kPID2).get());
  EXPECT_EQ(kTag2, config_->child_watch_tag_);
  DHCPProvider::GetInstance()->UnbindPID(kPID2);
  config_->pid_ = 0;
  config_->child_watch_tag_ = 0;
}

TEST_F(DHCPConfigTest, RestartNoClient) {
  const int kPID = 777;
  const unsigned int kTag = 66;
  EXPECT_CALL(*glib(), SourceRemove(_)).Times(0);
  EXPECT_CALL(*minijail_, RunAndDestroy(_, _, _)).WillOnce(
      DoAll(SetArgumentPointee<2>(kPID), Return(true)));
  EXPECT_CALL(*glib(), ChildWatchAdd(kPID, _, _)).WillOnce(Return(kTag));
  EXPECT_TRUE(config_->Restart());
  EXPECT_EQ(kPID, config_->pid_);
  EXPECT_EQ(config_.get(), DHCPProvider::GetInstance()->GetConfig(kPID).get());
  EXPECT_EQ(kTag, config_->child_watch_tag_);
  DHCPProvider::GetInstance()->UnbindPID(kPID);
  config_->pid_ = 0;
  config_->child_watch_tag_ = 0;
}

TEST_F(DHCPConfigCallbackTest, StartTimeout) {
  EXPECT_CALL(*config_.get(), ShouldFailOnAcquisitionTimeout())
      .WillOnce(Return(true));
  EXPECT_CALL(*this, SuccessCallback(_, _)).Times(0);
  EXPECT_CALL(*this, FailureCallback(ConfigRef()));
  config_->lease_acquisition_timeout_seconds_ = 0;
  config_->proxy_.reset(proxy_.release());
  EXPECT_CALL(*minijail_, RunAndDestroy(_, _, _)).WillOnce(Return(true));
  config_->Start();
  config_->dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(this);
  Mock::VerifyAndClearExpectations(config_.get());
}

TEST_F(DHCPConfigTest, Stop) {
  const int kPID = 1 << 17;  // Ensure unknown positive PID.
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, ContainsRegex(
      base::StringPrintf("Stopping.+%s", __func__))));
  config_->pid_ = kPID;
  DHCPProvider::GetInstance()->BindPID(kPID, config_);
  config_->lease_acquisition_timeout_callback_.Reset(base::Bind(&DoNothing));
  config_->lease_expiration_callback_.Reset(base::Bind(&DoNothing));
  config_->Stop(__func__);
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
  EXPECT_FALSE(DHCPProvider::GetInstance()->GetConfig(kPID));
  EXPECT_FALSE(config_->pid_);
}

TEST_F(DHCPConfigTest, StopDuringRequestIP) {
  config_->pid_ = 567;
  EXPECT_CALL(*proxy_, Rebind(kDeviceName)).Times(1);
  config_->proxy_.reset(proxy_.release());
  EXPECT_TRUE(config_->RenewIP());
  EXPECT_FALSE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  config_->pid_ = 0;  // Keep Stop from killing a real process.
  config_->Stop(__func__);
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
}

TEST_F(DHCPConfigTest, SetProperty) {
  ::DBus::Error error;
  // Ensure that an attempt to write a R/O property returns InvalidArgs error.
  EXPECT_FALSE(DBusAdaptor::SetProperty(config_->mutable_store(),
                                        kAddressProperty,
                                        PropertyStoreTest::kStringV,
                                        &error));
  ASSERT_TRUE(error.is_set());  // name() may be invalid otherwise
  EXPECT_EQ(invalid_args(), error.name());
}

}  // namespace shill
