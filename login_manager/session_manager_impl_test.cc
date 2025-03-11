// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_impl.h"

#include <fcntl.h>
#include <keyutils.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/notreached.h>
#include <base/posix/unix_domain_socket.h>
#include <base/run_loop.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_executor.h>
#include <base/test/bind.h>
#include <base/test/simple_test_tick_clock.h>
#include <base/test/test_future.h>
#include <brillo/cryptohome.h>
#include <brillo/errors/error.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <crypto/scoped_nss_types.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libcrossystem/crossystem_fake.h>
#include <libpasswordprovider/password.h>
#include <libpasswordprovider/password_provider.h>

#include "arc/arc.pb.h"
#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "dbus/login_manager/dbus-constants.h"
#include "libpasswordprovider/fake_password_provider.h"
#include "login_manager/arc_manager.h"
#include "login_manager/blob_util.h"
#include "login_manager/dbus_test_util.h"
#include "login_manager/dbus_util.h"
#include "login_manager/device_local_account_manager.h"
#include "login_manager/fake_container_manager.h"
#include "login_manager/fake_secret_util.h"
#include "login_manager/fake_system_utils.h"
#include "login_manager/file_checker.h"
#include "login_manager/matchers.h"
#include "login_manager/mock_arc_sideload_status.h"
#include "login_manager/mock_device_identifier_generator.h"
#include "login_manager/mock_device_policy_service.h"
#include "login_manager/mock_file_checker.h"
#include "login_manager/mock_init_daemon_controller.h"
#include "login_manager/mock_install_attributes_reader.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_nss_util.h"
#include "login_manager/mock_policy_key.h"
#include "login_manager/mock_policy_service.h"
#include "login_manager/mock_process_manager_service.h"
#include "login_manager/mock_system_utils.h"
#include "login_manager/mock_user_policy_service_factory.h"
#include "login_manager/mock_vpd_process.h"
#include "login_manager/proto_bindings/login_screen_storage.pb.h"
#include "login_manager/proto_bindings/policy_descriptor.pb.h"
#include "login_manager/secret_util.h"
#include "login_manager/system_utils_impl.h"

using ::base::test::TestFuture;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::Mock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::ReturnNull;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::WithArg;

using brillo::cryptohome::home::GetGuestUsername;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SetSystemSalt;
using brillo::cryptohome::home::Username;

ACTION_TEMPLATE(MovePointee,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(pointer)) {
  *pointer = std::move(*(::std::get<k>(args)));
}

using std::map;
using std::string;
using std::vector;

namespace em = enterprise_management;

namespace login_manager {

namespace {

const char* const kUserlessArgv[] = {
    "program",
    "--switch1",
    "--switch2=switch2_value",
    "--switch3=escaped_\"_quote",
    "--switch4=white space",
    "arg1",
    "arg 2",
};

const char* const kGuestArgv[] = {
    "program",
    "--bwsi",
    "--switch1=switch1_value",
    "--switch2=escaped_\"_quote",
    "--switch3=white space",
    "arg1",
    "arg 2",
};

// Test Bus instance to inject MockExportedObject.
class FakeBus : public dbus::Bus {
 public:
  FakeBus()
      : dbus::Bus(GetBusOptions()),
        exported_object_(new dbus::MockExportedObject(
            nullptr, dbus::ObjectPath("/fake/path"))) {}

  dbus::MockExportedObject* exported_object() { return exported_object_.get(); }

  // dbus::Bus overrides.
  dbus::ExportedObject* GetExportedObject(
      const dbus::ObjectPath& object_path) override {
    return exported_object_.get();
  }

  bool RequestOwnershipAndBlock(const std::string& service_name,
                                ServiceOwnershipOptions options) override {
    return true;  // Fake to success.
  }

 protected:
  // dbus::Bus is refcounted object.
  ~FakeBus() override = default;

 private:
  scoped_refptr<dbus::MockExportedObject> exported_object_;

  static dbus::Bus::Options GetBusOptions() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    return options;
  }
};

// Storing T value. Iff T is const char*, instead std::string value.
template <typename T>
struct PayloadStorage {
  // gtest/gmock 1.8.1 and later add an extra const that needs to be stripped.
  typename std::remove_const<T>::type value;
};

// For gtest/gmock < 1.8.1
template <>
struct PayloadStorage<const char*> {
  std::string value;
};

// For gtest/gmock >= 1.8.1
template <>
struct PayloadStorage<const char* const> {
  std::string value;
};

// Matcher for SessionManagerInterface's signal.
MATCHER_P(SignalEq, method_name, "") {
  return arg->GetMember() == method_name;
}

MATCHER_P2(SignalEq, method_name, payload1, "") {
  PayloadStorage<decltype(payload1)> actual1;
  dbus::MessageReader reader(arg);
  return (arg->GetMember() == method_name &&
          brillo::dbus_utils::ReadDBusArgs(&reader, &actual1.value) &&
          payload1 == actual1.value);
}

MATCHER_P3(SignalEq, method_name, payload1, payload2, "") {
  PayloadStorage<decltype(payload1)> actual1;
  PayloadStorage<decltype(payload2)> actual2;
  dbus::MessageReader reader(arg);
  return (arg->GetMember() == method_name &&
          brillo::dbus_utils::ReadDBusArgs(&reader, &actual1.value,
                                           &actual2.value) &&
          payload1 == actual1.value && payload2 == actual2.value);
}

// Checks whether a PolicyNamespace is not a POLICY_DOMAIN_CHROME namespace and
// has a component id.
MATCHER(IsComponentNamespace, "") {
  return arg.first != POLICY_DOMAIN_CHROME && !arg.second.empty();
}

// Checks whether the PK11SlotDescriptor object includes a given PK11SlotInfo
// object.
MATCHER_P(IncludesSlot, slot, "") {
  return arg->slot.get() == slot;
}

constexpr char kSaneEmail[] = "user@somewhere.com";

constexpr char kEmptyAccountId[] = "";

std::vector<uint8_t> MakePolicyDescriptor(PolicyAccountType account_type,
                                          const std::string& account_id) {
  PolicyDescriptor descriptor;
  descriptor.set_account_type(account_type);
  descriptor.set_account_id(account_id);
  descriptor.set_domain(POLICY_DOMAIN_CHROME);
  return StringToBlob(descriptor.SerializeAsString());
}

std::vector<uint8_t> MakeLoginScreenStorageMetadata(
    bool clear_on_session_exit) {
  LoginScreenStorageMetadata metadata;
  metadata.set_clear_on_session_exit(clear_on_session_exit);
  return StringToBlob(metadata.SerializeAsString());
}

}  // namespace

