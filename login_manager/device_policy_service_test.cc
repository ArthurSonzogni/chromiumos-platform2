// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/device_policy_service.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/memory/ptr_util.h>
#include <base/run_loop.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <chromeos/switches/chrome_switches.h>
#include <crypto/scoped_nss_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libcrossystem/crossystem_fake.h>

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "bindings/install_attributes.pb.h"
#include "login_manager/blob_util.h"
#include "login_manager/fake_system_utils.h"
#include "login_manager/matchers.h"
#include "login_manager/mock_device_policy_service.h"
#include "login_manager/mock_install_attributes_reader.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_nss_util.h"
#include "login_manager/mock_policy_key.h"
#include "login_manager/mock_policy_service.h"
#include "login_manager/mock_policy_store.h"
#include "login_manager/mock_vpd_process.h"

namespace em = enterprise_management;

using google::protobuf::RepeatedPtrField;

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::DoAll;
using testing::ElementsAre;
using testing::Expectation;
using testing::Mock;
using testing::Return;
using testing::ReturnRef;
using testing::Sequence;
using testing::StrictMock;
using testing::WithArg;

namespace login_manager {

namespace {

constexpr char kTestUser[] = "user@example.com";

ACTION_P(AssignVector, str) {
  arg0->assign(str.begin(), str.end());
}

PolicyNamespace MakeExtensionPolicyNamespace() {
  return std::make_pair(POLICY_DOMAIN_EXTENSIONS,
                        "ababababcdcdcdcdefefefefghghghgh");
}

void InitPolicyFetchResponse(const std::string& policy_value_str,
                             const char* policy_type,
                             const std::string& owner,
                             const std::vector<uint8_t>& signature,
                             const std::string& request_token,
                             em::PolicyFetchResponse* policy_proto) {
  em::PolicyData policy_data;
  policy_data.set_policy_type(policy_type);
  policy_data.set_policy_value(policy_value_str);
  if (!owner.empty()) {
    policy_data.set_username(owner);
  }
  if (!request_token.empty()) {
    policy_data.set_request_token(request_token);
  }
  std::string policy_data_str;
  ASSERT_TRUE(policy_data.SerializeToString(&policy_data_str));

  policy_proto->Clear();
  policy_proto->set_policy_data(policy_data_str);
  policy_proto->set_policy_data_signature(BlobToString(signature));
}

}  // namespace

class FakePolicyServiceDelegate : public PolicyService::Delegate {
 public:
  explicit FakePolicyServiceDelegate(
      DevicePolicyService* device_policy_service) {
    device_policy_service_ = device_policy_service;
  }

  // PolicyService::Delegate implementation:
  void OnPolicyPersisted(bool success) override {
    settings_ = device_policy_service_->GetSettings();
  }

  void OnKeyPersisted(bool success) override {}

  const em::ChromeDeviceSettingsProto& GetSettings() { return settings_; }

 private:
  DevicePolicyService* device_policy_service_;
  em::ChromeDeviceSettingsProto settings_;
};

class DevicePolicyServiceTest : public ::testing::Test {
 public:
  DevicePolicyServiceTest()
      : crossystem_(std::make_unique<crossystem::fake::CrossystemFake>()) {}

  void SetUp() override {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    install_attributes_file_ =
        tmpdir_.GetPath().AppendASCII("install_attributes.pb");
    base::CreateTemporaryFileInDir(tmpdir_.GetPath(),
                                   &install_attributes_file_);
  }

  void InitPolicy(const em::ChromeDeviceSettingsProto& settings,
                  const std::string& owner,
                  const std::vector<uint8_t>& signature,
                  const std::string& request_token) {
    std::string settings_str;
    ASSERT_TRUE(settings.SerializeToString(&settings_str));
    ASSERT_NO_FATAL_FAILURE(InitPolicyFetchResponse(
        settings_str, DevicePolicyService::kDevicePolicyType, owner, signature,
        request_token, &policy_proto_));
  }

  void InitEmptyPolicy(const std::string& owner,
                       const std::vector<uint8_t>& signature,
                       const std::string& request_token) {
    em::ChromeDeviceSettingsProto settings;
    InitPolicy(settings, owner, signature, request_token);
  }

  void InitService(NssUtil* nss, bool use_mock_store) {
    metrics_ = std::make_unique<MockMetrics>();
    service_.reset(new DevicePolicyService(
        tmpdir_.GetPath(), &key_, metrics_.get(), nss, &system_utils_,
        &crossystem_, &vpd_process_, &install_attributes_reader_));
    if (use_mock_store) {
      auto store_ptr = std::make_unique<StrictMock<MockPolicyStore>>();
      store_ = store_ptr.get();
      service_->SetStoreForTesting(MakeChromePolicyNamespace(),
                                   std::move(store_ptr));
    }

    // Allow the key to be read any time.
    EXPECT_CALL(key_, public_key_der()).WillRepeatedly(ReturnRef(fake_key_));
  }

  void SetInstallAttributesMissing() {
    install_attributes_reader_.SetLocked(false);
  }

  void SetDataInInstallAttributes(const std::string& mode) {
    install_attributes_reader_.SetAttributes({{"enterprise.mode", mode}});
  }

  void SetDefaultSettings() {
    crossystem_.VbSetSystemPropertyString(
        crossystem::Crossystem::kMainFirmwareType, "normal");
    crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kBlockDevmode,
                                       0);
    crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kNvramCleared,
                                       1);

    EXPECT_CALL(key_, IsPopulated()).WillRepeatedly(Return(true));

