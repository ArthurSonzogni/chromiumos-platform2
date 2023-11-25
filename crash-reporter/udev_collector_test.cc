// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/udev_collector.h"

#include <memory>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/stringprintf.h>
#include <bindings/cloud_policy.pb.h>
#include <bindings/device_management_backend.pb.h>
#include <brillo/strings/string_utils.h>
#include <brillo/syslog_logging.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <crash-reporter-client/crash-reporter/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <login_manager/proto_bindings/policy_descriptor.pb.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>
#include <session_manager-client-test/session_manager/dbus-proxy-mocks.h>

#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

using base::FilePath;

namespace {

// Bluetooth devcoredump feature flag path
// TODO(b/203034370): Remove this once the feature is fully launched and the
// feature flag is removed.
constexpr char kBluetoothDumpFlagPath[] = "/run/bluetooth/coredump_disabled";

constexpr char kFbpreprocessordBaseDirectory[] =
    "/run/daemon-store/fbpreprocessord/user_hash/raw_dumps";

// Dummy log config file name.
const char kLogConfigFileName[] = "log_config_file";

// Dummy directory for storing device coredumps.
const char kDevCoredumpDirectory[] = "devcoredump";

// A bunch of random rules to put into the dummy log config file.
const char kLogConfigFileContents[] =
    "crash_reporter-udev-collection-change-card0-drm=echo change card0 drm\n"
    "crash_reporter-udev-collection-add-state0-cpu=echo change state0 cpu\n"
    "crash_reporter-udev-collection-devcoredump-iwlwifi=echo devcoredump\n"
    "cros_installer=echo not for udev\n"
    "bt_firmware=echo bluetooth devcoredump\n";

const char kCrashLogFilePattern[] = "*.log.gz";
const char kDevCoredumpFilePattern[] = "*.devcore.gz";
const char kBluetoothCoredumpFilePattern[] = "bt_firmware.*";
const char kWiFiCoredumpFilePattern[] = "devcoredump_iwlwifi.*.devcore.gz";

// Dummy content for device coredump data file.
const char kDevCoredumpDataContents[] = "coredump";

// Driver name for a coredump that should not be collected:
const char kNoCollectDriverName[] = "disallowed_driver";

// BT tests should be using BT specific driver name.
constexpr char kConnectivityBTDriverName[] = "bt_driver";
constexpr char kConnectivityWiFiDriverName[] = "iwlwifi";
constexpr char kDeviceGoogleUser[] = "alice@google.com";
constexpr char kDeviceUserInAllowlist[] = "testuser@managedchrome.com";
constexpr char kDeviceGmailUser[] = "alice@gmail.com";
constexpr char kAffiliationID[] = "affiliation_id";

// Driver names for a coredump that should be collected:
constexpr const char* kCollectedDriverNames[] = {"adreno", "qcom-venus", "amdgpu"};

const char kCrashReporterInterface[] = "org.chromium.CrashReporterInterface";
const char kDebugDumpCreatedSignalName[] = "DebugDumpCreated";

const char kWiFiMetaFilePattern[] = "devcoredump_iwlwifi.*.meta";
const char kWiFiCrashLogFilePattern[] = "devcoredump_iwlwifi.*.log";

// Returns the number of files found in the given path that matches the
// specified file name pattern.
int GetNumFiles(const FilePath& path, const std::string& file_pattern) {
  base::FileEnumerator enumerator(path, false, base::FileEnumerator::FILES,
                                  file_pattern);
  int num_files = 0;
  for (FilePath file_path = enumerator.Next(); !file_path.value().empty();
       file_path = enumerator.Next()) {
    num_files++;
  }
  return num_files;
}

}  // namespace

using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;
using ::testing::WithArgs;

class UdevCollectorMock : public UdevCollector {
 public:
  UdevCollectorMock()
      : UdevCollector(
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}
  MOCK_METHOD(void, SetUpDBus, (), (override));