class SessionManagerImplTest : public ::testing::Test,
                               public SessionManagerImpl::Delegate {
 public:
  SessionManagerImplTest()
      : bus_(new FakeBus()),
        device_identifier_generator_(&system_utils_, &metrics_),
        crossystem_(std::make_unique<crossystem::fake::CrossystemFake>()),
        powerd_proxy_(new dbus::MockObjectProxy(
            nullptr, "", dbus::ObjectPath("/fake/powerd"))),
        system_clock_proxy_(new dbus::MockObjectProxy(
            nullptr, "", dbus::ObjectPath("/fake/clock"))),
        fwmp_proxy_(new dbus::MockObjectProxy(
            nullptr, "", dbus::ObjectPath("/fake/fwmp"))) {}
  SessionManagerImplTest(const SessionManagerImplTest&) = delete;
  SessionManagerImplTest& operator=(const SessionManagerImplTest&) = delete;

  ~SessionManagerImplTest() override = default;

  void SetUp() override {
    SetSystemSalt(&fake_salt_);

    ASSERT_TRUE(log_dir_.CreateUniqueTempDir());
    log_symlink_ = log_dir_.GetPath().Append("ui.LATEST");

    init_controller_ = new MockInitDaemonController();
    impl_ = std::make_unique<SessionManagerImpl>(
        this /* delegate */, base::WrapUnique(init_controller_), bus_.get(),
        &device_identifier_generator_, &manager_, &metrics_, &nss_,
        std::nullopt, &system_utils_, &crossystem_, &vpd_process_, &owner_key_,
        /*arc_manager=*/nullptr, &install_attributes_reader_,
        powerd_proxy_.get(), system_clock_proxy_.get(), fwmp_proxy_.get());

    impl_->SetSystemClockLastSyncInfoRetryDelayForTesting(base::TimeDelta());
    impl_->SetUiLogSymlinkPathForTesting(log_symlink_);

    device_policy_store_ = new MockPolicyStore();
    ON_CALL(*device_policy_store_, Get())
        .WillByDefault(ReturnRef(device_policy_));

    device_policy_service_ = new MockDevicePolicyService(&owner_key_);
    device_policy_service_->SetStoreForTesting(
        MakeChromePolicyNamespace(),
        std::unique_ptr<MockPolicyStore>(device_policy_store_));

    user_policy_service_factory_ =
        new testing::NiceMock<MockUserPolicyServiceFactory>();
    ON_CALL(*user_policy_service_factory_, Create(_))
        .WillByDefault(
            Invoke(this, &SessionManagerImplTest::CreateUserPolicyService));

    auto device_local_account_manager =
        std::make_unique<DeviceLocalAccountManager>(
            &system_utils_,
            base::FilePath(SessionManagerImpl::kDeviceLocalAccountsDir),
            &owner_key_);

    impl_->SetPolicyServicesForTesting(
        base::WrapUnique(device_policy_service_),
        base::WrapUnique(user_policy_service_factory_),
        std::move(device_local_account_manager));

    // Start at an arbitrary non-zero time.
    tick_clock_ = new base::SimpleTestTickClock();
    tick_clock_->SetNowTicks(base::TimeTicks() + base::Hours(1));
    impl_->SetTickClockForTesting(base::WrapUnique(tick_clock_));

    auto shared_memory_util =
        std::make_unique<secret_util::FakeSharedMemoryUtil>();
    shared_memory_util_ = shared_memory_util.get();
    impl_->SetLoginScreenStorageForTesting(std::make_unique<LoginScreenStorage>(
        &system_utils_,
        base::FilePath(SessionManagerImpl::kLoginScreenStoragePath),
        std::move(shared_memory_util)));

    EXPECT_CALL(*powerd_proxy_,
                DoConnectToSignal(power_manager::kPowerManagerInterface,
                                  power_manager::kSuspendImminentSignal, _, _))
        .WillOnce(SaveArg<2>(&suspend_imminent_callback_));
    EXPECT_CALL(*powerd_proxy_,
                DoConnectToSignal(power_manager::kPowerManagerInterface,
                                  power_manager::kSuspendDoneSignal, _, _))
        .WillOnce(SaveArg<2>(&suspend_done_callback_));

    EXPECT_CALL(*system_clock_proxy_, DoWaitForServiceToBeAvailable(_))
        .WillOnce(MovePointee<0>(&available_callback_));

    impl_->Initialize();

    ASSERT_TRUE(Mock::VerifyAndClearExpectations(powerd_proxy_.get()));
    ASSERT_FALSE(suspend_imminent_callback_.is_null());
    ASSERT_FALSE(suspend_done_callback_.is_null());

    ASSERT_TRUE(Mock::VerifyAndClearExpectations(system_clock_proxy_.get()));
    ASSERT_FALSE(available_callback_.is_null());

    EXPECT_CALL(*exported_object(), ExportMethodAndBlock(_, _, _))
        .WillRepeatedly(Return(true));
    impl_->StartDBusService();
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(exported_object()));

    password_provider_ = new password_provider::FakePasswordProvider;
    impl_->SetPasswordProviderForTesting(
        std::unique_ptr<password_provider::FakePasswordProvider>(
            password_provider_));
  }

  void TearDown() override {
    device_policy_service_ = nullptr;
    init_controller_ = nullptr;
    EXPECT_CALL(*exported_object(), Unregister()).Times(1);
    impl_.reset();
    Mock::VerifyAndClearExpectations(exported_object());

    SetSystemSalt(nullptr);
    EXPECT_EQ(actual_locks_, expected_locks_);
    EXPECT_EQ(actual_restarts_, expected_restarts_);
  }

  // SessionManagerImpl::Delegate:
  void LockScreen() override { actual_locks_++; }
  void RestartDevice(const std::string& description) override {
    actual_restarts_++;
  }

 protected:
  dbus::MockExportedObject* exported_object() {
    return bus_->exported_object();
  }

  void SetDeviceMode(const std::string& mode) {
    install_attributes_reader_.SetAttributes({{"enterprise.mode", mode}});
  }

  void ExpectStartSession(const string& account_id_string) {
    ExpectSessionBoilerplate(account_id_string);
  }

  void ExpectGuestSession() { ExpectSessionBoilerplate(*GetGuestUsername()); }

  void ExpectLockScreen() { expected_locks_ = 1; }

  // Since expected_restarts_ is 0 by default, ExpectDeviceRestart(0) initially
  // is equivalent to no-op. In the tests ExpectDeviceRestart(0) is used
  // to make the setup more explicit.
  void ExpectDeviceRestart(uint32_t count) { expected_restarts_ = count; }

  void ExpectStorePolicy(MockDevicePolicyService* service,
                         const std::vector<uint8_t>& policy_blob,
                         int flags) {
    EXPECT_CALL(*service,
                Store(MakeChromePolicyNamespace(), policy_blob, flags, _));
  }

  void ExpectNoStorePolicy(MockDevicePolicyService* service) {
    EXPECT_CALL(*service, Store(_, _, _, _)).Times(0);
  }

  void ExpectAndRunStartSession(const string& email) {
    ExpectStartSession(email);
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StartSession(&error, email, kNothing));
    EXPECT_FALSE(error.get());
    VerifyAndClearExpectations();
  }

  void ExpectAndRunGuestSession() {
    ExpectGuestSession();
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StartSession(&error, *GetGuestUsername(), kNothing));
    EXPECT_FALSE(error.get());
    VerifyAndClearExpectations();
  }

  std::unique_ptr<PolicyService> CreateUserPolicyService(
      const string& username) {
    std::unique_ptr<MockPolicyService> policy_service =
        std::make_unique<MockPolicyService>();
    user_policy_services_[username] = policy_service.get();
    return policy_service;
  }

  void SetDevicePolicy(const em::ChromeDeviceSettingsProto& settings) {
    em::PolicyData policy_data;
    CHECK(settings.SerializeToString(policy_data.mutable_policy_value()));
    CHECK(policy_data.SerializeToString(device_policy_.mutable_policy_data()));
  }

  // Stores a device policy with a device local account, which should add this
  // account to SessionManagerImpl's device local account manager.
  void SetupDeviceLocalAccount(const std::string& account_id) {
    // Setup device policy with a device local account.
    em::ChromeDeviceSettingsProto settings;
    em::DeviceLocalAccountInfoProto* account =
        settings.mutable_device_local_accounts()->add_account();
    account->set_type(
        em::DeviceLocalAccountInfoProto::ACCOUNT_TYPE_PUBLIC_SESSION);
    account->set_account_id(account_id);

    // Make sure that SessionManagerImpl calls DeviceLocalAccountManager with
    // the given |settings| to initialize the account.
    SetDevicePolicy(settings);
    EXPECT_CALL(*device_policy_store_, Get()).Times(1);
    EXPECT_CALL(*exported_object(),
                SendSignal(SignalEq(
                    login_manager::kPropertyChangeCompleteSignal, "success")))
        .Times(1);
    device_policy_service_->OnPolicySuccessfullyPersisted();
    VerifyAndClearExpectations();
  }

  // Creates a policy blob that can be serialized with a real PolicyService.
  std::vector<uint8_t> CreatePolicyFetchResponseBlob() {
    em::PolicyFetchResponse policy;
    em::PolicyData policy_data;
    policy_data.set_policy_value("fake policy");
    CHECK(policy_data.SerializeToString(policy.mutable_policy_data()));
    return StringToBlob(policy.SerializeAsString());
  }

  base::FilePath GetDeviceLocalAccountPolicyPath(
      const std::string& account_id) {
    return base::FilePath(SessionManagerImpl::kDeviceLocalAccountsDir)
        .Append(*SanitizeUserName(Username(account_id)))
        .Append(DeviceLocalAccountManager::kPolicyDir)
        .Append(PolicyService::kChromePolicyFileName);
  }

  void VerifyAndClearExpectations() {
    Mock::VerifyAndClearExpectations(device_policy_store_);
    Mock::VerifyAndClearExpectations(device_policy_service_);
    for (auto& entry : user_policy_services_) {
      Mock::VerifyAndClearExpectations(entry.second);
    }
    Mock::VerifyAndClearExpectations(init_controller_);
    Mock::VerifyAndClearExpectations(&manager_);
    Mock::VerifyAndClearExpectations(&metrics_);
    Mock::VerifyAndClearExpectations(&nss_);
    Mock::VerifyAndClearExpectations(exported_object());
  }

  void GotLastSyncInfo(bool network_synchronized) {
    ASSERT_FALSE(available_callback_.is_null());

    dbus::ObjectProxy::ResponseCallback time_sync_callback;
    EXPECT_CALL(*system_clock_proxy_,
                DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
        .WillOnce(MovePointee<2>(&time_sync_callback));
    std::move(available_callback_).Run(true);
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(system_clock_proxy_.get()));

    std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    writer.AppendBool(network_synchronized);
    std::move(time_sync_callback).Run(response.get());
  }

  base::FilePath GetTestLoginScreenStoragePath(const std::string& key) {
    return base::FilePath(SessionManagerImpl::kLoginScreenStoragePath)
        .Append(secret_util::StringToSafeFilename(key));
  }

  // These are bare pointers, not unique_ptrs, because we need to give them
  // to a SessionManagerImpl instance, but also be able to set expectations
  // on them after we hand them off.
  // Owned by SessionManagerImpl.
  MockInitDaemonController* init_controller_ = nullptr;
  MockPolicyStore* device_policy_store_ = nullptr;
  MockDevicePolicyService* device_policy_service_ = nullptr;
  MockUserPolicyServiceFactory* user_policy_service_factory_ = nullptr;
  base::SimpleTestTickClock* tick_clock_ = nullptr;
  map<string, MockPolicyService*> user_policy_services_;
  em::PolicyFetchResponse device_policy_;

  scoped_refptr<FakeBus> bus_;
  MockDeviceIdentifierGenerator device_identifier_generator_;
  MockProcessManagerService manager_;
  MockMetrics metrics_;
  MockNssUtil nss_;
  FakeSystemUtils system_utils_;
  crossystem::Crossystem crossystem_;
  MockVpdProcess vpd_process_;
  MockPolicyKey owner_key_;
  MockInstallAttributesReader install_attributes_reader_;

  scoped_refptr<dbus::MockObjectProxy> powerd_proxy_;
  dbus::ObjectProxy::SignalCallback suspend_imminent_callback_;
  dbus::ObjectProxy::SignalCallback suspend_done_callback_;

  scoped_refptr<dbus::MockObjectProxy> system_clock_proxy_;
  dbus::ObjectProxy::WaitForServiceToBeAvailableCallback available_callback_;

  scoped_refptr<dbus::MockObjectProxy> fwmp_proxy_;

  password_provider::FakePasswordProvider* password_provider_ = nullptr;

  base::ScopedTempDir log_dir_;  // simulates /var/log/ui
  base::FilePath log_symlink_;   // simulates ui.LATEST; not created by default

  std::unique_ptr<SessionManagerImpl> impl_;
  secret_util::SharedMemoryUtil* shared_memory_util_;

  static const pid_t kFakePid;
  static const char kNothing[];
  static const char kContainerInstanceId[];
  static const int kAllKeyFlags;

 private:
  // Returns a response for the given method call. Used to implement
  // CallMethodAndBlock() for |mock_proxy_|.
  base::expected<std::unique_ptr<dbus::Response>, dbus::Error>
  CreateMockProxyResponse(dbus::MethodCall* method_call, int timeout_ms) {
    return base::ok(dbus::Response::CreateEmpty());
  }

  void ExpectSessionBoilerplate(const string& account_id_string) {
    EXPECT_CALL(manager_,
                SetBrowserSessionForUser(
                    StrEq(account_id_string),
                    StrEq(*SanitizeUserName(Username(account_id_string)))))
        .Times(1);

    EXPECT_CALL(*init_controller_,
                TriggerImpulse(SessionManagerImpl::kStartUserSessionImpulse,
                               ElementsAre(StartsWith("CHROMEOS_USER=")),
                               InitDaemonController::TriggerMode::ASYNC))
        .WillOnce(Return(ByMove(nullptr)));
    EXPECT_CALL(*exported_object(),
                SendSignal(SignalEq(login_manager::kSessionStateChangedSignal,
                                    SessionManagerImpl::kStarted)))
        .Times(1);
  }

  string fake_salt_ = "fake salt";

  base::SingleThreadTaskExecutor task_executor;

  // Used by fake closures that simulate calling chrome and powerd to lock
  // the screen and restart the device.
  uint32_t actual_locks_ = 0;
  uint32_t expected_locks_ = 0;
  uint32_t actual_restarts_ = 0;
  uint32_t expected_restarts_ = 0;
};