    auto proto = std::make_unique<em::ChromeDeviceSettingsProto>();
    proto->mutable_system_settings()->set_block_devmode(false);
    SetSettings(service_.get(), std::move(proto));

    EXPECT_CALL(vpd_process_, RunInBackground(_, _))
        .WillRepeatedly(Return(true));
  }

  void SetSettings(DevicePolicyService* service,
                   std::unique_ptr<em::ChromeDeviceSettingsProto> proto) {
    service->settings_ = std::move(proto);
  }

  void SetPolicyKey(DevicePolicyService* service, PolicyKey* key) {
    service->set_policy_key_for_test(key);
  }

  void SetExpectationsAndStorePolicy(
      const PolicyNamespace& ns,
      MockPolicyStore* store,
      const em::PolicyFetchResponse& policy_proto) {
    // Make sure that no policy other than Chrome policy triggers
    // [May]UpdateSystemSettings(). This is done by making sure that
    // IsPopulated() isn't run, which is called by MayUpdateSystemSettings().
    if (ns == MakeChromePolicyNamespace()) {
      EXPECT_CALL(key_, IsPopulated()).WillRepeatedly(Return(false));
    } else {
      EXPECT_CALL(key_, IsPopulated()).Times(0);
    }
    EXPECT_CALL(key_, Verify(_, _, _)).WillRepeatedly(Return(true));

    EXPECT_CALL(*store, Persist()).WillRepeatedly(Return(true));
    EXPECT_CALL(*store, Set(_)).Times(AnyNumber());
    EXPECT_CALL(*store, Get()).WillRepeatedly(ReturnRef(policy_proto));
    service_->Store(ns, SerializeAsBlob(policy_proto),
                    PolicyService::KEY_CLOBBER,
                    MockPolicyService::CreateDoNothing());
  }

  bool UpdateSystemSettings(DevicePolicyService* service) {
    return service->UpdateSystemSettings(MockPolicyService::CreateDoNothing());
  }

  void PersistPolicy(DevicePolicyService* service) {
    service->PersistPolicy(MakeChromePolicyNamespace(),
                           MockPolicyService::CreateDoNothing());
  }

  void RecordNewPolicy(const em::PolicyFetchResponse& policy) {
    new_policy_proto_.CopyFrom(policy);
  }

  void ExpectGetPolicy(Sequence sequence,
                       const em::PolicyFetchResponse& policy) {
    EXPECT_CALL(*store_, Get())
        .InSequence(sequence)
        .WillRepeatedly(ReturnRef(policy));
  }

  void ExpectInstallNewOwnerPolicy(Sequence sequence, MockNssUtil* nss) {
    Expectation get_policy =
        EXPECT_CALL(*store_, Get()).WillRepeatedly(ReturnRef(policy_proto_));
    Expectation compare_keys =
        EXPECT_CALL(key_, Equals(_)).WillRepeatedly(Return(false));
    Expectation set_policy =
        EXPECT_CALL(*store_, Set(_))
            .InSequence(sequence)
            .After(compare_keys)
            .WillOnce(Invoke(this, &DevicePolicyServiceTest::RecordNewPolicy));
  }

  void ExpectFailedInstallNewOwnerPolicy(Sequence sequence, MockNssUtil* nss) {
    Expectation get_policy =
        EXPECT_CALL(*store_, Get()).WillRepeatedly(ReturnRef(policy_proto_));
    Expectation compare_keys =
        EXPECT_CALL(key_, Equals(_)).WillRepeatedly(Return(false));
  }

  void ExpectPersistKeyAndPolicy(bool is_populated) {
    EXPECT_CALL(key_, IsPopulated()).WillRepeatedly(Return(is_populated));
    EXPECT_CALL(key_, Persist()).WillOnce(Return(true));
    EXPECT_CALL(*store_, Persist()).WillOnce(Return(true));
  }

  void ExpectNoPersistKeyAndPolicy() {
    EXPECT_CALL(key_, Persist()).Times(0);
    EXPECT_CALL(*store_, Persist()).Times(0);
  }

  void ExpectKeyPopulated(bool key_populated) {
    EXPECT_CALL(key_, HaveCheckedDisk()).WillRepeatedly(Return(true));
    EXPECT_CALL(key_, IsPopulated()).WillRepeatedly(Return(key_populated));
  }

  bool IsResilient() { return service_->IsChromeStoreResilientForTesting(); }

  bool PolicyAllowsNewUsers(const em::ChromeDeviceSettingsProto settings) {
    InitPolicy(settings, owner_, fake_sig_, "");
    return DevicePolicyService::PolicyAllowsNewUsers(policy_proto_);
  }

  em::PolicyFetchResponse policy_proto_;
  em::PolicyFetchResponse new_policy_proto_;

  const std::string owner_ = "user@somewhere";
  const std::vector<uint8_t> fake_sig_ = StringToBlob("fake_signature");
  const std::vector<uint8_t> fake_key_ = StringToBlob("fake_key");
  const std::vector<uint8_t> new_fake_sig_ = StringToBlob("new_fake_signature");
  const base::FilePath chromad_migration_file_path_{
      DevicePolicyService::kChromadMigrationSkipOobePreservePath};

  base::ScopedTempDir tmpdir_;
  base::FilePath install_attributes_file_;

  // Use StrictMock to make sure that no unexpected policy or key mutations can
  // occur without the test failing.
  StrictMock<MockPolicyKey> key_;
  StrictMock<MockPolicyStore>* store_ = nullptr;
  std::unique_ptr<MockMetrics> metrics_;
  FakeSystemUtils system_utils_;
  crossystem::Crossystem crossystem_;
  MockVpdProcess vpd_process_;
  MockInstallAttributesReader install_attributes_reader_;

  std::unique_ptr<DevicePolicyService> service_;
};