  void SetSessionManagerProxy(
      org::chromium::SessionManagerInterfaceProxyMock* mock) {
    // This will take ownership of the mock, and the mock will be
    // deleted when the UdevCollectorMock is deleted.
    session_manager_proxy_.reset(mock);
  }

  void SetBus(scoped_refptr<dbus::MockBus> bus) { bus_ = bus.get(); }
};

class UdevCollectorTest : public ::testing::Test {
 protected:
  base::ScopedTempDir temp_dir_generator_;

  void HandleCrash(const std::string& udev_event) {
    collector_.HandleCrash(udev_event);
  }

  void GenerateDevCoredump(const std::string& device_name,
                           const std::string& driver_name) {
    // Generate coredump data file.
    ASSERT_TRUE(CreateDirectory(FilePath(
        base::StringPrintf("%s/%s", collector_.dev_coredump_directory_.c_str(),
                           device_name.c_str()))));
    FilePath data_path = FilePath(base::StringPrintf(
        "%s/%s/data", collector_.dev_coredump_directory_.c_str(),
        device_name.c_str()));
    ASSERT_TRUE(test_util::CreateFile(data_path, kDevCoredumpDataContents));
    // Generate uevent file for failing device.
    ASSERT_TRUE(CreateDirectory(FilePath(base::StringPrintf(
        "%s/%s/failing_device", collector_.dev_coredump_directory_.c_str(),
        device_name.c_str()))));
    FilePath uevent_path = FilePath(base::StringPrintf(
        "%s/%s/failing_device/uevent",
        collector_.dev_coredump_directory_.c_str(), device_name.c_str()));
    ASSERT_TRUE(
        test_util::CreateFile(uevent_path, "DRIVER=" + driver_name + "\n"));
  }

  // This function creates fbpreprocessord daemon-store directory and sets
  // it to expected user, mode and group. The test invoking this function
  // need to run as root to be able to change the group and ownership.
  void CreateFbpreprocessordDirectoryForTest(UdevCollectorMock* collector) {
    const int kFbPreprocessordAccessGid = 429;
    const int kFbPreprocessordUid = 20213;
    const mode_t kExpectedMode = 03770;

    FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
    ASSERT_TRUE(base::CreateDirectory(user_hash_path));
    ASSERT_EQ(chown(user_hash_path.value().c_str(), kFbPreprocessordUid,
                    kFbPreprocessordAccessGid),
              0)
        << strerrordesc_np(errno);
    ASSERT_EQ(chmod(user_hash_path.value().c_str(), kExpectedMode), 0)
        << strerrordesc_np(errno);
  }

  void SetupFirmwareDumpsFinchFlag(const std::string& val) {
    FilePath fwdump_allowed_path =
        paths::Get(paths::kAllowFirmwareDumpsFlagPath);
    ASSERT_TRUE(test_util::CreateFile(fwdump_allowed_path, val));
  }

  void SetUpCollector(UdevCollectorMock* collector) {
    // Reset the g_test_prefix in Path.
    paths::SetPrefixForTesting(temp_dir_generator_.GetPath());
    EXPECT_CALL(*collector, SetUpDBus()).WillRepeatedly(testing::Return());
    collector->Initialize(false);

    collector->log_config_path_ = log_config_path_;
    collector->set_crash_directory_for_test(temp_dir_generator_.GetPath());

    FilePath dev_coredump_path =
        temp_dir_generator_.GetPath().Append(kDevCoredumpDirectory);
    collector->dev_coredump_directory_ = dev_coredump_path.value();
    SetupFirmwareDumpsFinchFlag("1");
    collector->EnableConnectivityFwdumpForTest(true);
    dbus::Bus::Options bus_options;
    mock_bus_ = base::MakeRefCounted<dbus::MockBus>(bus_options);
  }