const pid_t SessionManagerImplTest::kFakePid = 4;
const char SessionManagerImplTest::kNothing[] = "";
const int SessionManagerImplTest::kAllKeyFlags =
    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW |
    PolicyService::KEY_CLOBBER;

TEST_F(SessionManagerImplTest, EmitLoginPromptVisible) {
  const char event_name[] = "login-prompt-visible";
  EXPECT_CALL(metrics_, RecordStats(StrEq(event_name))).Times(1);
  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kLoginPromptVisibleSignal)))
      .Times(1);
  EXPECT_CALL(*init_controller_,
              TriggerImpulse("login-prompt-visible", ElementsAre(),
                             InitDaemonController::TriggerMode::ASYNC))
      .Times(1);
  impl_->EmitLoginPromptVisible();
}

TEST_F(SessionManagerImplTest, EmitAshInitialized) {
  EXPECT_CALL(*init_controller_,
              TriggerImpulse("ash-initialized", ElementsAre(),
                             InitDaemonController::TriggerMode::ASYNC))
      .Times(1);
  impl_->EmitAshInitialized();
}

TEST_F(SessionManagerImplTest, EnableChromeTesting) {
  std::vector<std::string> args = {"--repeat-arg", "--one-time-arg"};
  const std::vector<std::string> kEnvVars = {"FOO=", "BAR=/tmp"};

  // Check that SetBrowserTestArgs() is called with a randomly chosen
  // --testing-channel path name.
  const std::string expected_testing_path_prefix =
      base::FormatTemporaryFileName("").value();

  {
    ::testing::InSequence sequence;
    EXPECT_CALL(manager_,
                SetBrowserTestArgs(ElementsAre(
                    args[0], args[1], HasSubstr(expected_testing_path_prefix))))
        .Times(1);
    EXPECT_CALL(manager_, SetBrowserAdditionalEnvironmentalVariables(
                              ElementsAre(kEnvVars[0], kEnvVars[1])))
        .Times(1);
    EXPECT_CALL(manager_, RestartBrowser()).Times(1);

    brillo::ErrorPtr error;
    std::string testing_path;
    ASSERT_TRUE(impl_->EnableChromeTesting(&error, false, args, kEnvVars,
                                           &testing_path));
    EXPECT_FALSE(error.get());
    EXPECT_NE(std::string::npos,
              testing_path.find(expected_testing_path_prefix))
        << testing_path;
    Mock::VerifyAndClearExpectations(&manager_);
  }

  {
    // Calling again, without forcing relaunch, should not do anything.
    brillo::ErrorPtr error;
    std::string testing_path;
    ASSERT_TRUE(impl_->EnableChromeTesting(&error, false, args, kEnvVars,
                                           &testing_path));
    EXPECT_FALSE(error.get());
    EXPECT_NE(std::string::npos,
              testing_path.find(expected_testing_path_prefix))
        << testing_path;
    Mock::VerifyAndClearExpectations(&manager_);
  }

  // Force relaunch.  Should go through the whole path again.
  {
    args[0] = "--some-switch";
    args[1] = "--repeat-arg";
    ::testing::InSequence sequence;
    EXPECT_CALL(manager_,
                SetBrowserTestArgs(ElementsAre(
                    args[0], args[1], HasSubstr(expected_testing_path_prefix))))
        .Times(1);
    EXPECT_CALL(manager_, SetBrowserAdditionalEnvironmentalVariables(
                              ElementsAre(kEnvVars[0], kEnvVars[1])))
        .Times(1);
    EXPECT_CALL(manager_, RestartBrowser()).Times(1);

    brillo::ErrorPtr error;
    std::string testing_path;
    ASSERT_TRUE(impl_->EnableChromeTesting(&error, true, args, kEnvVars,
                                           &testing_path));
    EXPECT_FALSE(error.get());
    EXPECT_NE(std::string::npos,
              testing_path.find(expected_testing_path_prefix))
        << testing_path;
    Mock::VerifyAndClearExpectations(&manager_);
  }
}