TEST_F(DevicePolicyServiceTest, PolicyAllowsNewUsersWhitelist) {
  em::ChromeDeviceSettingsProto allowed;
  allowed.mutable_allow_new_users()->set_allow_new_users(true);
  EXPECT_TRUE(PolicyAllowsNewUsers(allowed));

  allowed.mutable_user_whitelist();
  EXPECT_TRUE(PolicyAllowsNewUsers(allowed));

  allowed.mutable_user_whitelist()->add_user_whitelist("a@b");
  EXPECT_TRUE(PolicyAllowsNewUsers(allowed));

  em::ChromeDeviceSettingsProto broken;
  broken.mutable_allow_new_users()->set_allow_new_users(false);
  EXPECT_TRUE(PolicyAllowsNewUsers(broken));

  em::ChromeDeviceSettingsProto disallowed = broken;
  disallowed.mutable_user_whitelist();
  disallowed.mutable_user_whitelist()->add_user_whitelist("a@b");
  EXPECT_FALSE(PolicyAllowsNewUsers(disallowed));

  em::ChromeDeviceSettingsProto not_disallowed;
  EXPECT_TRUE(PolicyAllowsNewUsers(not_disallowed));
  not_disallowed.mutable_user_whitelist();
  EXPECT_TRUE(PolicyAllowsNewUsers(not_disallowed));

  em::ChromeDeviceSettingsProto implicitly_disallowed = not_disallowed;
  implicitly_disallowed.mutable_user_whitelist()->add_user_whitelist("a@b");
  EXPECT_FALSE(PolicyAllowsNewUsers(implicitly_disallowed));
}

TEST_F(DevicePolicyServiceTest, PolicyAllowsNewUsersAllowlist) {
  em::ChromeDeviceSettingsProto allowed;
  allowed.mutable_allow_new_users()->set_allow_new_users(true);
  EXPECT_TRUE(PolicyAllowsNewUsers(allowed));

  allowed.mutable_user_allowlist();
  EXPECT_TRUE(PolicyAllowsNewUsers(allowed));

  allowed.mutable_user_allowlist()->add_user_allowlist("a@b");
  EXPECT_TRUE(PolicyAllowsNewUsers(allowed));

  em::ChromeDeviceSettingsProto broken;
  broken.mutable_allow_new_users()->set_allow_new_users(false);
  EXPECT_TRUE(PolicyAllowsNewUsers(broken));

  em::ChromeDeviceSettingsProto disallowed = broken;
  disallowed.mutable_user_allowlist();
  disallowed.mutable_user_allowlist()->add_user_allowlist("a@b");
  EXPECT_FALSE(PolicyAllowsNewUsers(disallowed));

  em::ChromeDeviceSettingsProto not_disallowed;
  EXPECT_TRUE(PolicyAllowsNewUsers(not_disallowed));
  not_disallowed.mutable_user_allowlist();
  EXPECT_TRUE(PolicyAllowsNewUsers(not_disallowed));

  em::ChromeDeviceSettingsProto implicitly_disallowed = not_disallowed;
  implicitly_disallowed.mutable_user_allowlist()->add_user_allowlist("a@b");
  EXPECT_FALSE(PolicyAllowsNewUsers(implicitly_disallowed));
}

TEST_F(DevicePolicyServiceTest, GivenUserIsOwner) {
  {  // Correct owner.
    em::PolicyData policy_data;
    policy_data.set_username(kTestUser);
    em::PolicyFetchResponse response;
    response.set_policy_data(policy_data.SerializeAsString());

    EXPECT_TRUE(DevicePolicyService::GivenUserIsOwner(response, kTestUser));
  }
  {  // Empty string is not an owner.
    em::PolicyData policy_data;
    em::PolicyFetchResponse response;
    response.set_policy_data(policy_data.SerializeAsString());

    EXPECT_FALSE(DevicePolicyService::GivenUserIsOwner(response, ""));
  }
  {  // Managed device has no owner.
    em::PolicyData policy_data;
    policy_data.set_username(kTestUser);
    policy_data.set_management_mode(em::PolicyData::ENTERPRISE_MANAGED);
    em::PolicyFetchResponse response;
    response.set_policy_data(policy_data.SerializeAsString());

    EXPECT_FALSE(DevicePolicyService::GivenUserIsOwner(response, kTestUser));
  }
  {  // Managed device has no owner (fallback to DM token).
    em::PolicyData policy_data;
    policy_data.set_username(kTestUser);
    policy_data.set_request_token("asdf");
    em::PolicyFetchResponse response;
    response.set_policy_data(policy_data.SerializeAsString());

    EXPECT_FALSE(DevicePolicyService::GivenUserIsOwner(response, kTestUser));
  }
}