  // This function creates input request blob required to call
  // RetrievePolicyEx() function.
  std::vector<uint8_t> CreateExpectedDescriptorBlob(
      const login_manager::PolicyAccountType& type, const std::string& user) {
    login_manager::PolicyDescriptor descriptor;
    descriptor.set_domain(login_manager::POLICY_DOMAIN_CHROME);
    descriptor.set_account_id(user);

    if (type == login_manager::PolicyAccountType::ACCOUNT_TYPE_USER) {
      descriptor.set_account_type(
          login_manager::PolicyAccountType::ACCOUNT_TYPE_USER);
    } else if (type == login_manager::PolicyAccountType::ACCOUNT_TYPE_DEVICE) {
      descriptor.set_account_type(
          login_manager::PolicyAccountType::ACCOUNT_TYPE_DEVICE);
    } else {
      CHECK(false);
    }

    std::string descriptor_string = descriptor.SerializeAsString();
    return std::vector<uint8_t>(descriptor_string.begin(),
                                descriptor_string.end());
  }

  void OnDebugDumpCreated(dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), kCrashReporterInterface);
    EXPECT_EQ(signal->GetMember(), kDebugDumpCreatedSignalName);
  }

  void SetDebugDumpCreatedSignalExpectation(
      UdevCollectorMock* collector,
      scoped_refptr<dbus::MockExportedObject> exported_object,
      dbus::ObjectPath obj_path) {
    EXPECT_CALL(*exported_object, ExportMethodAndBlock(_, _, _))
        .WillRepeatedly(testing::Return(true));

    EXPECT_CALL(*mock_bus_, GetExportedObject(testing::Eq(obj_path)))
        .WillOnce(testing::Return(exported_object.get()));

    EXPECT_CALL(*mock_bus_, RequestOwnershipAndBlock(_, _))
        .WillOnce(testing::Return(true));

    EXPECT_CALL(*exported_object, Unregister())
        .WillRepeatedly(testing::Return());

    EXPECT_CALL(*exported_object, SendSignal(testing::A<dbus::Signal*>()))
        .WillOnce(Invoke(this, &UdevCollectorTest::OnDebugDumpCreated));
    collector->SetBus(mock_bus_);
  }

  // This function creates Policy Fetch Response blob to simulate expected
  // response of RetrievePolicyEx() function call.
  std::vector<uint8_t> CreatePolicyFetchResponseBlob(
      const login_manager::PolicyAccountType& type,
      const std::string& affiliation_id,
      const std::string& policy_val) {
    enterprise_management::PolicyData policy_data;
    enterprise_management::CloudPolicySettings user_policy_val;
    // Add policy required for connectivity fwdumps.
    user_policy_val.mutable_subproto1()
        ->mutable_userfeedbackwithlowleveldebugdataallowed()
        ->mutable_value()
        ->add_entries(policy_val);
    std::string serialized_user_policy = user_policy_val.SerializeAsString();
    policy_data.set_policy_value(serialized_user_policy);

    if (type == login_manager::PolicyAccountType::ACCOUNT_TYPE_USER) {
      auto id = policy_data.add_user_affiliation_ids();
      *id = affiliation_id;
    } else if (type == login_manager::PolicyAccountType::ACCOUNT_TYPE_DEVICE) {
      auto id = policy_data.add_device_affiliation_ids();
      *id = affiliation_id;
    } else {
      CHECK(false);
    }

    enterprise_management::PolicyFetchResponse response;
    CHECK(policy_data.SerializeToString(response.mutable_policy_data()));
    auto serialized = response.SerializeAsString();
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
  }

  UdevCollectorMock collector_;
  scoped_refptr<dbus::MockBus> mock_bus_;

 private:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_generator_.CreateUniqueTempDir());
    log_config_path_ = temp_dir_generator_.GetPath().Append(kLogConfigFileName);

    SetUpCollector(&collector_);
    // Write to a dummy log config file.
    ASSERT_TRUE(
        test_util::CreateFile(log_config_path_, kLogConfigFileContents));
    brillo::ClearLog();
  }

  void TearDown() override { paths::SetPrefixForTesting(base::FilePath()); }

  FilePath log_config_path_;
};