TEST_F(SessionManagerImplTest, StartSession) {
  ExpectStartSession(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
}

TEST_F(SessionManagerImplTest, StartSession_InvalidUser) {
  constexpr char kBadEmail[] = "user";
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->StartSession(&error, kBadEmail, kNothing));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kInvalidAccount, error->GetCode());
}

TEST_F(SessionManagerImplTest, StartSession_Twice) {
  ExpectStartSession(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
  EXPECT_FALSE(error.get());

  EXPECT_FALSE(impl_->StartSession(&error, kSaneEmail, kNothing));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSessionExists, error->GetCode());
}

TEST_F(SessionManagerImplTest, StartSession_TwoUsers) {
  ExpectStartSession(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
  EXPECT_FALSE(error.get());
  VerifyAndClearExpectations();

  constexpr char kEmail2[] = "user2@somewhere";
  ExpectStartSession(kEmail2);
  EXPECT_TRUE(impl_->StartSession(&error, kEmail2, kNothing));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, EmitStartedUserSession) {
  // Succeed for a user who is starting a session.
  ExpectAndRunStartSession(kSaneEmail);
  EXPECT_CALL(*init_controller_,
              TriggerImpulse(SessionManagerImpl::kStartedUserSessionImpulse,
                             ElementsAre(StartsWith("CHROMEOS_USER=")),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(nullptr)));
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->EmitStartedUserSession(&error, kSaneEmail));
    EXPECT_FALSE(error.get());
  }

  // Fail for a user who hasn't been starting a session yet.
  constexpr char kEmail2[] = "user2@somewhere";
  {
    brillo::ErrorPtr error;
    EXPECT_FALSE(impl_->EmitStartedUserSession(&error, kEmail2));
    EXPECT_TRUE(error.get());
  }
}

TEST_F(SessionManagerImplTest, SaveLoginPassword) {
  const string kPassword("thepassword");
  base::ScopedFD password_fd = secret_util::WriteSizeAndDataToPipe(
      std::vector<uint8_t>(kPassword.begin(), kPassword.end()));
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->SaveLoginPassword(&error, password_fd));
  EXPECT_FALSE(error.get());

  EXPECT_TRUE(password_provider_->password_saved());
}

TEST_F(SessionManagerImplTest, DiscardPasswordOnStopSession) {
  impl_->StopSessionWithReason(
      static_cast<uint32_t>(SessionStopReason::RESTORE_ACTIVE_SESSIONS));
  EXPECT_TRUE(password_provider_->password_discarded());
}

TEST_F(SessionManagerImplTest, StopSession) {
  EXPECT_CALL(manager_, ScheduleShutdown()).Times(1);
  impl_->StopSessionWithReason(
      static_cast<uint32_t>(SessionStopReason::RESTORE_ACTIVE_SESSIONS));
}

TEST_F(SessionManagerImplTest, LoadShillProfile) {
  EXPECT_CALL(*init_controller_,
              TriggerImpulse(SessionManagerImpl::kLoadShillProfileImpulse,
                             ElementsAre(StartsWith("CHROMEOS_USER=")),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(nullptr)));
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->LoadShillProfile(&error, kSaneEmail));
    EXPECT_FALSE(error.get());
  }
}

TEST_F(SessionManagerImplTest, LoginScreenStorage_StoreEphemeral) {
  const string kTestKey("testkey");
  const string kTestValue("testvalue");
  const vector<uint8_t> kTestValueVector =
      std::vector<uint8_t>(kTestValue.begin(), kTestValue.end());
  auto value_fd =
      shared_memory_util_->WriteDataToSharedMemory(kTestValueVector);

  ExpectAndRunStartSession(kSaneEmail);

  brillo::ErrorPtr error;
  impl_->LoginScreenStorageStore(
      &error, kTestKey,
      MakeLoginScreenStorageMetadata(/*clear_on_session_exit=*/true),
      kTestValue.size(), value_fd);
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(system_utils_.Exists(GetTestLoginScreenStoragePath(kTestKey)));

  base::ScopedFD out_value_fd;
  uint64_t out_value_size;
  impl_->LoginScreenStorageRetrieve(&error, kTestKey, &out_value_size,
                                    &out_value_fd);
  EXPECT_FALSE(error.get());
  std::vector<uint8_t> out_value;
  EXPECT_TRUE(shared_memory_util_->ReadDataFromSharedMemory(
      out_value_fd, out_value_size, &out_value));
  EXPECT_EQ(out_value,
            std::vector<uint8_t>(kTestValue.begin(), kTestValue.end()));
}

TEST_F(SessionManagerImplTest, LoginScreenStorage_StorePersistent) {
  const string kTestKey("testkey");
  const string kTestValue("testvalue");
  const vector<uint8_t> kTestValueVector =
      std::vector<uint8_t>(kTestValue.begin(), kTestValue.end());
  auto value_fd =
      shared_memory_util_->WriteDataToSharedMemory(kTestValueVector);

  brillo::ErrorPtr error;
  impl_->LoginScreenStorageStore(
      &error, kTestKey,
      MakeLoginScreenStorageMetadata(/*clear_on_session_exit=*/false),
      kTestValue.size(), value_fd);
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(system_utils_.Exists(GetTestLoginScreenStoragePath(kTestKey)));

  base::ScopedFD out_value_fd;
  uint64_t out_value_size;
  impl_->LoginScreenStorageRetrieve(&error, kTestKey, &out_value_size,
                                    &out_value_fd);
  EXPECT_FALSE(error.get());
  std::vector<uint8_t> out_value;
  EXPECT_TRUE(shared_memory_util_->ReadDataFromSharedMemory(
      out_value_fd, out_value_size, &out_value));
  EXPECT_EQ(out_value,
            std::vector<uint8_t>(kTestValue.begin(), kTestValue.end()));
}

TEST_F(SessionManagerImplTest,
       LoginScreenStorage_StorePersistentFailsInSession) {
  const string kTestKey("testkey");
  const string kTestValue("testvalue");
  const vector<uint8_t> kTestValueVector =
      std::vector<uint8_t>(kTestValue.begin(), kTestValue.end());
  auto value_fd =
      shared_memory_util_->WriteDataToSharedMemory(kTestValueVector);

  ExpectAndRunStartSession(kSaneEmail);

  brillo::ErrorPtr error;
  impl_->LoginScreenStorageStore(
      &error, kTestKey,
      MakeLoginScreenStorageMetadata(/*clear_on_session_exit=*/false),
      kTestValue.size(), value_fd);
  EXPECT_TRUE(error.get());
  EXPECT_FALSE(system_utils_.Exists(GetTestLoginScreenStoragePath(kTestKey)));
  base::ScopedFD out_value_fd;
  uint64_t out_value_size;
  impl_->LoginScreenStorageRetrieve(&error, kTestKey, &out_value_size,
                                    &out_value_fd);
  EXPECT_TRUE(error.get());
}

TEST_F(SessionManagerImplTest, StorePolicyEx_NoSession) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  ExpectStorePolicy(device_policy_service_, policy_blob, kAllKeyFlags);
  ResponseCapturer capturer;
  impl_->StorePolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE, kEmptyAccountId), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicyEx_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  ExpectStorePolicy(device_policy_service_, policy_blob,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW);

  ResponseCapturer capturer;
  impl_->StorePolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE, kEmptyAccountId), policy_blob);
}

TEST_F(SessionManagerImplTest, RetrievePolicyEx) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*device_policy_service_, Retrieve(MakeChromePolicyNamespace(), _))
      .WillOnce(DoAll(SetArgPointee<1>(policy_blob), Return(true)));
  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RetrievePolicyEx(
      &error, MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE, kEmptyAccountId),
      &out_blob));
  EXPECT_FALSE(error.get());
  EXPECT_EQ(policy_blob, out_blob);
}

TEST_F(SessionManagerImplTest, GetServerBackedStateKeys_TimeSync) {
  EXPECT_CALL(device_identifier_generator_, RequestStateKeys(_));

  ResponseCapturer capturer;
  impl_->GetServerBackedStateKeys(
      capturer.CreateMethodResponse<std::vector<std::vector<uint8_t>>>());
  ASSERT_NO_FATAL_FAILURE(GotLastSyncInfo(true));
}

TEST_F(SessionManagerImplTest, GetServerBackedStateKeys_NoTimeSync) {
  EXPECT_CALL(device_identifier_generator_, RequestStateKeys(_)).Times(0);
  ResponseCapturer capturer;
  impl_->GetServerBackedStateKeys(
      capturer.CreateMethodResponse<std::vector<std::vector<uint8_t>>>());
}