// Ensure block devmode is set properly in NVRAM.
TEST_F(DevicePolicyServiceTest, SetBlockDevModeInNvram) {
  MockNssUtil nss;
  InitService(&nss, true);

  crossystem_.VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType, "normal");
  crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kBlockDevmode, 0);
  crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kNvramCleared, 1);

  auto proto = std::make_unique<em::ChromeDeviceSettingsProto>();
  proto->mutable_system_settings()->set_block_devmode(true);
  SetSettings(service_.get(), std::move(proto));

  EXPECT_CALL(vpd_process_, RunInBackground(_, _)).WillOnce(Return(true));

  // This file should be removed, because the device is cloud managed.
  ASSERT_TRUE(system_utils_.EnsureFile(chromad_migration_file_path_, ""));

  SetDataInInstallAttributes("enterprise");
  EXPECT_TRUE(UpdateSystemSettings(service_.get()));

  EXPECT_EQ(0, crossystem_.VbGetSystemPropertyInt(
                   crossystem::Crossystem::kNvramCleared));
  EXPECT_EQ(1, crossystem_.VbGetSystemPropertyInt(
                   crossystem::Crossystem::kBlockDevmode));
  EXPECT_FALSE(system_utils_.Exists(chromad_migration_file_path_));
}

// Ensure block devmode is unset properly in NVRAM.
TEST_F(DevicePolicyServiceTest, UnsetBlockDevModeInNvram) {
  MockNssUtil nss;
  InitService(&nss, true);

  crossystem_.VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType, "normal");
  crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kBlockDevmode, 1);
  crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kNvramCleared, 1);

  auto proto = std::make_unique<em::ChromeDeviceSettingsProto>();
  proto->mutable_system_settings()->set_block_devmode(false);
  SetSettings(service_.get(), std::move(proto));

  EXPECT_CALL(vpd_process_, RunInBackground(_, _)).WillOnce(Return(true));

  // This file should be removed, because the device is cloud managed.
  ASSERT_TRUE(system_utils_.EnsureFile(chromad_migration_file_path_, ""));

  SetDataInInstallAttributes("enterprise");
  EXPECT_TRUE(UpdateSystemSettings(service_.get()));

  EXPECT_EQ(0, crossystem_.VbGetSystemPropertyInt(
                   crossystem::Crossystem::kNvramCleared));
  EXPECT_EQ(0, crossystem_.VbGetSystemPropertyInt(
                   crossystem::Crossystem::kBlockDevmode));
  EXPECT_FALSE(system_utils_.Exists(chromad_migration_file_path_));
}

// Ensure non-enrolled and non-blockdevmode device will call VPD update
// process to clean block_devmode only.
TEST_F(DevicePolicyServiceTest, CheckNotEnrolledDevice) {
  MockNssUtil nss;
  InitService(&nss, true);

  MockPolicyKey key;
  MockPolicyStore* store = new MockPolicyStore();
  MockDevicePolicyService service(&key);
  service.SetStoreForTesting(MakeChromePolicyNamespace(),
                             std::unique_ptr<MockPolicyStore>(store));

  service.set_system_utils(&system_utils_);
  service.set_crossystem(&crossystem_);
  service.set_vpd_process(&vpd_process_);
  service.set_install_attributes_reader(&install_attributes_reader_);
  crossystem_.VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType, "normal");

  auto proto = std::make_unique<em::ChromeDeviceSettingsProto>();
  proto->mutable_system_settings()->set_block_devmode(false);
  SetSettings(&service, std::move(proto));
  SetPolicyKey(&service, &key);

  EXPECT_CALL(key, IsPopulated()).WillRepeatedly(Return(true));
  EXPECT_CALL(*store, Persist()).WillRepeatedly(Return(true));
  SetDataInInstallAttributes("consumer");

  VpdProcess::KeyValuePairs updates{
      {crossystem::Crossystem::kBlockDevmode, "0"},
      {crossystem::Crossystem::kCheckEnrollment, "0"},
  };
  EXPECT_CALL(vpd_process_, RunInBackground(updates, _)).WillOnce(Return(true));

  // This file should be removed, because the device is owned by a consumer.
  ASSERT_TRUE(system_utils_.EnsureFile(chromad_migration_file_path_, ""));

  PersistPolicy(&service);
  EXPECT_FALSE(system_utils_.Exists(chromad_migration_file_path_));
}

// Ensure enrolled device gets VPD updated. A MockDevicePolicyService object is
// used.
TEST_F(DevicePolicyServiceTest, CheckEnrolledDevice) {
  MockNssUtil nss;
  InitService(&nss, true);

  MockPolicyKey key;
  MockPolicyStore* store = new MockPolicyStore();
  MockDevicePolicyService service(&key);
  service.SetStoreForTesting(MakeChromePolicyNamespace(),
                             std::unique_ptr<MockPolicyStore>(store));

  service.set_system_utils(&system_utils_);
  service.set_crossystem(&crossystem_);
  service.set_vpd_process(&vpd_process_);
  service.set_install_attributes_reader(&install_attributes_reader_);
  crossystem_.VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType, "normal");

  auto proto = std::make_unique<em::ChromeDeviceSettingsProto>();
  proto->mutable_system_settings()->set_block_devmode(false);
  SetSettings(&service, std::move(proto));
  SetPolicyKey(&service, &key);

  EXPECT_CALL(key, IsPopulated()).WillRepeatedly(Return(true));
  EXPECT_CALL(*store, Persist()).WillRepeatedly(Return(true));
  SetDataInInstallAttributes("enterprise");

  VpdProcess::KeyValuePairs updates{
      {crossystem::Crossystem::kBlockDevmode, "0"},
      {crossystem::Crossystem::kCheckEnrollment, "1"},
  };
  EXPECT_CALL(vpd_process_, RunInBackground(updates, _)).WillOnce(Return(true));

  // This file should be removed, because the device is cloud managed.
  ASSERT_TRUE(system_utils_.EnsureFile(chromad_migration_file_path_, ""));

  PersistPolicy(&service);
  EXPECT_FALSE(system_utils_.Exists(chromad_migration_file_path_));
}