TEST_F(UdevCollectorTest, TestNoMatch) {
  // No rule should match this.
  HandleCrash("ACTION=change:KERNEL=foo:SUBSYSTEM=bar");
  EXPECT_EQ(0,
            GetNumFiles(temp_dir_generator_.GetPath(), kCrashLogFilePattern));
}

TEST_F(UdevCollectorTest, TestMatches) {
  // Try multiple udev events in sequence.  The number of log files generated
  // should increase.
  HandleCrash("ACTION=change:KERNEL=card0:SUBSYSTEM=drm");
  EXPECT_EQ(1,
            GetNumFiles(temp_dir_generator_.GetPath(), kCrashLogFilePattern));

  // Each collector is only allowed to handle one crash, so create a second
  // collector for the second crash.
  UdevCollectorMock second_collector;
  SetUpCollector(&second_collector);
  second_collector.HandleCrash("ACTION=add:KERNEL=state0:SUBSYSTEM=cpu");
  EXPECT_EQ(2,
            GetNumFiles(temp_dir_generator_.GetPath(), kCrashLogFilePattern));
}

TEST_F(UdevCollectorTest, TestDevCoredump) {
  GenerateDevCoredump("devcd0", kNoCollectDriverName);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  // IsDeveloperImage() returns false while running this test so devcoredumps
  // will not be added to the crash directory.
  EXPECT_EQ(
      0, GetNumFiles(temp_dir_generator_.GetPath(), kDevCoredumpFilePattern));
  GenerateDevCoredump("devcd1", kNoCollectDriverName);
  // Each collector is only allowed to handle one crash, so create a second
  // collector for the second crash.
  UdevCollectorMock second_collector;
  SetUpCollector(&second_collector);
  second_collector.HandleCrash(
      "ACTION=add:KERNEL_NUMBER=1:SUBSYSTEM=devcoredump");
  EXPECT_EQ(
      0, GetNumFiles(temp_dir_generator_.GetPath(), kDevCoredumpFilePattern));
}

// Ensure that subsequent fwdumps are generated on back to back udev events
// for allowed users.
TEST_F(UdevCollectorTest,
       RunAsRoot_TestConnectivityWiFiDevCoredumpUserAllowed) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);

  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);
  CreateFbpreprocessordDirectoryForTest(&collector_);
  auto obj_path = dbus::ObjectPath(crash_reporter::kCrashReporterServicePath);
  auto exported_object =
      base::MakeRefCounted<dbus::MockExportedObject>(mock_bus_.get(), obj_path);

  SetDebugDumpCreatedSignalExpectation(&collector_, exported_object, obj_path);

  EXPECT_CALL(*mock, RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceGoogleUser;
            *sanitized = "user_hash";
            return true;
          })));

  EXPECT_CALL(
      *mock,
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceGoogleUser),
                       _, _, _))
      .WillRepeatedly(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "wifi");
        return true;
      })));

  FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(1, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));

  // Generate another coredump and check additional fwdump file is generated.
  GenerateDevCoredump("devcd1", kConnectivityWiFiDriverName);
  // Each collector is only allowed to handle one crash, so create a second
  // collector for the second crash.
  UdevCollectorMock second_collector;
  SetUpCollector(&second_collector);
  auto* mock2 = new org::chromium::SessionManagerInterfaceProxyMock;
  second_collector.SetSessionManagerProxy(mock2);
  CreateFbpreprocessordDirectoryForTest(&second_collector);
  SetDebugDumpCreatedSignalExpectation(&second_collector, exported_object,
                                       obj_path);

  EXPECT_CALL(*mock2, RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceGoogleUser;
            *sanitized = "user_hash";
            return true;
          })));

  EXPECT_CALL(
      *mock2,
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceGoogleUser),
                       _, _, _))
      .WillRepeatedly(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "wifi");
        return true;
      })));
  second_collector.HandleCrash(
      "ACTION=add:KERNEL_NUMBER=1:SUBSYSTEM=devcoredump");
  EXPECT_EQ(2, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));
}