TEST_F(SessionManagerImplTest, GetServerBackedStateKeys_TimeSyncDoneBefore) {
  ASSERT_NO_FATAL_FAILURE(GotLastSyncInfo(true));

  EXPECT_CALL(device_identifier_generator_, RequestStateKeys(_));
  ResponseCapturer capturer;
  impl_->GetServerBackedStateKeys(
      capturer.CreateMethodResponse<std::vector<std::vector<uint8_t>>>());
}

TEST_F(SessionManagerImplTest, GetServerBackedStateKeys_FailedTimeSync) {
  ASSERT_NO_FATAL_FAILURE(GotLastSyncInfo(false));

  EXPECT_CALL(device_identifier_generator_, RequestStateKeys(_)).Times(0);
  ResponseCapturer capturer;
  impl_->GetServerBackedStateKeys(
      capturer.CreateMethodResponse<std::vector<std::vector<uint8_t>>>());

  EXPECT_CALL(*system_clock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .Times(1);
  base::RunLoop().RunUntilIdle();
}

TEST_F(SessionManagerImplTest, GetServerBackedStateKeys_TimeSyncAfterFail) {
  ASSERT_NO_FATAL_FAILURE(GotLastSyncInfo(false));

  ResponseCapturer capturer;
  impl_->GetServerBackedStateKeys(
      capturer.CreateMethodResponse<std::vector<std::vector<uint8_t>>>());

  dbus::ObjectProxy::ResponseCallback time_sync_callback;
  EXPECT_CALL(*system_clock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(MovePointee<2>(&time_sync_callback));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(Mock::VerifyAndClearExpectations(system_clock_proxy_.get()));
  ASSERT_FALSE(time_sync_callback.is_null());

  EXPECT_CALL(device_identifier_generator_, RequestStateKeys(_)).Times(1);
  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
  dbus::MessageWriter writer(response.get());
  writer.AppendBool(true);
  std::move(time_sync_callback).Run(response.get());
}

TEST_F(SessionManagerImplTest, GetPsmDeviceActiveSecretSuccess) {
  EXPECT_CALL(device_identifier_generator_, RequestPsmDeviceActiveSecret(_));
  ResponseCapturer capturer;
  impl_->GetPsmDeviceActiveSecret(capturer.CreateMethodResponse<std::string>());
}

TEST_F(SessionManagerImplTest, StoreUserPolicyEx_NoSession) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");

  ResponseCapturer capturer;
  impl_->StorePolicyEx(capturer.CreateMethodResponse<>(),
                       MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail),
                       policy_blob);
  ASSERT_TRUE(capturer.response());
  EXPECT_EQ(dbus_error::kGetServiceFail, capturer.response()->GetErrorName());
}

TEST_F(SessionManagerImplTest, StoreUserPolicyEx_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(
      *user_policy_services_[kSaneEmail],
      Store(MakeChromePolicyNamespace(), policy_blob,
            PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW, _));

  ResponseCapturer capturer;
  impl_->StorePolicyEx(capturer.CreateMethodResponse<>(),
                       MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail),
                       policy_blob);
}

TEST_F(SessionManagerImplTest, StoreUserPolicyEx_SecondSession) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(user_policy_services_[kSaneEmail]);

  // Store policy for the signed-in user.
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(
      *user_policy_services_[kSaneEmail],
      Store(MakeChromePolicyNamespace(), policy_blob,
            PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW, _));

  {
    ResponseCapturer capturer;
    impl_->StorePolicyEx(capturer.CreateMethodResponse<>(),
                         MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail),
                         policy_blob);
    Mock::VerifyAndClearExpectations(user_policy_services_[kSaneEmail]);
  }

  // Storing policy for another username fails before their session starts.
  constexpr char kEmail2[] = "user2@somewhere.com";
  {
    ResponseCapturer capturer;
    impl_->StorePolicyEx(capturer.CreateMethodResponse<>(),
                         MakePolicyDescriptor(ACCOUNT_TYPE_USER, kEmail2),
                         policy_blob);
    ASSERT_TRUE(capturer.response());
    EXPECT_EQ(dbus_error::kGetServiceFail, capturer.response()->GetErrorName());
  }

  // Now start another session for the 2nd user.
  ExpectAndRunStartSession(kEmail2);
  ASSERT_TRUE(user_policy_services_[kEmail2]);

  // Storing policy for that user now succeeds.
  EXPECT_CALL(
      *user_policy_services_[kEmail2],
      Store(MakeChromePolicyNamespace(), policy_blob,
            PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW, _));
  {
    ResponseCapturer capturer;
    impl_->StorePolicyEx(capturer.CreateMethodResponse<>(),
                         MakePolicyDescriptor(ACCOUNT_TYPE_USER, kEmail2),
                         policy_blob);
  }
  Mock::VerifyAndClearExpectations(user_policy_services_[kEmail2]);
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicyEx_NoSession) {
  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->RetrievePolicyEx(
      &error, MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail), &out_blob));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSigEncodeFail, error->GetCode());
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicyEx_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Retrieve(MakeChromePolicyNamespace(), _))
      .WillOnce(DoAll(SetArgPointee<1>(policy_blob), Return(true)));

  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RetrievePolicyEx(
      &error, MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail), &out_blob));
  EXPECT_FALSE(error.get());
  EXPECT_EQ(policy_blob, out_blob);
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicyEx_SecondSession) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(user_policy_services_[kSaneEmail]);

  // Retrieve policy for the signed-in user.
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Retrieve(MakeChromePolicyNamespace(), _))
      .WillOnce(DoAll(SetArgPointee<1>(policy_blob), Return(true)));
  {
    std::vector<uint8_t> out_blob;
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->RetrievePolicyEx(
        &error, MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail),
        &out_blob));
    EXPECT_FALSE(error.get());
    Mock::VerifyAndClearExpectations(user_policy_services_[kSaneEmail]);
    EXPECT_EQ(policy_blob, out_blob);
  }

  // Retrieving policy for another username fails before their session starts.
  constexpr char kEmail2[] = "user2@somewhere.com";
  {
    std::vector<uint8_t> out_blob;
    brillo::ErrorPtr error;
    EXPECT_FALSE(impl_->RetrievePolicyEx(
        &error, MakePolicyDescriptor(ACCOUNT_TYPE_USER, kEmail2), &out_blob));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kSigEncodeFail, error->GetCode());
  }

  // Now start another session for the 2nd user.
  ExpectAndRunStartSession(kEmail2);
  ASSERT_TRUE(user_policy_services_[kEmail2]);

  // Retrieving policy for that user now succeeds.
  EXPECT_CALL(*user_policy_services_[kEmail2],
              Retrieve(MakeChromePolicyNamespace(), _))
      .WillOnce(DoAll(SetArgPointee<1>(policy_blob), Return(true)));
  {
    std::vector<uint8_t> out_blob;
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->RetrievePolicyEx(
        &error, MakePolicyDescriptor(ACCOUNT_TYPE_USER, kEmail2), &out_blob));
    EXPECT_FALSE(error.get());
    Mock::VerifyAndClearExpectations(user_policy_services_[kEmail2]);
    EXPECT_EQ(policy_blob, out_blob);
  }
}

TEST_F(SessionManagerImplTest, StoreDeviceLocalAccountPolicyNoAccount) {
  const std::vector<uint8_t> policy_blob = CreatePolicyFetchResponseBlob();
  base::FilePath policy_path = GetDeviceLocalAccountPolicyPath(kSaneEmail);

  ResponseCapturer capturer;
  impl_->StorePolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE_LOCAL_ACCOUNT, kSaneEmail),
      policy_blob);
  ASSERT_TRUE(capturer.response());
  EXPECT_EQ(dbus_error::kGetServiceFail, capturer.response()->GetErrorName());
  VerifyAndClearExpectations();

  EXPECT_FALSE(system_utils_.Exists(policy_path));
}

TEST_F(SessionManagerImplTest, StoreDeviceLocalAccountPolicySuccess) {
  const std::vector<uint8_t> policy_blob = CreatePolicyFetchResponseBlob();
  base::FilePath policy_path = GetDeviceLocalAccountPolicyPath(kSaneEmail);
  SetupDeviceLocalAccount(kSaneEmail);
  EXPECT_FALSE(system_utils_.Exists(policy_path));
  EXPECT_CALL(owner_key_, Verify(_, _, _)).WillOnce(Return(true));

  brillo::FakeMessageLoop io_loop(nullptr);
  io_loop.SetAsCurrent();

  ResponseCapturer capturer;
  impl_->StorePolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE_LOCAL_ACCOUNT, kSaneEmail),
      policy_blob);
  VerifyAndClearExpectations();

  io_loop.Run();
  EXPECT_TRUE(system_utils_.Exists(policy_path));
}