// Check enrolled device that fails at VPD update.
TEST_F(DevicePolicyServiceTest, CheckFailUpdateVPD) {
  MockNssUtil nss;
  InitService(&nss, true);

  MockPolicyKey key;
  MockDevicePolicyService service;

  service.set_system_utils(&system_utils_);
  service.set_crossystem(&crossystem_);
  service.set_vpd_process(&vpd_process_);
  service.set_install_attributes_reader(&install_attributes_reader_);
  crossystem_.VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType, "normal");

  auto proto = std::make_unique<em::ChromeDeviceSettingsProto>();
  proto->mutable_system_settings()->set_block_devmode(false);
  SetSettings(&service, std::move(proto));
  SetPolicyKey(&service, &key);

  EXPECT_CALL(key, IsPopulated()).WillRepeatedly(Return(true));
  SetDataInInstallAttributes("enterprise");
  VpdProcess::KeyValuePairs updates{
      {crossystem::Crossystem::kBlockDevmode, "0"},
      {crossystem::Crossystem::kCheckEnrollment, "1"},
  };
  EXPECT_CALL(vpd_process_, RunInBackground(updates, _))
      .WillOnce(Return(false));

  // This file should be removed, because the device is cloud managed.
  ASSERT_TRUE(system_utils_.EnsureFile(chromad_migration_file_path_, ""));

  EXPECT_FALSE(UpdateSystemSettings(&service));
  EXPECT_FALSE(system_utils_.Exists(chromad_migration_file_path_));
}

// Check the behavior when install attributes file is missing.
TEST_F(DevicePolicyServiceTest, CheckMissingInstallAttributes) {
  MockNssUtil nss;
  InitService(&nss, true);

  crossystem_.VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType, "normal");
  crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kBlockDevmode, 0);
  crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kNvramCleared, 1);

  auto proto = std::make_unique<em::ChromeDeviceSettingsProto>();
  proto->mutable_system_settings()->set_block_devmode(true);
  SetSettings(service_.get(), std::move(proto));

  SetInstallAttributesMissing();

  EXPECT_CALL(vpd_process_, RunInBackground(_, _)).Times(0);

  // No file should be removed, because the management mode is unknown.
  ASSERT_TRUE(system_utils_.EnsureFile(chromad_migration_file_path_, ""));

  EXPECT_TRUE(UpdateSystemSettings(service_.get()));

  EXPECT_TRUE(system_utils_.Exists(chromad_migration_file_path_));
}

// Check the behavior when devmode is blocked for consumer owned device.
TEST_F(DevicePolicyServiceTest, CheckWeirdInstallAttributes) {
  MockNssUtil nss;
  InitService(&nss, true);

  crossystem_.VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType, "normal");
  crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kBlockDevmode, 0);
  crossystem_.VbSetSystemPropertyInt(crossystem::Crossystem::kNvramCleared, 1);

  auto proto = std::make_unique<em::ChromeDeviceSettingsProto>();
  proto->mutable_system_settings()->set_block_devmode(true);
  SetSettings(service_.get(), std::move(proto));

  SetDataInInstallAttributes(std::string());

  EXPECT_CALL(vpd_process_, RunInBackground(_, _)).Times(0);

  // This file should be removed, because the device is owned by a consumer.
  ASSERT_TRUE(system_utils_.EnsureFile(chromad_migration_file_path_, ""));

  EXPECT_TRUE(UpdateSystemSettings(service_.get()));

  EXPECT_FALSE(system_utils_.Exists(chromad_migration_file_path_));
}

TEST_F(DevicePolicyServiceTest, RecoverOwnerKeyFromPolicy) {
  MockNssUtil nss;
  InitService(&nss, true);

  EXPECT_CALL(nss, CheckPublicKeyBlob(fake_key_)).WillRepeatedly(Return(true));
  EXPECT_CALL(key_, PopulateFromDiskIfPossible()).WillRepeatedly(Return(false));
  EXPECT_CALL(key_, PopulateFromBuffer(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(key_, ClobberCompromisedKey(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(key_, IsPopulated()).WillRepeatedly(Return(true));
  EXPECT_CALL(key_, Persist()).WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, EnsureLoadedOrCreated()).WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, Get()).WillRepeatedly(ReturnRef(policy_proto_));

  em::ChromeDeviceSettingsProto settings;
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, ""));
  EXPECT_FALSE(service_->Initialize());

  policy_proto_.set_new_public_key(BlobToString(fake_key_));
  EXPECT_TRUE(service_->Initialize());
}

TEST_F(DevicePolicyServiceTest, GetSettings) {
  MockNssUtil nss;
  InitService(&nss, true);

  // No policy blob should result in an empty settings protobuf.
  em::ChromeDeviceSettingsProto settings;
  EXPECT_CALL(*store_, Get()).WillRepeatedly(ReturnRef(policy_proto_));
  EXPECT_EQ(service_->GetSettings().SerializeAsString(),
            settings.SerializeAsString());
  Mock::VerifyAndClearExpectations(store_);

  // Storing new policy should cause the settings to update as well.
  settings.mutable_metrics_enabled()->set_metrics_enabled(true);
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "t"));
  SetExpectationsAndStorePolicy(MakeChromePolicyNamespace(), store_,
                                policy_proto_);
  EXPECT_EQ(service_->GetSettings().SerializeAsString(),
            settings.SerializeAsString());
}