// Ensure that connectivity fwdump is generated for user in allowlist.
TEST_F(UdevCollectorTest,
       RunAsRoot_TestConnectivityWiFiDevCoredumpUserInAllowList) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);

  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);
  CreateFbpreprocessordDirectoryForTest(&collector_);
  auto obj_path = dbus::ObjectPath(crash_reporter::kCrashReporterServicePath);
  auto exported_object =
      base::MakeRefCounted<dbus::MockExportedObject>(mock_bus_.get(), obj_path);

  SetDebugDumpCreatedSignalExpectation(&collector_, exported_object, obj_path);

  EXPECT_CALL(*mock, RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceUserInAllowlist;
            *sanitized = "user_hash";
            return true;
          })));

  EXPECT_CALL(
      *mock,
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceUserInAllowlist),
                       _, _, _))
      .WillRepeatedly(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "wifi");
        return true;
      })));

  FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(1, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));
}

// For connectivity firmware dump we do not want .meta and .log file. This
// test ensures that .meta and .log files are not generated.
TEST_F(UdevCollectorTest, RunAsRoot_TestEnsureOnlyDevcoredumpFileIsGenerated) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);

  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);
  CreateFbpreprocessordDirectoryForTest(&collector_);
  auto obj_path = dbus::ObjectPath(crash_reporter::kCrashReporterServicePath);
  auto exported_object =
      base::MakeRefCounted<dbus::MockExportedObject>(mock_bus_.get(), obj_path);

  SetDebugDumpCreatedSignalExpectation(&collector_, exported_object, obj_path);

  EXPECT_CALL(*mock, RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceUserInAllowlist;
            *sanitized = "user_hash";
            return true;
          })));

  EXPECT_CALL(
      *mock,
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceUserInAllowlist),
                       _, _, _))
      .WillRepeatedly(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "wifi");
        return true;
      })));

  FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(1, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));

  // Ensure .meta and .log file are not generated.
  EXPECT_EQ(0, GetNumFiles(user_hash_path, kWiFiMetaFilePattern));
  EXPECT_EQ(0, GetNumFiles(user_hash_path, kWiFiCrashLogFilePattern));
}

// Ensure fwdump is generated if policy is set and user is allowed.
TEST_F(UdevCollectorTest, RunAsRoot_TestConnectivityWiFiDevCoredumpPolicySet) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);
  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);
  CreateFbpreprocessordDirectoryForTest(&collector_);
  auto obj_path = dbus::ObjectPath(crash_reporter::kCrashReporterServicePath);
  auto exported_object =
      base::MakeRefCounted<dbus::MockExportedObject>(mock_bus_.get(), obj_path);
  SetDebugDumpCreatedSignalExpectation(&collector_, exported_object, obj_path);

  EXPECT_CALL(*mock, RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceGoogleUser;
            *sanitized = "user_hash";
            return true;
          })));

  EXPECT_CALL(
      *mock,
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceGoogleUser),
                       _, _, _))
      .WillRepeatedly(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "wifi");
        return true;
      })));

  FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(1, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));
}

// Ensure fwdump is not generated when disallowed by finch.
TEST_F(UdevCollectorTest, TestConnectivityWiFiDevCoredumpDisabledByFinch) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);
  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);
  SetupFirmwareDumpsFinchFlag("0");

  FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(0, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));
}

// Ensure fwdump is not generated when finch cache file does not exist.
TEST_F(UdevCollectorTest,
       TestConnectivityWiFiDevCoredumpDisabledNoFinchFilePresent) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);
  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);

  FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(0, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));
}

// Ensure fwdump is not generated when finch flag has unexpected value.
TEST_F(UdevCollectorTest,
       TestConnectivityWiFiDevCoredumpDisabledByCorruptFinchValue) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);
  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);

  // AllowedFirmwareDumpsFlagPath contains some corrupted value.
  SetupFirmwareDumpsFinchFlag("10");

  FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(0, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));
}