TEST_F(SessionManagerImplTest, RetrieveDeviceLocalAccountPolicyNoAccount) {
  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->RetrievePolicyEx(
      &error,
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE_LOCAL_ACCOUNT, kSaneEmail),
      &out_blob));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSigEncodeFail, error->GetCode());
}

TEST_F(SessionManagerImplTest, RetrieveDeviceLocalAccountPolicySuccess) {
  const std::vector<uint8_t> policy_blob = CreatePolicyFetchResponseBlob();
  base::FilePath policy_path = GetDeviceLocalAccountPolicyPath(kSaneEmail);
  SetupDeviceLocalAccount(kSaneEmail);
  ASSERT_TRUE(system_utils_.EnsureFile(policy_path, policy_blob));

  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RetrievePolicyEx(
      &error,
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE_LOCAL_ACCOUNT, kSaneEmail),
      &out_blob));
  EXPECT_FALSE(error.get());
  EXPECT_EQ(policy_blob, out_blob);
}

TEST_F(SessionManagerImplTest, RetrieveActiveSessions) {
  ExpectStartSession(kSaneEmail);
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
    EXPECT_FALSE(error.get());
  }
  {
    std::map<std::string, std::string> active_users =
        impl_->RetrieveActiveSessions();
    EXPECT_EQ(active_users.size(), 1);
    EXPECT_EQ(active_users[kSaneEmail],
              *SanitizeUserName(Username(kSaneEmail)));
  }
  VerifyAndClearExpectations();

  constexpr char kEmail2[] = "user2@somewhere";
  ExpectStartSession(kEmail2);
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StartSession(&error, kEmail2, kNothing));
    EXPECT_FALSE(error.get());
  }
  {
    std::map<std::string, std::string> active_users =
        impl_->RetrieveActiveSessions();
    EXPECT_EQ(active_users.size(), 2);
    EXPECT_EQ(active_users[kSaneEmail],
              *SanitizeUserName(Username(kSaneEmail)));
    EXPECT_EQ(active_users[kEmail2], *SanitizeUserName(Username(kEmail2)));
  }
}

TEST_F(SessionManagerImplTest, RetrievePrimarySession) {
  ExpectGuestSession();
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StartSession(&error, *GetGuestUsername(), kNothing));
    EXPECT_FALSE(error.get());
  }
  {
    std::string username;
    std::string sanitized_username;
    impl_->RetrievePrimarySession(&username, &sanitized_username);
    EXPECT_EQ(username, "");
    EXPECT_EQ(sanitized_username, "");
  }
  VerifyAndClearExpectations();

  ExpectStartSession(kSaneEmail);
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
    EXPECT_FALSE(error.get());
  }
  {
    std::string username;
    std::string sanitized_username;
    impl_->RetrievePrimarySession(&username, &sanitized_username);
    EXPECT_EQ(username, kSaneEmail);
    EXPECT_EQ(sanitized_username, *SanitizeUserName(Username(kSaneEmail)));
  }
  VerifyAndClearExpectations();

  constexpr char kEmail2[] = "user2@somewhere";
  ExpectStartSession(kEmail2);
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StartSession(&error, kEmail2, kNothing));
    EXPECT_FALSE(error.get());
  }
  {
    std::string username;
    std::string sanitized_username;
    impl_->RetrievePrimarySession(&username, &sanitized_username);
    EXPECT_EQ(username, kSaneEmail);
    EXPECT_EQ(sanitized_username, *SanitizeUserName(Username(kSaneEmail)));
  }
}

TEST_F(SessionManagerImplTest, IsGuestSessionActive) {
  EXPECT_FALSE(impl_->IsGuestSessionActive());
  ExpectAndRunGuestSession();
  EXPECT_TRUE(impl_->IsGuestSessionActive());
  ExpectAndRunStartSession(kSaneEmail);
  EXPECT_FALSE(impl_->IsGuestSessionActive());
}

TEST_F(SessionManagerImplTest, RestartJobBadSocket) {
  brillo::ErrorPtr error;
  auto mode = static_cast<uint32_t>(SessionManagerImpl::RestartJobMode::kGuest);
  EXPECT_FALSE(impl_->RestartJob(&error, base::ScopedFD(), {}, mode));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kGetPeerCredsFailed, error->GetCode());
}

TEST_F(SessionManagerImplTest, RestartJobBadPid) {
  base::ScopedFD fd0_closer, fd1;
  EXPECT_TRUE(CreateSocketPair(&fd0_closer, &fd1));

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(false));
  brillo::ErrorPtr error;
  auto mode = static_cast<uint32_t>(SessionManagerImpl::RestartJobMode::kGuest);
  EXPECT_FALSE(impl_->RestartJob(&error, fd1, {}, mode));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kUnknownPid, error->GetCode());
}

TEST_F(SessionManagerImplTest, RestartJobGuestFailure) {
  const std::vector<std::string> argv(std::begin(kUserlessArgv),
                                      std::end(kUserlessArgv));

  base::ScopedFD fd0_closer, fd1;
  EXPECT_TRUE(CreateSocketPair(&fd0_closer, &fd1));

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(true));
  brillo::ErrorPtr error;
  auto mode = static_cast<uint32_t>(SessionManagerImpl::RestartJobMode::kGuest);
  EXPECT_FALSE(impl_->RestartJob(&error, fd1, argv, mode));
  EXPECT_EQ(dbus_error::kInvalidParameter, error->GetCode());
}

TEST_F(SessionManagerImplTest, RestartJobModeMismatch) {
  const std::vector<std::string> argv(std::begin(kGuestArgv),
                                      std::end(kGuestArgv));

  base::ScopedFD fd0_closer, fd1;
  EXPECT_TRUE(CreateSocketPair(&fd0_closer, &fd1));

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(true));
  brillo::ErrorPtr error;
  auto mode =
      static_cast<uint32_t>(SessionManagerImpl::RestartJobMode::kUserless);
  EXPECT_FALSE(impl_->RestartJob(&error, fd1, argv, mode));
  EXPECT_EQ(dbus_error::kInvalidParameter, error->GetCode());
}

TEST_F(SessionManagerImplTest, RestartJobSuccess) {
  const std::vector<std::string> argv(std::begin(kGuestArgv),
                                      std::end(kGuestArgv));

  base::ScopedFD fd0_closer, fd1;
  EXPECT_TRUE(CreateSocketPair(&fd0_closer, &fd1));

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, SetBrowserArgs(ElementsAreArray(argv))).Times(1);
  EXPECT_CALL(manager_, RestartBrowser()).Times(1);
  ExpectGuestSession();

  brillo::ErrorPtr error;
  auto mode = static_cast<uint32_t>(SessionManagerImpl::RestartJobMode::kGuest);
  EXPECT_TRUE(impl_->RestartJob(&error, fd1, argv, mode));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, RestartJobUserlessSuccess) {
  const std::vector<std::string> argv(std::begin(kUserlessArgv),
                                      std::end(kUserlessArgv));

  base::ScopedFD fd0_closer, fd1;
  EXPECT_TRUE(CreateSocketPair(&fd0_closer, &fd1));

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, SetBrowserArgs(ElementsAreArray(argv))).Times(1);
  EXPECT_CALL(manager_, RestartBrowser()).Times(1);

  brillo::ErrorPtr error;
  auto mode =
      static_cast<uint32_t>(SessionManagerImpl::RestartJobMode::kUserless);
  EXPECT_TRUE(impl_->RestartJob(&error, fd1, argv, mode));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, RestartJobForNonGuestUserFailure) {
  const std::vector<std::string> argv(std::begin(kUserlessArgv),
                                      std::end(kUserlessArgv));

  // Start session.
  ExpectStartSession(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));

  base::ScopedFD fd0_closer, fd1;
  EXPECT_TRUE(CreateSocketPair(&fd0_closer, &fd1));

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(true));

  auto mode =
      static_cast<uint32_t>(SessionManagerImpl::RestartJobMode::kUserless);
  EXPECT_FALSE(impl_->RestartJob(&error, fd1, argv, mode));
  EXPECT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kInvalidParameter, error->GetCode());
}