TEST_F(DevicePolicyServiceTest, CheckSettingsOnPolicyStore) {
  MockNssUtil nss;
  InitService(&nss, true);

  FakePolicyServiceDelegate delegate(service_.get());
  service_->set_delegate(&delegate);

  // Store some default settings first.
  em::ChromeDeviceSettingsProto settings;
  EXPECT_CALL(*store_, Get()).WillRepeatedly(ReturnRef(policy_proto_));
  EXPECT_EQ(service_->GetSettings().SerializeAsString(),
            settings.SerializeAsString());
  Mock::VerifyAndClearExpectations(store_);

  // Storing new policy should cause the settings to update as well.
  // At the time the delegate is notified, the new settings should be in.
  settings.mutable_metrics_enabled()->set_metrics_enabled(true);
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "t"));
  SetExpectationsAndStorePolicy(MakeChromePolicyNamespace(), store_,
                                policy_proto_);
  EXPECT_EQ(delegate.GetSettings().SerializeAsString(),
            settings.SerializeAsString());
}

TEST_F(DevicePolicyServiceTest, FeatureFlags) {
  MockNssUtil nss;
  InitService(&nss, true);

  em::ChromeDeviceSettingsProto settings;

  settings.mutable_feature_flags()->add_feature_flags("first");
  settings.mutable_feature_flags()->add_feature_flags("second");

  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, ""));
  SetExpectationsAndStorePolicy(MakeChromePolicyNamespace(), store_,
                                policy_proto_);

  EXPECT_THAT(service_->GetFeatureFlags(), ElementsAre("first", "second"));
}

// TODO(crbug/1104193): Remove this test when switch to feature flag mapping
// compatibility code is no longer needed.
TEST_F(DevicePolicyServiceTest, FeatureFlagsCompatiblity) {
  MockNssUtil nss;
  InitService(&nss, true);

  em::ChromeDeviceSettingsProto settings;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  settings.mutable_feature_flags()->add_switches("invalid");
  settings.mutable_feature_flags()->add_switches(
      "--enable-features=DarkLightMode");
  settings.mutable_feature_flags()->add_switches("--unknown-switch");
#pragma GCC diagnostic pop

  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, ""));
  SetExpectationsAndStorePolicy(MakeChromePolicyNamespace(), store_,
                                policy_proto_);

  EXPECT_THAT(service_->GetFeatureFlags(), ElementsAre("dark-light-mode@1"));
}

TEST_F(DevicePolicyServiceTest, ExtraCommandLineArguments) {
  MockNssUtil nss;
  InitService(&nss, true);

  // DeviceHardwareVideoDecodingEnabled true -> no command line args
  {
    em::ChromeDeviceSettingsProto settings;
    settings.mutable_devicehardwarevideodecodingenabled()->set_value(true);

    ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, ""));
    SetExpectationsAndStorePolicy(MakeChromePolicyNamespace(), store_,
                                  policy_proto_);

    EXPECT_THAT(service_->GetExtraCommandLineArguments(), ElementsAre());
  }

  // DeviceHardwareVideoDecodingEnabled unset -> no command line args
  {
    em::ChromeDeviceSettingsProto settings;

    ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, ""));
    SetExpectationsAndStorePolicy(MakeChromePolicyNamespace(), store_,
                                  policy_proto_);

    EXPECT_THAT(service_->GetExtraCommandLineArguments(), ElementsAre());
  }

  // DeviceHardwareVideoDecodingEnabled false -> disable gpu command line arg
  {
    em::ChromeDeviceSettingsProto settings;
    settings.mutable_devicehardwarevideodecodingenabled()->set_value(false);

    ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, ""));
    SetExpectationsAndStorePolicy(MakeChromePolicyNamespace(), store_,
                                  policy_proto_);

    EXPECT_THAT(service_->GetExtraCommandLineArguments(),
                ElementsAre("--disable-accelerated-video-decode"));
  }
}

TEST_F(DevicePolicyServiceTest, PersistPolicyMultipleNamespaces) {
  MockNssUtil nss;
  InitService(&nss, true);

  // Set up store for extension policy.
  auto extension_store_ptr = std::make_unique<StrictMock<MockPolicyStore>>();
  StrictMock<MockPolicyStore>* extension_store = extension_store_ptr.get();
  service_->SetStoreForTesting(MakeExtensionPolicyNamespace(),
                               std::move(extension_store_ptr));

  // Set up device policy.
  em::ChromeDeviceSettingsProto settings;
  settings.mutable_metrics_enabled()->set_metrics_enabled(true);
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "t"));

  // Set up extension policy.
  em::PolicyFetchResponse extension_policy_proto;
  ASSERT_NO_FATAL_FAILURE(InitPolicyFetchResponse(
      "fake_extension_policy", DevicePolicyService::kExtensionPolicyType,
      owner_, fake_sig_, "t", &extension_policy_proto));

  // Store and retrieve device policy.
  SetExpectationsAndStorePolicy(MakeChromePolicyNamespace(), store_,
                                policy_proto_);
  EXPECT_EQ(service_->GetSettings().SerializeAsString(),
            settings.SerializeAsString());
  Mock::VerifyAndClearExpectations(&key_);
  Mock::VerifyAndClearExpectations(store_);

  // Store and retrieve extension policy.
  SetExpectationsAndStorePolicy(MakeExtensionPolicyNamespace(), extension_store,
                                extension_policy_proto);
  std::vector<uint8_t> extension_policy_blob;
  EXPECT_TRUE(service_->Retrieve(MakeExtensionPolicyNamespace(),
                                 &extension_policy_blob));
  EXPECT_EQ(BlobToString(extension_policy_blob),
            extension_policy_proto.SerializeAsString());

  // Storing extension policy should not wipe or modify the cached device
  // settings.
  EXPECT_NE(nullptr, service_->settings_);
  EXPECT_EQ(service_->settings_->SerializeAsString(),
            settings.SerializeAsString());
}