// Ensure fwdump is not generated when there is trailing whitespace in finch
// status.
TEST_F(UdevCollectorTest,
       TestConnectivityWiFiDevCoredumpEnabledByFinchValueWithWhiteSpace) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);
  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);
  // White space in finch flag, gets rejected.
  SetupFirmwareDumpsFinchFlag("1 ");

  FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(0, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));
}

// Ensure fwdump is not generated even if allowed by finch because it is
// disabled by connectivity_fwdump_feature_enabled_ to not mistakenly enabled
// fwdumps in feedback report feature.
TEST_F(UdevCollectorTest,
       TestConnectivityWiFiDevCoredumpAllowedByFinchButDisabled) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);
  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);

  // Disable fwdump feature.
  collector_.EnableConnectivityFwdumpForTest(false);

  FilePath user_hash_path = paths::Get(kFbpreprocessordBaseDirectory);
  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(0, GetNumFiles(user_hash_path, kWiFiCoredumpFilePattern));
}

// Ensures that there is no fwdump file generated if policy is not set
// but fwdump for user is allowed.
TEST_F(UdevCollectorTest, TestConnectivityWiFiDevCoredumpPolicyNotSet) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);

  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);

  EXPECT_CALL(*mock, RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceGoogleUser;
            *sanitized = "user_hash";
            return true;
          })));

  EXPECT_CALL(
      *mock,
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceGoogleUser),
                       _, _, _))
      .WillRepeatedly(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "");
        return true;
      })));

  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(
      0, GetNumFiles(temp_dir_generator_.GetPath(), kWiFiCoredumpFilePattern));
}

// Ensure no fwdump is generated if user is not in allowed for fwdumps.
TEST_F(UdevCollectorTest, TestConnectivityWiFiDevCoredumpUserNotAllowed) {
  GenerateDevCoredump("devcd0", kConnectivityWiFiDriverName);

  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  collector_.SetSessionManagerProxy(mock);

  EXPECT_CALL(*mock, RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceGmailUser;
            *sanitized = "user_hash";
            return true;
          })));

  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(
      0, GetNumFiles(temp_dir_generator_.GetPath(), kWiFiCoredumpFilePattern));
}

TEST_F(UdevCollectorTest, TestCollectedDevCoredump) {
  // One more test, this time for the case of a devcoredump that should be
  // collected in all builds:
  const int driver_count =
      sizeof(kCollectedDriverNames) / sizeof(kCollectedDriverNames[0]);
  for (int i = 0; i < driver_count; i++) {
    const char* driver_name = kCollectedDriverNames[i];
    const std::string kernel_number = std::to_string(i + 2);
    const std::string device_name = std::string("devcd") + kernel_number;
    GenerateDevCoredump(device_name, driver_name);

    UdevCollectorMock third_collector;
    SetUpCollector(&third_collector);
    const std::string udev_event =
        std::string("ACTION=add:SUBSYSTEM=devcoredump:KERNEL_NUMBER=") +
        kernel_number;
    third_collector.HandleCrash(udev_event);
  }

  EXPECT_EQ(driver_count, GetNumFiles(temp_dir_generator_.GetPath(),
                                      kDevCoredumpFilePattern));

  for (int i = 0; i < driver_count; i++) {
    const char* driver_name = kCollectedDriverNames[i];
    // Check for the expected crash signature:
    std::string sanitized_name = driver_name;
    for (size_t i = 0; i < sanitized_name.size(); ++i) {
      if (!isalnum(sanitized_name[i]) && sanitized_name[i] != '_')
        sanitized_name[i] = '_';
    }
    base::FilePath meta_path;
    std::string meta_pattern = "devcoredump_";
    meta_pattern += sanitized_name;
    meta_pattern += ".*.meta";
    EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
        temp_dir_generator_.GetPath(), meta_pattern, &meta_path));
    std::string meta_contents;
    EXPECT_TRUE(base::ReadFileToString(meta_path, &meta_contents));
    std::string expected_sig =
        "sig=crash_reporter-udev-collection-devcoredump-";
    expected_sig += driver_name;
    EXPECT_THAT(meta_contents, testing::HasSubstr(expected_sig));
  }
}