TEST_F(SessionManagerImplTest, LockScreen) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->LockScreen(&error));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(impl_->ShouldEndSession(nullptr));
}

TEST_F(SessionManagerImplTest, LockScreen_MultiSession) {
  ExpectAndRunStartSession("user@somewhere");
  ExpectAndRunStartSession("user2@somewhere");
  ExpectLockScreen();
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->LockScreen(&error));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(impl_->ShouldEndSession(nullptr));
}

TEST_F(SessionManagerImplTest, LockScreen_NoSession) {
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->LockScreen(&error));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error->GetCode());
}

TEST_F(SessionManagerImplTest, LockScreen_Guest) {
  ExpectAndRunGuestSession();
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->LockScreen(&error));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSessionExists, error->GetCode());
}

TEST_F(SessionManagerImplTest, LockScreen_UserAndGuest) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectAndRunGuestSession();
  ExpectLockScreen();
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->LockScreen(&error));
  ASSERT_FALSE(error.get());
  EXPECT_TRUE(impl_->ShouldEndSession(nullptr));
}

TEST_F(SessionManagerImplTest, LockUnlockScreen) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  brillo::ErrorPtr error;
  EXPECT_CALL(
      *init_controller_,
      TriggerImpulse(SessionManagerImpl::kScreenLockedImpulse, ElementsAre(),
                     InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  EXPECT_TRUE(impl_->LockScreen(&error));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(impl_->ShouldEndSession(nullptr));

  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kScreenIsLockedSignal)))
      .Times(1);
  impl_->HandleLockScreenShown();
  EXPECT_TRUE(impl_->ShouldEndSession(nullptr));

  EXPECT_TRUE(impl_->IsScreenLocked());

  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kScreenIsUnlockedSignal)))
      .Times(1);
  EXPECT_CALL(
      *init_controller_,
      TriggerImpulse(SessionManagerImpl::kScreenUnlockedImpulse, ElementsAre(),
                     InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  impl_->HandleLockScreenDismissed();
  EXPECT_FALSE(impl_->ShouldEndSession(nullptr));

  EXPECT_FALSE(impl_->IsScreenLocked());
}

TEST_F(SessionManagerImplTest, EndSessionBeforeSuspend) {
  const base::TimeTicks crash_time = tick_clock_->NowTicks();
  auto set_expectations = [&](bool should_stop) {
    EXPECT_CALL(manager_, GetLastBrowserRestartTime())
        .WillRepeatedly(Return(crash_time));
    EXPECT_CALL(manager_, ScheduleShutdown()).Times(should_stop ? 1 : 0);
  };

  // The session should be ended in response to a SuspendImminent signal.
  set_expectations(true);
  dbus::Signal imminent_signal(power_manager::kPowerManagerInterface,
                               power_manager::kSuspendImminentSignal);
  suspend_imminent_callback_.Run(&imminent_signal);
  Mock::VerifyAndClearExpectations(&manager_);

  // It should also be ended if a small amount of time passes between the
  // restart and the signal.
  tick_clock_->Advance(SessionManagerImpl::kCrashBeforeSuspendInterval);
  set_expectations(true);
  suspend_imminent_callback_.Run(&imminent_signal);
  Mock::VerifyAndClearExpectations(&manager_);

  // We shouldn't end the session after the specified interval has elapsed.
  tick_clock_->Advance(base::Seconds(1));
  set_expectations(false);
  suspend_imminent_callback_.Run(&imminent_signal);
}

TEST_F(SessionManagerImplTest, EndSessionDuringAndAfterSuspend) {
  EXPECT_CALL(manager_, GetLastBrowserRestartTime())
      .WillRepeatedly(Return(base::TimeTicks()));

  // Initially, we should restart Chrome if it crashes.
  EXPECT_FALSE(impl_->ShouldEndSession(nullptr));

  // Right after suspend starts, we should end the session instead.
  dbus::Signal imminent_signal(power_manager::kPowerManagerInterface,
                               power_manager::kSuspendImminentSignal);
  suspend_imminent_callback_.Run(&imminent_signal);
  EXPECT_TRUE(impl_->ShouldEndSession(nullptr));

  // We should also end it if some time passes...
  tick_clock_->Advance(base::Seconds(20));
  EXPECT_TRUE(impl_->ShouldEndSession(nullptr));

  // ... and right after resume finishes...
  dbus::Signal done_signal(power_manager::kPowerManagerInterface,
                           power_manager::kSuspendDoneSignal);
  suspend_done_callback_.Run(&done_signal);
  EXPECT_TRUE(impl_->ShouldEndSession(nullptr));

  // ... and for a period of time after that.
  tick_clock_->Advance(SessionManagerImpl::kCrashAfterSuspendInterval);
  EXPECT_TRUE(impl_->ShouldEndSession(nullptr));

  // If we wait long enough, we should go back to restarting Chrome.
  tick_clock_->Advance(base::Seconds(1));
  EXPECT_FALSE(impl_->ShouldEndSession(nullptr));
}

TEST_F(SessionManagerImplTest, StartDeviceWipe) {
  // Just make sure the device is being restart as a basic check of
  // InitiateDeviceWipe() invocation.
  ExpectDeviceRestart(1);

  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartDeviceWipe(&error));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, StartDeviceWipe_AlreadyLoggedIn) {
  base::FilePath logged_in_path(SessionManagerImpl::kLoggedInFlag);
  ASSERT_FALSE(system_utils_.Exists(logged_in_path));
  ASSERT_TRUE(system_utils_.WriteStringToFile(logged_in_path, "1"));
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->StartDeviceWipe(&error));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSessionExists, error->GetCode());
}

TEST_F(SessionManagerImplTest,
       StartRemoteDeviceWipe_CorrectlySignedShouldPowerwash) {
  ExpectDeviceRestart(1);
  brillo::ErrorPtr error;
  std::vector<uint8_t> signed_command;
  signed_command.push_back(1);
  EXPECT_CALL(
      *device_policy_service_,
      ValidateRemoteDeviceWipeCommand(_, em::PolicyFetchRequest::SHA256_RSA))
      .WillOnce(Return(true));

  EXPECT_TRUE(impl_->StartRemoteDeviceWipe(&error, signed_command));

  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest,
       StartRemoteDeviceWipe_IncorrectlySignedShouldFail) {
  ExpectDeviceRestart(0);
  brillo::ErrorPtr error;
  std::vector<uint8_t> signed_command;
  signed_command.push_back(1);
  EXPECT_CALL(
      *device_policy_service_,
      ValidateRemoteDeviceWipeCommand(_, em::PolicyFetchRequest::SHA256_RSA))
      .WillOnce(Return(false));

  EXPECT_FALSE(impl_->StartRemoteDeviceWipe(&error, signed_command));

  EXPECT_EQ(dbus_error::kInvalidParameter, error->GetCode());
}

TEST_F(SessionManagerImplTest, InitiateDeviceWipe_TooLongReason) {
  ASSERT_TRUE(system_utils_.RemoveFile(
      base::FilePath(SessionManagerImpl::kLoggedInFlag)));
  ExpectDeviceRestart(1);
  impl_->InitiateDeviceWipe(
      "overly long test message with\nspecial/chars$\t\xa4\xd6 1234567890");
  std::string contents;
  ASSERT_TRUE(system_utils_.ReadFileToString(
      base::FilePath(SessionManagerImpl::kResetFile), &contents));
  ASSERT_EQ(
      "fast safe keepimg preserve_lvs reason="
      "overly_long_test_message_with_special_chars_____12",
      contents);
}

TEST_F(SessionManagerImplTest, ClearBlockDevmodeVpd) {
  ResponseCapturer capturer;
  EXPECT_CALL(*device_policy_service_, ClearBlockDevmode(_)).Times(1);
  impl_->ClearBlockDevmodeVpd(capturer.CreateMethodResponse<>());
}