TEST_F(DevicePolicyServiceTest, TestClearBlockDevmode) {
  MockNssUtil nss;
  InitService(&nss, true);
  const std::vector<std::pair<std::string, std::string>> kExpectedUpdate = {
      {crossystem::Crossystem::kBlockDevmode, "0"}};

  EXPECT_TRUE(crossystem_.VbSetSystemPropertyInt(
      crossystem::Crossystem::kBlockDevmode, 1));
  EXPECT_CALL(vpd_process_, RunInBackground(kExpectedUpdate, _))
      .Times(1)
      .WillOnce(Return(true));
  service_->ClearBlockDevmode(MockPolicyService::CreateDoNothing());
  Mock::VerifyAndClearExpectations(&vpd_process_);

  EXPECT_FALSE(crossystem_.VbGetSystemPropertyInt(
      crossystem::Crossystem::kNvramCleared));
  EXPECT_EQ(0, crossystem_.VbGetSystemPropertyInt(
                   crossystem::Crossystem::kBlockDevmode));

  EXPECT_TRUE(crossystem_.VbSetSystemPropertyInt(
      crossystem::Crossystem::kBlockDevmode, 1));
  EXPECT_CALL(vpd_process_, RunInBackground(kExpectedUpdate, _))
      .Times(1)
      .WillOnce(Return(false));
  service_->ClearBlockDevmode(MockPolicyService::CreateExpectFailureCallback());

  EXPECT_FALSE(crossystem_.VbGetSystemPropertyInt(
      crossystem::Crossystem::kNvramCleared));
  EXPECT_EQ(0, crossystem_.VbGetSystemPropertyInt(
                   crossystem::Crossystem::kBlockDevmode));
}

TEST_F(DevicePolicyServiceTest, TestResilientStore) {
  MockNssUtil nss;
  InitService(&nss, false);
  EXPECT_TRUE(IsResilient());
}

namespace {

em::RemoteCommand CreatePowerwashCommand() {
  em::RemoteCommand command;
  command.set_type(em::RemoteCommand_Type_DEVICE_REMOTE_POWERWASH);
  command.set_command_id(123);
  command.set_age_of_command(45678);
  command.set_target_device_id("");
  return command;
}

em::PolicyData CreatePolicydata(const em::RemoteCommand& command) {
  em::PolicyData policy_data;
  EXPECT_TRUE(command.SerializeToString(policy_data.mutable_policy_value()));
  policy_data.set_policy_type("google/chromeos/remotecommand");
  return policy_data;
}

em::SignedData CreateSignedCommand(const em::PolicyData& policy_data) {
  em::SignedData data;
  EXPECT_TRUE(policy_data.SerializeToString(data.mutable_data()));
  data.set_signature("signature");
  return data;
}

}  // namespace

TEST_F(DevicePolicyServiceTest, ValidateRemoteDeviceWipeCommand_Success) {
  MockNssUtil nss;
  InitService(&nss, false);

  EXPECT_CALL(key_, Verify(_, _, crypto::SignatureVerifier::RSA_PKCS1_SHA1))
      .WillOnce(Return(true));
  em::RemoteCommand command = CreatePowerwashCommand();
  em::PolicyData policy_data = CreatePolicydata(command);
  em::SignedData data = CreateSignedCommand(policy_data);

  EXPECT_TRUE(service_->ValidateRemoteDeviceWipeCommand(
      SerializeAsBlob(data), em::PolicyFetchRequest::SHA1_RSA));
}

TEST_F(DevicePolicyServiceTest, ValidateRemoteDeviceWipeCommand_BadSignedData) {
  MockNssUtil nss;
  InitService(&nss, false);

  em::RemoteCommand command = CreatePowerwashCommand();

  // Passing over RemoteCommand proto instead of SignedData should fail.
  EXPECT_FALSE(service_->ValidateRemoteDeviceWipeCommand(
      SerializeAsBlob(command), em::PolicyFetchRequest::SHA1_RSA));
}

TEST_F(DevicePolicyServiceTest, ValidateRemoteDeviceWipeCommand_BadSignature) {
  MockNssUtil nss;
  InitService(&nss, false);

  // Set the signature verification call to fail.
  EXPECT_CALL(key_, Verify(_, _, crypto::SignatureVerifier::RSA_PKCS1_SHA1))
      .WillOnce(Return(false));
  em::RemoteCommand command = CreatePowerwashCommand();
  em::PolicyData policy_data = CreatePolicydata(command);
  em::SignedData data = CreateSignedCommand(policy_data);

  EXPECT_FALSE(service_->ValidateRemoteDeviceWipeCommand(
      SerializeAsBlob(data), em::PolicyFetchRequest::SHA1_RSA));
}

TEST_F(DevicePolicyServiceTest,
       ValidateRemoteDeviceWipeCommand_BadSignatureType) {
  MockNssUtil nss;
  InitService(&nss, false);

  em::RemoteCommand command = CreatePowerwashCommand();
  em::PolicyData policy_data = CreatePolicydata(command);
  em::SignedData data = CreateSignedCommand(policy_data);

  EXPECT_FALSE(service_->ValidateRemoteDeviceWipeCommand(
      SerializeAsBlob(data), em::PolicyFetchRequest::NONE));
}