TEST_F(UdevCollectorTest, RunAsRoot_TestValidBluetoothDevCoredump) {
  std::string device_name = "devcd0";
  GenerateDevCoredump(device_name, kConnectivityBTDriverName);

  FilePath data_path =
      FilePath(base::StringPrintf("%s/%s/data",
                                  temp_dir_generator_.GetPath()
                                      .Append(kDevCoredumpDirectory)
                                      .value()
                                      .c_str(),
                                  device_name.c_str()));

  std::vector<std::string> data = {
      "Bluetooth devcoredump",
      "State: 2",
      "Driver: TestDrv",
      "Vendor: TestVen",
      "Controller Name: TestCon",
      "--- Start dump ---",
      "TestData",
  };
  std::string data_str = brillo::string_utils::Join("\n", data);
  ASSERT_EQ(base::WriteFile(data_path, data_str.c_str(), data_str.length()),
            data_str.length());

  ASSERT_TRUE(test_util::CreateFile(paths::Get(kBluetoothDumpFlagPath), "0"));

  HandleCrash("ACTION=add:KERNEL_NUMBER=0:SUBSYSTEM=devcoredump");
  EXPECT_EQ(3, GetNumFiles(temp_dir_generator_.GetPath(),
                           kBluetoothCoredumpFilePattern));
}

TEST_F(UdevCollectorTest, RunAsRoot_TestInvalidBluetoothDevCoredump) {
  std::string device_name = "devcd1";
  GenerateDevCoredump(device_name, kConnectivityBTDriverName);

  FilePath data_path =
      FilePath(base::StringPrintf("%s/%s/data",
                                  temp_dir_generator_.GetPath()
                                      .Append(kDevCoredumpDirectory)
                                      .value()
                                      .c_str(),
                                  device_name.c_str()));

  // Incomplete bluetooth devcoredump header, parsing should fail and no output
  // files should get generated.
  std::vector<std::string> data = {
      "Bluetooth devcoredump",
      "State: 2",
      "Driver: TestDrv",
      "Vendor: TestVen",
  };
  std::string data_str = brillo::string_utils::Join("\n", data);
  ASSERT_EQ(base::WriteFile(data_path, data_str.c_str(), data_str.length()),
            data_str.length());

  ASSERT_TRUE(test_util::CreateFile(paths::Get(kBluetoothDumpFlagPath), "0"));

  HandleCrash("ACTION=add:KERNEL_NUMBER=1:SUBSYSTEM=devcoredump");
  EXPECT_EQ(0, GetNumFiles(temp_dir_generator_.GetPath(),
                           kBluetoothCoredumpFilePattern));
}

class UdevCollectorCrashSeverityTest
    : public UdevCollectorTest,
      public ::testing::WithParamInterface<
          test_util::ComputeCrashSeverityTestParams> {};

TEST_P(UdevCollectorCrashSeverityTest, ComputeCrashSeverity) {
  const test_util::ComputeCrashSeverityTestParams& test_case = GetParam();
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity(test_case.exec_name);

  EXPECT_EQ(computed_severity.crash_severity, test_case.expected_severity);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}

INSTANTIATE_TEST_SUITE_P(
    UdevCollectorCrashSeverityTestSuite,
    UdevCollectorCrashSeverityTest,
    testing::ValuesIn<test_util::ComputeCrashSeverityTestParams>({
        {"udev-usb", CrashCollector::CrashSeverity::kError},
        {"devcoredump_adreno", CrashCollector::CrashSeverity::kWarning},
        {"udev-i2c-atmel_mxt_ts", CrashCollector::CrashSeverity::kWarning},
        {"udev-drm", CrashCollector::CrashSeverity::kWarning},
        {"another executable", CrashCollector::CrashSeverity::kUnspecified},
    }));

// TODO(sque, crosbug.com/32238) - test wildcard cases, multiple identical udev
// events.