TEST_F(SessionManagerImplTest, DisconnectLogFile) {
  // Write a log file and create a relative symlink pointing at it.
  constexpr char kData[] = "fake log data";
  const base::FilePath kLogFile = log_dir_.GetPath().Append("ui.real");
  ASSERT_TRUE(base::WriteFile(kLogFile, kData));
  ASSERT_TRUE(base::CreateSymbolicLink(kLogFile.BaseName(), log_symlink_));

  struct stat st;
  ASSERT_EQ(0, stat(kLogFile.value().c_str(), &st));
  const ino_t orig_inode = st.st_ino;

  ExpectAndRunStartSession(kSaneEmail);

  // The file should still contain the same data...
  std::string data;
  ASSERT_TRUE(base::ReadFileToString(kLogFile, &data));
  EXPECT_EQ(kData, data);

  // ... but its inode should've changed.
  ASSERT_EQ(0, stat(kLogFile.value().c_str(), &st));
  const ino_t updated_inode = st.st_ino;
  EXPECT_NE(orig_inode, updated_inode);

  // Start a second session. The log file shouldn't be modified this time.
  constexpr char kEmail2[] = "user2@somewhere.com";
  ExpectAndRunStartSession(kEmail2);
  ASSERT_EQ(0, stat(kLogFile.value().c_str(), &st));
  EXPECT_EQ(updated_inode, st.st_ino);
}

TEST_F(SessionManagerImplTest, DontDisconnectLogFileInOtherDir) {
  // Write a log file to a subdirectory and create an absolute symlink.
  constexpr char kData[] = "fake log data";
  const base::FilePath kSubdir = log_dir_.GetPath().Append("subdir");
  ASSERT_TRUE(base::CreateDirectory(kSubdir));
  const base::FilePath kLogFile = kSubdir.Append("ui.real");
  ASSERT_TRUE(base::WriteFile(kLogFile, kData));
  ASSERT_TRUE(base::CreateSymbolicLink(kLogFile, log_symlink_));

  struct stat st;
  ASSERT_EQ(0, stat(kLogFile.value().c_str(), &st));
  const ino_t orig_inode = st.st_ino;

  ExpectAndRunStartSession(kSaneEmail);

  // The inode should stay the same since the symlink points to a file in a
  // different directory.
  ASSERT_EQ(0, stat(kLogFile.value().c_str(), &st));
  EXPECT_EQ(orig_inode, st.st_ino);
}

class StartTPMFirmwareUpdateTest : public SessionManagerImplTest {
 public:
  void SetUp() override {
    SessionManagerImplTest::SetUp();

    SetDeviceMode("consumer");

    ASSERT_TRUE(system_utils_.EnsureFile(
        base::FilePath(SessionManagerImpl::kTPMFirmwareUpdateLocationFile),
        "/lib/firmware/tpm/fake.bin"));
    ASSERT_TRUE(system_utils_.EnsureFile(
        base::FilePath(
            SessionManagerImpl::kTPMFirmwareUpdateSRKVulnerableROCAFile),
        ""));
  }

  void TearDown() override {
    brillo::ErrorPtr error;
    bool result = impl_->StartTPMFirmwareUpdate(&error, update_mode_);
    if (expected_error_.empty()) {
      EXPECT_TRUE(result);
      EXPECT_FALSE(error);
      std::string contents;
      ASSERT_TRUE(system_utils_.ReadFileToString(
          base::FilePath(SessionManagerImpl::kTPMFirmwareUpdateRequestFlagFile),
          &contents));
      EXPECT_EQ(update_mode_, contents);

      if (update_mode_ == "preserve_stateful") {
        ASSERT_TRUE(system_utils_.Exists(base::FilePath(
            SessionManagerImpl::kStatefulPreservationRequestFile)));
        EXPECT_EQ(1, crossystem_.VbGetSystemPropertyInt(
                         crossystem::Crossystem::kClearTpmOwnerRequest));
      }
    } else {
      EXPECT_FALSE(result);
      ASSERT_TRUE(error);
      EXPECT_EQ(expected_error_, error->GetCode());
    }

    SessionManagerImplTest::TearDown();
  }

  void ExpectError(const std::string& error) { expected_error_ = error; }

  void SetUpdateMode(const std::string& mode) { update_mode_ = mode; }

  std::string update_mode_ = "first_boot";
  std::string expected_error_;
  bool file_write_status_ = true;
};

TEST_F(StartTPMFirmwareUpdateTest, Success) {
  ExpectDeviceRestart(1);
}

TEST_F(StartTPMFirmwareUpdateTest, AlreadyLoggedIn) {
  ASSERT_TRUE(system_utils_.EnsureFile(
      base::FilePath(SessionManagerImpl::kLoggedInFlag), ""));
  ExpectError(dbus_error::kSessionExists);
}

TEST_F(StartTPMFirmwareUpdateTest, BadUpdateMode) {
  SetUpdateMode("no_such_thing");
  ExpectError(dbus_error::kInvalidParameter);
}

TEST_F(StartTPMFirmwareUpdateTest, EnterpriseFirstBootNotSet) {
  SetDeviceMode("enterprise");
  ExpectError(dbus_error::kNotAvailable);
}

TEST_F(StartTPMFirmwareUpdateTest, EnterpriseFirstBootAllowed) {
  SetDeviceMode("enterprise");
  em::ChromeDeviceSettingsProto settings;
  settings.mutable_tpm_firmware_update_settings()
      ->set_allow_user_initiated_powerwash(true);
  SetDevicePolicy(settings);
  ExpectDeviceRestart(1);
}

TEST_F(StartTPMFirmwareUpdateTest, EnterprisePreserveStatefulNotSet) {
  SetUpdateMode("preserve_stateful");
  SetDeviceMode("enterprise");
  ExpectError(dbus_error::kNotAvailable);
}

TEST_F(StartTPMFirmwareUpdateTest, EnterprisePreserveStatefulAllowed) {
  SetUpdateMode("preserve_stateful");
  SetDeviceMode("enterprise");
  em::ChromeDeviceSettingsProto settings;
  settings.mutable_tpm_firmware_update_settings()
      ->set_allow_user_initiated_preserve_device_state(true);
  SetDevicePolicy(settings);
  ExpectDeviceRestart(1);
}

TEST_F(StartTPMFirmwareUpdateTest, EnterpriseCleanupDisallowed) {
  SetUpdateMode("cleanup");
  ASSERT_TRUE(system_utils_.EnsureFile(
      base::FilePath(SessionManagerImpl::kTPMFirmwareUpdateLocationFile), ""));
  SetDeviceMode("enterprise");
  ExpectError(dbus_error::kNotAvailable);
}

TEST_F(StartTPMFirmwareUpdateTest, EnterpriseCleanupAllowed) {
  SetUpdateMode("cleanup");
  ASSERT_TRUE(system_utils_.EnsureFile(
      base::FilePath(SessionManagerImpl::kTPMFirmwareUpdateLocationFile), ""));
  em::ChromeDeviceSettingsProto settings;
  settings.mutable_tpm_firmware_update_settings()
      ->set_allow_user_initiated_preserve_device_state(true);
  SetDevicePolicy(settings);
  ExpectDeviceRestart(1);
}

TEST_F(StartTPMFirmwareUpdateTest, AvailabilityNotDecided) {
  ASSERT_TRUE(system_utils_.RemoveFile(
      base::FilePath(SessionManagerImpl::kTPMFirmwareUpdateLocationFile)));
  ExpectError(dbus_error::kNotAvailable);
}

TEST_F(StartTPMFirmwareUpdateTest, NoUpdateAvailable) {
  ASSERT_TRUE(system_utils_.EnsureFile(
      base::FilePath(SessionManagerImpl::kTPMFirmwareUpdateLocationFile), ""));
  ExpectError(dbus_error::kNotAvailable);
}

TEST_F(StartTPMFirmwareUpdateTest, CleanupSRKVulnerable) {
  ASSERT_TRUE(system_utils_.EnsureFile(
      base::FilePath(SessionManagerImpl::kTPMFirmwareUpdateLocationFile), ""));
  ExpectError(dbus_error::kNotAvailable);
}

TEST_F(StartTPMFirmwareUpdateTest, CleanupSRKNotVulnerable) {
  ASSERT_TRUE(system_utils_.EnsureFile(
      base::FilePath(SessionManagerImpl::kTPMFirmwareUpdateLocationFile), ""));
  ASSERT_TRUE(system_utils_.RemoveFile(base::FilePath(
      SessionManagerImpl::kTPMFirmwareUpdateSRKVulnerableROCAFile)));
  ExpectError(dbus_error::kNotAvailable);
}

TEST_F(StartTPMFirmwareUpdateTest, RequestFileWriteFailure) {
  system_utils_.set_atomic_file_write_success(false);
  ExpectError(dbus_error::kNotAvailable);
}

TEST_F(StartTPMFirmwareUpdateTest, PreserveStateful) {
  update_mode_ = "preserve_stateful";
  ExpectDeviceRestart(1);
}

}  // namespace login_manager