TEST_F(DevicePolicyServiceTest, ValidateRemoteDeviceWipeCommand_BadPolicyData) {
  MockNssUtil nss;
  InitService(&nss, false);

  EXPECT_CALL(key_, Verify(_, _, crypto::SignatureVerifier::RSA_PKCS1_SHA1))
      .WillOnce(Return(true));
  em::RemoteCommand command = CreatePowerwashCommand();
  em::PolicyData policy_data = CreatePolicydata(command);
  em::SignedData data = CreateSignedCommand(policy_data);
  // Corrupt PolicyData proto data by removing one byte.
  data.mutable_data()->resize(data.data().size() - 1);

  EXPECT_FALSE(service_->ValidateRemoteDeviceWipeCommand(
      SerializeAsBlob(data), em::PolicyFetchRequest::SHA1_RSA));
}

TEST_F(DevicePolicyServiceTest,
       ValidateRemoteDeviceWipeCommand_BadPolicyDataType) {
  MockNssUtil nss;
  InitService(&nss, false);

  EXPECT_CALL(key_, Verify(_, _, crypto::SignatureVerifier::RSA_PKCS1_SHA1))
      .WillOnce(Return(true));
  em::RemoteCommand command = CreatePowerwashCommand();
  em::PolicyData policy_data = CreatePolicydata(command);
  // Corrupt the policy type.
  policy_data.set_policy_type("acme-type");
  em::SignedData data = CreateSignedCommand(policy_data);

  EXPECT_FALSE(service_->ValidateRemoteDeviceWipeCommand(
      SerializeAsBlob(data), em::PolicyFetchRequest::SHA1_RSA));
}

TEST_F(DevicePolicyServiceTest,
       ValidateRemoteDeviceWipeCommand_BadRemoteCommand) {
  MockNssUtil nss;
  InitService(&nss, false);

  EXPECT_CALL(key_, Verify(_, _, crypto::SignatureVerifier::RSA_PKCS1_SHA1))
      .WillOnce(Return(true));
  em::RemoteCommand command = CreatePowerwashCommand();
  em::PolicyData policy_data = CreatePolicydata(command);
  // Corrupt RemoteCommand proto data by removing one byte.
  policy_data.mutable_policy_value()->resize(policy_data.policy_value().size() -
                                             1);
  em::SignedData data = CreateSignedCommand(policy_data);

  EXPECT_FALSE(service_->ValidateRemoteDeviceWipeCommand(
      SerializeAsBlob(data), em::PolicyFetchRequest::SHA1_RSA));
}

TEST_F(DevicePolicyServiceTest,
       ValidateRemoteDeviceWipeCommand_BadCommandType) {
  MockNssUtil nss;
  InitService(&nss, false);

  EXPECT_CALL(key_, Verify(_, _, crypto::SignatureVerifier::RSA_PKCS1_SHA1))
      .WillOnce(Return(true));
  em::RemoteCommand command = CreatePowerwashCommand();
  // Set the command type here to reboot, that should fail.
  command.set_type(em::RemoteCommand_Type_DEVICE_REBOOT);
  em::PolicyData policy_data = CreatePolicydata(command);
  em::SignedData data = CreateSignedCommand(policy_data);

  EXPECT_FALSE(service_->ValidateRemoteDeviceWipeCommand(
      SerializeAsBlob(data), em::PolicyFetchRequest::SHA1_RSA));
}

TEST_F(DevicePolicyServiceTest, ValidateRemoteDeviceWipeCommand_BadDeviceId) {
  MockNssUtil nss;
  InitService(&nss, true);

  EXPECT_CALL(key_, Verify(_, _, crypto::SignatureVerifier::RSA_PKCS1_SHA1))
      .WillOnce(Return(true));
  em::RemoteCommand command = CreatePowerwashCommand();
  // Set bogus target device id.
  command.set_target_device_id("acme-device");
  em::PolicyData policy_data = CreatePolicydata(command);
  em::SignedData data = CreateSignedCommand(policy_data);

  // Set expected target device id.
  em::PolicyData policy_data_id;
  policy_data_id.set_device_id("coyote-device");
  em::PolicyFetchResponse policy_proto;
  policy_proto.set_policy_data(policy_data_id.SerializeAsString());
  EXPECT_CALL(*store_, Get()).WillOnce(ReturnRef(policy_proto));

  EXPECT_FALSE(service_->ValidateRemoteDeviceWipeCommand(
      SerializeAsBlob(data), em::PolicyFetchRequest::SHA1_RSA));
}

TEST_F(DevicePolicyServiceTest, MayUpdateSystemSettings) {
  MockNssUtil nss;
  InitService(&nss, true);
  EXPECT_CALL(key_, IsPopulated()).WillRepeatedly(Return(true));

  // We shouldn't update system settings if kMainFirmwareType isn't set.
  EXPECT_FALSE(service_->MayUpdateSystemSettings());

  crossystem_.VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType,
      crossystem::Crossystem::kMainfwTypeNonchrome);
  // We shouldn't update system settings if the device is non chrome.
  EXPECT_FALSE(service_->MayUpdateSystemSettings());

  // Any FW type that's not "nonchrome" is a valid FW to update.
  crossystem_.VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType, "normal");
  // We should update a "normal" ChromeOS FW.
  EXPECT_TRUE(service_->MayUpdateSystemSettings());
}

}  // namespace login_manager
