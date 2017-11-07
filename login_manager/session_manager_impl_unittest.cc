// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_impl.h"

#include <fcntl.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <base/strings/string_util.h>
#include <brillo/cryptohome.h>
#include <brillo/dbus/dbus_param_writer.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <crypto/scoped_nss_types.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/mock_exported_object.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "login_manager/blob_util.h"
#include "login_manager/dbus_util.h"
#include "login_manager/device_local_account_manager.h"
#include "login_manager/fake_container_manager.h"
#include "login_manager/fake_crossystem.h"
#include "login_manager/fake_termina_manager.h"
#include "login_manager/file_checker.h"
#include "login_manager/matchers.h"
#include "login_manager/mock_device_policy_service.h"
#include "login_manager/mock_file_checker.h"
#include "login_manager/mock_init_daemon_controller.h"
#include "login_manager/mock_install_attributes_reader.h"
#include "login_manager/mock_key_generator.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_nss_util.h"
#include "login_manager/mock_object_proxy.h"
#include "login_manager/mock_policy_key.h"
#include "login_manager/mock_policy_service.h"
#include "login_manager/mock_process_manager_service.h"
#include "login_manager/mock_server_backed_state_key_generator.h"
#include "login_manager/mock_system_utils.h"
#include "login_manager/mock_user_policy_service_factory.h"
#include "login_manager/mock_vpd_process.h"
#include "login_manager/proto_bindings/arc.pb.h"
#include "login_manager/proto_bindings/policy_descriptor.pb.h"
#include "login_manager/system_utils_impl.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::WithArg;

using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SetSystemSalt;

using enterprise_management::ChromeDeviceSettingsProto;
using enterprise_management::PolicyData;
using enterprise_management::PolicyFetchResponse;

using std::map;
using std::string;
using std::vector;

namespace login_manager {

namespace {

// Test Bus instance to inject MockExportedObject.
class FakeBus : public dbus::Bus {
 public:
  FakeBus()
      : dbus::Bus(GetBusOptions()),
        exported_object_(
            new dbus::MockExportedObject(nullptr, dbus::ObjectPath())) {}

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
  T value;
};

template <>
struct PayloadStorage<const char*> {
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
          brillo::dbus_utils::PopValueFromReader(&reader, &actual1.value) &&
          payload1 == actual1.value);
}

MATCHER_P3(SignalEq, method_name, payload1, payload2, "") {
  PayloadStorage<decltype(payload1)> actual1;
  PayloadStorage<decltype(payload2)> actual2;
  dbus::MessageReader reader(arg);
  return (arg->GetMember() == method_name &&
          brillo::dbus_utils::PopValueFromReader(&reader, &actual1.value) &&
          payload1 == actual1.value &&
          brillo::dbus_utils::PopValueFromReader(&reader, &actual2.value) &&
          payload2 == actual2.value);
}

constexpr pid_t kAndroidPid = 10;

enum class DataDirType {
  DATA_DIR_AVAILABLE = 0,
  DATA_DIR_MISSING = 1,
};

enum class OldDataDirType {
  OLD_DATA_DIR_NOT_EMPTY = 0,
  OLD_DATA_DIR_EMPTY = 1,
  OLD_DATA_FILE_EXISTS = 2,
};

constexpr char kSaneEmail[] = "user@somewhere.com";

StartArcInstanceRequest CreateStartArcInstanceRequestForUser() {
  StartArcInstanceRequest request;
  request.set_account_id(kSaneEmail);
  request.set_skip_boot_completed_broadcast(false);
  request.set_scan_vendor_priv_app(false);
  return request;
}

#if USE_CHEETS
// gmock 1.7 does not support returning move-only-type value.
// Usage:
//   EXPECT_CALL(
//       *init_controller_,
//       TriggerImpulseInternal(...args...))
//       .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
dbus::Response* CreateEmptyResponse() {
  return dbus::Response::CreateEmpty().release();
}
#endif

// Captures the D-Bus Response object passed via DBusMethodResponse via
// ResponseSender.
//
// Example Usage:
//   ResponseCapturer capture;
//   impl_->SomeAsyncDBusMethod(capturer.CreateMethodResponse(), ...);
//   EXPECT_EQ(SomeErrorName, capture.response()->GetErrorName());
class ResponseCapturer {
 public:
  ResponseCapturer()
      : call_("org.chromium.SessionManagerInterface", "DummyDbusMethod"),
        weak_ptr_factory_(this) {
    call_.SetSerial(1);  // Dummy serial is needed.
  }

  ~ResponseCapturer() = default;

  // Needs to be non-const, because some accessors like GetErrorName() are
  // non-const.
  dbus::Response* response() { return response_.get(); }

  template <typename... Types>
  std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<Types...>>
  CreateMethodResponse() {
    return std::make_unique<brillo::dbus_utils::DBusMethodResponse<Types...>>(
        &call_,
        base::Bind(&ResponseCapturer::Capture, weak_ptr_factory_.GetWeakPtr()));
  }

 private:
  void Capture(std::unique_ptr<dbus::Response> response) {
    DCHECK(!response_);
    response_ = std::move(response);
  }

  dbus::MethodCall call_;
  std::unique_ptr<dbus::Response> response_;
  base::WeakPtrFactory<ResponseCapturer> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(ResponseCapturer);
};

constexpr char kEmptyAccountId[] = "";

std::vector<uint8_t> MakePolicyDescriptor(PolicyAccountType account_type,
                                          const std::string& account_id) {
  PolicyDescriptor descriptor;
  descriptor.set_account_type(account_type);
  descriptor.set_account_id(account_id);
  descriptor.set_domain(POLICY_DOMAIN_CHROME);
  return StringToBlob(descriptor.SerializeAsString());
}

}  // namespace

class SessionManagerImplTest : public ::testing::Test,
                               public SessionManagerImpl::Delegate {
 public:
  SessionManagerImplTest()
      : bus_(new FakeBus()),
        state_key_generator_(&utils_, &metrics_),
        android_container_(kAndroidPid),
        component_updater_proxy_(new MockObjectProxy()),
        system_clock_proxy_(new MockObjectProxy()) {}

  ~SessionManagerImplTest() override = default;

  void SetUp() override {
    ON_CALL(utils_, GetDevModeState())
        .WillByDefault(Return(DevModeState::DEV_MODE_OFF));
    ON_CALL(utils_, GetVmState()).WillByDefault(Return(VmState::OUTSIDE_VM));

    // Forward file operation calls to |real_utils_| so that the tests can
    // actually create/modify/delete files in |tmpdir_|.
    ON_CALL(utils_, EnsureAndReturnSafeFileSize(_, _))
        .WillByDefault(Invoke(&real_utils_,
                              &SystemUtilsImpl::EnsureAndReturnSafeFileSize));
    ON_CALL(utils_, Exists(_))
        .WillByDefault(Invoke(&real_utils_, &SystemUtilsImpl::Exists));
    ON_CALL(utils_, DirectoryExists(_))
        .WillByDefault(Invoke(&real_utils_, &SystemUtilsImpl::DirectoryExists));
    ON_CALL(utils_, IsDirectoryEmpty(_))
        .WillByDefault(
            Invoke(&real_utils_, &SystemUtilsImpl::IsDirectoryEmpty));
    ON_CALL(utils_, CreateReadOnlyFileInTempDir(_))
        .WillByDefault(Invoke(&real_utils_,
                              &SystemUtilsImpl::CreateReadOnlyFileInTempDir));
    ON_CALL(utils_, CreateTemporaryDirIn(_, _))
        .WillByDefault(
            Invoke(&real_utils_, &SystemUtilsImpl::CreateTemporaryDirIn));
    ON_CALL(utils_, CreateDir(_))
        .WillByDefault(Invoke(&real_utils_, &SystemUtilsImpl::CreateDir));
    ON_CALL(utils_, GetUniqueFilenameInWriteOnlyTempDir(_))
        .WillByDefault(
            Invoke(&real_utils_,
                   &SystemUtilsImpl::GetUniqueFilenameInWriteOnlyTempDir));
    ON_CALL(utils_, RemoveDirTree(_))
        .WillByDefault(Invoke(&real_utils_, &SystemUtilsImpl::RemoveDirTree));
    ON_CALL(utils_, RemoveFile(_))
        .WillByDefault(Invoke(&real_utils_, &SystemUtilsImpl::RemoveFile));
    ON_CALL(utils_, RenameDir(_, _))
        .WillByDefault(Invoke(&real_utils_, &SystemUtilsImpl::RenameDir));
    ON_CALL(utils_, AtomicFileWrite(_, _))
        .WillByDefault(Invoke(&real_utils_, &SystemUtilsImpl::AtomicFileWrite));

    // 10 GB Free Disk Space for ARC launch.
    ON_CALL(utils_, AmountOfFreeDiskSpace(_)).WillByDefault(Return(10LL << 30));

    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    real_utils_.set_base_dir_for_testing(tmpdir_.path());
    SetSystemSalt(&fake_salt_);

#if USE_CHEETS
    android_data_dir_ =
        SessionManagerImpl::GetAndroidDataDirForUser(kSaneEmail);
    android_data_old_dir_ =
        SessionManagerImpl::GetAndroidDataOldDirForUser(kSaneEmail);
#endif  // USE_CHEETS

    // AtomicFileWrite calls in TEST_F assume that these directories exist.
    ASSERT_TRUE(utils_.CreateDir(
        base::FilePath(FILE_PATH_LITERAL("/run/session_manager"))));
    ASSERT_TRUE(utils_.CreateDir(
        base::FilePath(FILE_PATH_LITERAL("/mnt/stateful_partition"))));

    init_controller_ = new MockInitDaemonController();
    impl_ = std::make_unique<SessionManagerImpl>(
        this /* delegate */, base::WrapUnique(init_controller_), bus_.get(),
        &key_gen_, &state_key_generator_, &manager_, &metrics_, &nss_, &utils_,
        &crossystem_, &vpd_process_, &owner_key_, &android_container_,
        &termina_manager_, &install_attributes_reader_,
        component_updater_proxy_.get(), system_clock_proxy_.get());
    impl_->SetSystemClockLastSyncInfoRetryDelayForTesting(base::TimeDelta());

    device_policy_store_ = new MockPolicyStore();
    device_policy_service_ = new MockDevicePolicyService(
        std::unique_ptr<MockPolicyStore>(device_policy_store_), &owner_key_);
    user_policy_service_factory_ =
        new testing::NiceMock<MockUserPolicyServiceFactory>();
    ON_CALL(*user_policy_service_factory_, Create(_))
        .WillByDefault(
            Invoke(this, &SessionManagerImplTest::CreateUserPolicyService));
    ON_CALL(*user_policy_service_factory_, CreateForHiddenUserHome(_))
        .WillByDefault(Invoke(
            this,
            &SessionManagerImplTest::CreateUserPolicyServiceForHiddenUserHome));
    auto device_local_account_policy =
        std::make_unique<DeviceLocalAccountPolicyService>(tmpdir_.path(),
                                                          nullptr);
    impl_->SetPolicyServicesForTest(
        base::WrapUnique(device_policy_service_),
        base::WrapUnique(user_policy_service_factory_),
        std::move(device_local_account_policy));

    EXPECT_CALL(*system_clock_proxy_, WaitForServiceToBeAvailable(_))
        .WillOnce(SaveArg<0>(&available_callback_));
    impl_->Initialize();
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(system_clock_proxy_.get()));
    ASSERT_FALSE(available_callback_.is_null());

    EXPECT_CALL(*exported_object(), ExportMethodAndBlock(_, _, _))
        .WillRepeatedly(Return(true));
    impl_->StartDBusService();
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(exported_object()));
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
    ExpectSessionBoilerplate(account_id_string, false, false);
  }

  void ExpectGuestSession() {
    ExpectSessionBoilerplate(kGuestUserName, true, false);
  }

  void ExpectStartOwnerSession(const string& account_id_string) {
    ExpectSessionBoilerplate(account_id_string, false, true);
  }

  void ExpectStartSessionUnowned(const string& account_id_string) {
    ExpectStartSessionUnownedBoilerplate(account_id_string,
                                         false,  // mitigating
                                         true);  // key_gen
  }

  void ExpectStartSessionOwningInProcess(const string& account_id_string) {
    ExpectStartSessionUnownedBoilerplate(account_id_string,
                                         false,   // mitigating
                                         false);  // key_gen
  }

  void ExpectStartSessionOwnerLost(const string& account_id_string) {
    ExpectStartSessionUnownedBoilerplate(account_id_string,
                                         true,    // mitigating
                                         false);  // key_gen
  }

  void ExpectStartSessionActiveDirectory(const string& account_id_string) {
    ExpectStartSessionUnownedBoilerplate(account_id_string,
                                         false,   // mitigating
                                         false);  // key_gen
  }
  void ExpectRemoveArcData(DataDirType data_dir_type,
                           OldDataDirType old_data_dir_type) {
#if USE_CHEETS
    if (data_dir_type == DataDirType::DATA_DIR_MISSING &&
        old_data_dir_type == OldDataDirType::OLD_DATA_DIR_EMPTY) {
      return;  // RemoveArcDataInternal does nothing in this case.
    }
    EXPECT_CALL(
        *init_controller_,
        TriggerImpulseInternal(SessionManagerImpl::kRemoveOldArcDataImpulse,
                               ElementsAre(StartsWith("ANDROID_DATA_OLD_DIR=")),
                               InitDaemonController::TriggerMode::ASYNC))
        .WillOnce(Return(nullptr));
#endif
  }

  void ExpectLockScreen() { expected_locks_ = 1; }

  void ExpectDeviceRestart() { expected_restarts_ = 1; }

  void ExpectStorePolicy(MockDevicePolicyService* service,
                         const std::vector<uint8_t>& policy_blob,
                         int flags,
                         SignatureCheck signature_check) {
    EXPECT_CALL(*service, Store(policy_blob, flags, signature_check, _))
        .WillOnce(Return(true));
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
    EXPECT_TRUE(impl_->StartSession(&error, kGuestUserName, kNothing));
    EXPECT_FALSE(error.get());
    VerifyAndClearExpectations();
  }

  void ExpectStartArcInstance() {
    EXPECT_CALL(
        utils_,
        CreateServerHandle(
            // Use Field() since NamedPlatformHandle does not have operator==().
            Field(&NamedPlatformHandle::name,
                  SessionManagerImpl::kArcBridgeSocketPath)))
        .WillOnce(Invoke(this, &SessionManagerImplTest::CreateDummyHandle));
    EXPECT_CALL(utils_,
                GetGroupInfo(SessionManagerImpl::kArcBridgeSocketGroup, _))
        .WillOnce(DoAll(SetArgPointee<1>(getgid()), Return(true)));
    EXPECT_CALL(
        utils_,
        ChangeOwner(base::FilePath(SessionManagerImpl::kArcBridgeSocketPath),
                    -1, _))
        .WillOnce(Return(true));
    EXPECT_CALL(
        utils_,
        SetPosixFilePermissions(
            base::FilePath(SessionManagerImpl::kArcBridgeSocketPath), 0660))
        .WillOnce(Return(true));
  }

  std::unique_ptr<PolicyService> CreateUserPolicyService(
      const string& username) {
    std::unique_ptr<MockPolicyService> policy_service =
        std::make_unique<MockPolicyService>();
    user_policy_services_[username] = policy_service.get();
    return policy_service;
  }

  std::unique_ptr<PolicyService> CreateUserPolicyServiceForHiddenUserHome(
      const string& username) {
    EXPECT_EQ(username, hidden_user_home_expected_username_);
    return std::move(hidden_user_home_policy_service_);
  }

  void VerifyAndClearExpectations() {
    Mock::VerifyAndClearExpectations(device_policy_service_);
    for (auto& entry : user_policy_services_)
      Mock::VerifyAndClearExpectations(entry.second);
    Mock::VerifyAndClearExpectations(init_controller_);
    Mock::VerifyAndClearExpectations(&manager_);
    Mock::VerifyAndClearExpectations(&metrics_);
    Mock::VerifyAndClearExpectations(&nss_);
    Mock::VerifyAndClearExpectations(&utils_);
    Mock::VerifyAndClearExpectations(exported_object());
  }

  void GotLastSyncInfo(bool network_synchronized) {
    ASSERT_FALSE(available_callback_.is_null());

    dbus::ObjectProxy::ResponseCallback time_sync_callback;
    EXPECT_CALL(*system_clock_proxy_,
                CallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
        .WillOnce(SaveArg<2>(&time_sync_callback));
    available_callback_.Run(true);
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(system_clock_proxy_.get()));

    std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    writer.AppendBool(network_synchronized);
    time_sync_callback.Run(response.get());
  }

  ScopedPlatformHandle CreateDummyHandle(
      const NamedPlatformHandle& named_handle) {
    return ScopedPlatformHandle(open("/dev/null", O_RDONLY));
  }

  // These are bare pointers, not unique_ptrs, because we need to give them
  // to a SessionManagerImpl instance, but also be able to set expectations
  // on them after we hand them off.
  // Owned by SessionManagerImpl.
  MockInitDaemonController* init_controller_ = nullptr;
  MockPolicyStore* device_policy_store_ = nullptr;
  MockDevicePolicyService* device_policy_service_ = nullptr;
  MockUserPolicyServiceFactory* user_policy_service_factory_ = nullptr;
  map<string, MockPolicyService*> user_policy_services_;
  // The username which is expected to be passed to
  // MockUserPolicyServiceFactory::CreateForHiddenUserHome.
  std::string hidden_user_home_expected_username_;
  // The policy service which shall be returned from
  // MockUserPolicyServiceFactory::CreateForHiddenUserHome.
  std::unique_ptr<MockPolicyService> hidden_user_home_policy_service_;

  scoped_refptr<FakeBus> bus_;
  MockKeyGenerator key_gen_;
  MockServerBackedStateKeyGenerator state_key_generator_;
  MockProcessManagerService manager_;
  MockMetrics metrics_;
  MockNssUtil nss_;
  SystemUtilsImpl real_utils_;
  testing::NiceMock<MockSystemUtils> utils_;
  FakeCrossystem crossystem_;
  MockVpdProcess vpd_process_;
  MockPolicyKey owner_key_;
  FakeContainerManager android_container_;
  FakeTerminaManager termina_manager_;
  MockInstallAttributesReader install_attributes_reader_;
  scoped_refptr<MockObjectProxy> component_updater_proxy_;
  scoped_refptr<MockObjectProxy> system_clock_proxy_;
  dbus::ObjectProxy::WaitForServiceToBeAvailableCallback available_callback_;

  std::unique_ptr<SessionManagerImpl> impl_;
  base::ScopedTempDir tmpdir_;

#if USE_CHEETS
  base::FilePath android_data_dir_;
  base::FilePath android_data_old_dir_;
#endif  // USE_CHEETS

  static const pid_t kDummyPid;
  static const char kNothing[];
  static const char kContainerInstanceId[];
  static const int kAllKeyFlags;

 private:
  void ExpectSessionBoilerplate(const string& account_id_string,
                                bool guest,
                                bool for_owner) {
    EXPECT_CALL(manager_, SetBrowserSessionForUser(
                              StrEq(account_id_string),
                              StrEq(SanitizeUserName(account_id_string))))
        .Times(1);
    // Expect initialization of the device policy service, return success.
    EXPECT_CALL(*device_policy_service_,
                CheckAndHandleOwnerLogin(StrEq(account_id_string), _, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(for_owner), Return(true)));
    // Confirm that the key is present.
    EXPECT_CALL(*device_policy_service_, KeyMissing()).WillOnce(Return(false));

    EXPECT_CALL(metrics_, SendLoginUserType(false, guest, for_owner)).Times(1);
    EXPECT_CALL(
        *init_controller_,
        TriggerImpulseInternal(SessionManagerImpl::kStartUserSessionImpulse,
                               ElementsAre(StartsWith("CHROMEOS_USER=")),
                               InitDaemonController::TriggerMode::ASYNC))
        .WillOnce(Return(nullptr));
    EXPECT_CALL(*exported_object(),
                SendSignal(SignalEq(login_manager::kSessionStateChangedSignal,
                                    SessionManagerImpl::kStarted)))
        .Times(1);
  }

  void ExpectStartSessionUnownedBoilerplate(const string& account_id_string,
                                            bool mitigating,
                                            bool key_gen) {
    CHECK(!(mitigating && key_gen));

    EXPECT_CALL(manager_, SetBrowserSessionForUser(
                              StrEq(account_id_string),
                              StrEq(SanitizeUserName(account_id_string))))
        .Times(1);

    // Expect initialization of the device policy service, return success.
    EXPECT_CALL(*device_policy_service_,
                CheckAndHandleOwnerLogin(StrEq(account_id_string), _, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(false), Return(true)));

    // Indicate that there is no owner key in order to trigger a new one to be
    // generated.
    EXPECT_CALL(*device_policy_service_, KeyMissing()).WillOnce(Return(true));
    EXPECT_CALL(*device_policy_service_, Mitigating())
        .WillRepeatedly(Return(mitigating));
    if (key_gen)
      EXPECT_CALL(key_gen_, Start(StrEq(account_id_string))).Times(1);
    else
      EXPECT_CALL(key_gen_, Start(_)).Times(0);

    EXPECT_CALL(metrics_, SendLoginUserType(false, false, false)).Times(1);
    EXPECT_CALL(
        *init_controller_,
        TriggerImpulseInternal(SessionManagerImpl::kStartUserSessionImpulse,
                               ElementsAre(StartsWith("CHROMEOS_USER=")),
                               InitDaemonController::TriggerMode::ASYNC))
        .WillOnce(Return(nullptr));
    EXPECT_CALL(*exported_object(),
                SendSignal(SignalEq(login_manager::kSessionStateChangedSignal,
                                    SessionManagerImpl::kStarted)))
        .Times(1);
  }

  string fake_salt_ = "fake salt";

  base::MessageLoop loop;

  // Used by fake closures that simulate calling chrome and powerd to lock
  // the screen and restart the device.
  uint32_t actual_locks_ = 0;
  uint32_t expected_locks_ = 0;
  uint32_t actual_restarts_ = 0;
  uint32_t expected_restarts_ = 0;

  DISALLOW_COPY_AND_ASSIGN(SessionManagerImplTest);
};

const pid_t SessionManagerImplTest::kDummyPid = 4;
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
              TriggerImpulseInternal("login-prompt-visible", ElementsAre(),
                                     InitDaemonController::TriggerMode::ASYNC))
      .Times(1);
  impl_->EmitLoginPromptVisible();
}

TEST_F(SessionManagerImplTest, EnableChromeTesting) {
  std::vector<std::string> args = {"--repeat-arg", "--one-time-arg"};

  base::FilePath temp_dir;
  ASSERT_TRUE(base::CreateNewTempDirectory("" /* ignored */, &temp_dir));

  const size_t random_suffix_len = strlen("XXXXXX");
  ASSERT_LT(random_suffix_len, temp_dir.value().size()) << temp_dir.value();

  // Check that RestartBrowserWithArgs() is called with a randomly chosen
  // --testing-channel path name.
  const string expected_testing_path_prefix =
      temp_dir.value().substr(0, temp_dir.value().size() - random_suffix_len);
  EXPECT_CALL(manager_,
              RestartBrowserWithArgs(
                  ElementsAre(args[0], args[1],
                              HasSubstr(expected_testing_path_prefix)),
                  true))
      .Times(1);

  {
    brillo::ErrorPtr error;
    std::string testing_path;
    ASSERT_TRUE(impl_->EnableChromeTesting(&error, false, args, &testing_path));
    EXPECT_FALSE(error.get());
    EXPECT_NE(std::string::npos,
              testing_path.find(expected_testing_path_prefix))
        << testing_path;
  }

  // Calling again, without forcing relaunch, should not do anything.
  {
    brillo::ErrorPtr error;
    std::string testing_path;
    ASSERT_TRUE(impl_->EnableChromeTesting(&error, false, args, &testing_path));
    EXPECT_FALSE(error.get());
    EXPECT_NE(std::string::npos,
              testing_path.find(expected_testing_path_prefix))
        << testing_path;
  }

  // Force relaunch.  Should go through the whole path again.
  args[0] = "--dummy";
  args[1] = "--repeat-arg";
  EXPECT_CALL(manager_,
              RestartBrowserWithArgs(
                  ElementsAre(args[0], args[1],
                              HasSubstr(expected_testing_path_prefix)),
                  true))
      .Times(1);

  {
    brillo::ErrorPtr error;
    std::string testing_path;
    ASSERT_TRUE(impl_->EnableChromeTesting(&error, true, args, &testing_path));
    EXPECT_FALSE(error.get());
    EXPECT_NE(std::string::npos,
              testing_path.find(expected_testing_path_prefix))
        << testing_path;
  }
}

TEST_F(SessionManagerImplTest, StartSession) {
  ExpectStartSession(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
}

TEST_F(SessionManagerImplTest, StartSession_New) {
  ExpectStartSessionUnowned(kSaneEmail);
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

TEST_F(SessionManagerImplTest, StartSession_OwnerAndOther) {
  ExpectStartSessionUnowned(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
  EXPECT_FALSE(error.get());
  VerifyAndClearExpectations();

  constexpr char kEmail2[] = "user2@somewhere";
  ExpectStartSession(kEmail2);
  EXPECT_TRUE(impl_->StartSession(&error, kEmail2, kNothing));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, StartSession_OwnerRace) {
  ExpectStartSessionUnowned(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
  EXPECT_FALSE(error.get());
  VerifyAndClearExpectations();

  constexpr char kEmail2[] = "user2@somewhere";
  ExpectStartSessionOwningInProcess(kEmail2);
  EXPECT_TRUE(impl_->StartSession(&error, kEmail2, kNothing));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, StartSession_BadNssDB) {
  nss_.MakeBadDB();
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->StartSession(&error, kSaneEmail, kNothing));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kNoUserNssDb, error->GetCode());
}

TEST_F(SessionManagerImplTest, StartSession_DevicePolicyFailure) {
  // Upon the owner login check, return an error.

  EXPECT_CALL(*device_policy_service_,
              CheckAndHandleOwnerLogin(StrEq(kSaneEmail), _, _, _))
      .WillOnce(WithArg<3>(Invoke([](brillo::ErrorPtr* error) {
        *error = CreateError(dbus_error::kPubkeySetIllegal, "test");
        return false;
      })));

  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->StartSession(&error, kSaneEmail, kNothing));
  ASSERT_TRUE(error.get());
}

TEST_F(SessionManagerImplTest, StartSession_Owner) {
  ExpectStartOwnerSession(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, StartSession_KeyMitigation) {
  ExpectStartSessionOwnerLost(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
  EXPECT_FALSE(error.get());
}

// Ensure that starting Active Directory session does not create owner key.
TEST_F(SessionManagerImplTest, StartSession_ActiveDirectorManaged) {
  SetDeviceMode("enterprise_ad");
  ExpectStartSessionActiveDirectory(kSaneEmail);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, StopSession) {
  EXPECT_CALL(manager_, ScheduleShutdown()).Times(1);
  impl_->StopSession("");
}

TEST_F(SessionManagerImplTest, StorePolicy_NoSession) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  ExpectStorePolicy(device_policy_service_, policy_blob, kAllKeyFlags,
                    SignatureCheck::kEnabled);
  ResponseCapturer capturer;
  impl_->StorePolicy(capturer.CreateMethodResponse<>(), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicyEx_NoSession) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  ExpectStorePolicy(device_policy_service_, policy_blob, kAllKeyFlags,
                    SignatureCheck::kEnabled);
  ResponseCapturer capturer;
  impl_->StorePolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE, kEmptyAccountId), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicy_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  ExpectStorePolicy(device_policy_service_, policy_blob,
                    PolicyService::KEY_ROTATE, SignatureCheck::kEnabled);

  ResponseCapturer capturer;
  impl_->StorePolicy(capturer.CreateMethodResponse<>(), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicyEx_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  ExpectStorePolicy(device_policy_service_, policy_blob,
                    PolicyService::KEY_ROTATE, SignatureCheck::kEnabled);

  ResponseCapturer capturer;
  impl_->StorePolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE, kEmptyAccountId), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicy_NoSignatureConsumer) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  ExpectNoStorePolicy(device_policy_service_);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicy(capturer.CreateMethodResponse<>(), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicyEx_NoSignatureConsumer) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  ExpectNoStorePolicy(device_policy_service_);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE, kEmptyAccountId), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicy_NoSignatureEnterprise) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  SetDeviceMode("enterprise");
  ExpectNoStorePolicy(device_policy_service_);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicy(capturer.CreateMethodResponse<>(), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicyEx_NoSignatureEnterprise) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  SetDeviceMode("enterprise");
  ExpectNoStorePolicy(device_policy_service_);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE, kEmptyAccountId), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicy_NoSignatureEnterpriseAD) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  SetDeviceMode("enterprise_ad");
  ExpectStorePolicy(device_policy_service_, policy_blob, kAllKeyFlags,
                    SignatureCheck::kDisabled);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicy(capturer.CreateMethodResponse<>(), policy_blob);
}

TEST_F(SessionManagerImplTest, StorePolicyEx_NoSignatureEnterpriseAD) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  SetDeviceMode("enterprise_ad");
  ExpectStorePolicy(device_policy_service_, policy_blob, kAllKeyFlags,
                    SignatureCheck::kDisabled);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE, kEmptyAccountId), policy_blob);
}

TEST_F(SessionManagerImplTest, RetrievePolicy) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*device_policy_service_, Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));
  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RetrievePolicy(&error, &out_blob));
  EXPECT_FALSE(error.get());
  EXPECT_EQ(policy_blob, out_blob);
}

TEST_F(SessionManagerImplTest, RetrievePolicyEx) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*device_policy_service_, Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));
  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RetrievePolicyEx(
      &error, MakePolicyDescriptor(ACCOUNT_TYPE_DEVICE, kEmptyAccountId),
      &out_blob));
  EXPECT_FALSE(error.get());
  EXPECT_EQ(policy_blob, out_blob);
}

TEST_F(SessionManagerImplTest, GetServerBackedStateKeys_TimeSync) {
  EXPECT_CALL(state_key_generator_, RequestStateKeys(_));

  ResponseCapturer capturer;
  impl_->GetServerBackedStateKeys(
      capturer.CreateMethodResponse<std::vector<std::vector<uint8_t>>>());
  ASSERT_NO_FATAL_FAILURE(GotLastSyncInfo(true));
}

TEST_F(SessionManagerImplTest, GetServerBackedStateKeys_NoTimeSync) {
  EXPECT_CALL(state_key_generator_, RequestStateKeys(_)).Times(0);
  ResponseCapturer capturer;
  impl_->GetServerBackedStateKeys(
      capturer.CreateMethodResponse<std::vector<std::vector<uint8_t>>>());
}

TEST_F(SessionManagerImplTest, GetServerBackedStateKeys_TimeSyncDoneBefore) {
  ASSERT_NO_FATAL_FAILURE(GotLastSyncInfo(true));

  EXPECT_CALL(state_key_generator_, RequestStateKeys(_));
  ResponseCapturer capturer;
  impl_->GetServerBackedStateKeys(
      capturer.CreateMethodResponse<std::vector<std::vector<uint8_t>>>());
}

TEST_F(SessionManagerImplTest, GetServerBackedStateKeys_FailedTimeSync) {
  ASSERT_NO_FATAL_FAILURE(GotLastSyncInfo(false));

  EXPECT_CALL(state_key_generator_, RequestStateKeys(_)).Times(0);
  ResponseCapturer capturer;
  impl_->GetServerBackedStateKeys(
      capturer.CreateMethodResponse<std::vector<std::vector<uint8_t>>>());

  EXPECT_CALL(*system_clock_proxy_,
              CallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
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
              CallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(SaveArg<2>(&time_sync_callback));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(Mock::VerifyAndClearExpectations(system_clock_proxy_.get()));
  ASSERT_FALSE(time_sync_callback.is_null());

  EXPECT_CALL(state_key_generator_, RequestStateKeys(_)).Times(1);
  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
  dbus::MessageWriter writer(response.get());
  writer.AppendBool(true);
  time_sync_callback.Run(response.get());
}

TEST_F(SessionManagerImplTest, StoreUserPolicy_NoSession) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");

  ResponseCapturer capturer;
  impl_->StorePolicyForUser(capturer.CreateMethodResponse<>(), kSaneEmail,
                            policy_blob);
  ASSERT_TRUE(capturer.response());
  EXPECT_EQ(dbus_error::kSessionDoesNotExist,
            capturer.response()->GetErrorName());
}

TEST_F(SessionManagerImplTest, StoreUserPolicyEx_NoSession) {
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");

  ResponseCapturer capturer;
  impl_->StorePolicyEx(capturer.CreateMethodResponse<>(),
                       MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail),
                       policy_blob);
  ASSERT_TRUE(capturer.response());
  EXPECT_EQ(dbus_error::kSessionDoesNotExist,
            capturer.response()->GetErrorName());
}

TEST_F(SessionManagerImplTest, StoreUserPolicy_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Store(policy_blob,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW,
                    SignatureCheck::kEnabled, _))
      .WillOnce(Return(true));

  ResponseCapturer capturer;
  impl_->StorePolicyForUser(capturer.CreateMethodResponse<>(), kSaneEmail,
                            policy_blob);
}

TEST_F(SessionManagerImplTest, StoreUserPolicyEx_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Store(policy_blob,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW,
                    SignatureCheck::kEnabled, _))
      .WillOnce(Return(true));

  ResponseCapturer capturer;
  impl_->StorePolicyEx(capturer.CreateMethodResponse<>(),
                       MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail),
                       policy_blob);
}

TEST_F(SessionManagerImplTest, StoreUserPolicy_SecondSession) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(user_policy_services_[kSaneEmail]);

  // Store policy for the signed-in user.
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Store(policy_blob,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW,
                    SignatureCheck::kEnabled, _))
      .WillOnce(Return(true));

  {
    ResponseCapturer capturer;
    impl_->StorePolicyForUser(capturer.CreateMethodResponse<>(), kSaneEmail,
                              policy_blob);
  }
  Mock::VerifyAndClearExpectations(user_policy_services_[kSaneEmail]);

  // Storing policy for another username fails before their session starts.
  constexpr char kEmail2[] = "user2@somewhere.com";
  {
    ResponseCapturer capturer;
    impl_->StorePolicyForUser(capturer.CreateMethodResponse<>(), kEmail2,
                              policy_blob);
    ASSERT_TRUE(capturer.response());
    EXPECT_EQ(dbus_error::kSessionDoesNotExist,
              capturer.response()->GetErrorName());
  }

  // Now start another session for the 2nd user.
  ExpectAndRunStartSession(kEmail2);
  ASSERT_TRUE(user_policy_services_[kEmail2]);

  // Storing policy for that user now succeeds.
  EXPECT_CALL(*user_policy_services_[kEmail2],
              Store(policy_blob,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW,
                    SignatureCheck::kEnabled, _))
      .WillOnce(Return(true));
  {
    ResponseCapturer capturer;
    impl_->StorePolicyForUser(capturer.CreateMethodResponse<>(), kEmail2,
                              policy_blob);
  }
  Mock::VerifyAndClearExpectations(user_policy_services_[kEmail2]);
}

TEST_F(SessionManagerImplTest, StoreUserPolicyEx_SecondSession) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(user_policy_services_[kSaneEmail]);

  // Store policy for the signed-in user.
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Store(policy_blob,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW,
                    SignatureCheck::kEnabled, _))
      .WillOnce(Return(true));

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
    EXPECT_EQ(dbus_error::kSessionDoesNotExist,
              capturer.response()->GetErrorName());
  }

  // Now start another session for the 2nd user.
  ExpectAndRunStartSession(kEmail2);
  ASSERT_TRUE(user_policy_services_[kEmail2]);

  // Storing policy for that user now succeeds.
  EXPECT_CALL(*user_policy_services_[kEmail2],
              Store(policy_blob,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW,
                    SignatureCheck::kEnabled, _))
      .WillOnce(Return(true));
  {
    ResponseCapturer capturer;
    impl_->StorePolicyEx(capturer.CreateMethodResponse<>(),
                         MakePolicyDescriptor(ACCOUNT_TYPE_USER, kEmail2),
                         policy_blob);
  }
  Mock::VerifyAndClearExpectations(user_policy_services_[kEmail2]);
}

TEST_F(SessionManagerImplTest, StoreUserPolicy_NoSignatureConsumer) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Store(_, _, _, _)).Times(0);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicyForUser(capturer.CreateMethodResponse<>(),
                                    kSaneEmail, policy_blob);
}

TEST_F(SessionManagerImplTest, StoreUserPolicyEx_NoSignatureConsumer) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Store(_, _, _, _)).Times(0);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail), policy_blob);
}

TEST_F(SessionManagerImplTest, StoreUserPolicy_NoSignatureEnterprise) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  SetDeviceMode("enterprise");
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Store(_, _, _, _)).Times(0);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicyForUser(capturer.CreateMethodResponse<>(),
                                    kSaneEmail, policy_blob);
}

TEST_F(SessionManagerImplTest, StoreUserPolicyEx_NoSignatureEnterprise) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  SetDeviceMode("enterprise");
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Store(_, _, _, _)).Times(0);

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail), policy_blob);
}

TEST_F(SessionManagerImplTest, StoreUserPolicy_NoSignatureEnterpriseAD) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  SetDeviceMode("enterprise_ad");
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Store(policy_blob,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW,
                    SignatureCheck::kDisabled, _))
      .WillOnce(Return(true));

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicyForUser(capturer.CreateMethodResponse<>(),
                                    kSaneEmail, policy_blob);
}

TEST_F(SessionManagerImplTest, StoreUserPolicyEx_NoSignatureEnterpriseAD) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  SetDeviceMode("enterprise_ad");
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Store(policy_blob,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW,
                    SignatureCheck::kDisabled, _))
      .WillOnce(Return(true));

  ResponseCapturer capturer;
  impl_->StoreUnsignedPolicyEx(
      capturer.CreateMethodResponse<>(),
      MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail), policy_blob);
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicy_NoSession) {
  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->RetrievePolicyForUser(&error, kSaneEmail, &out_blob));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error->GetCode());
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicyEx_NoSession) {
  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->RetrievePolicyEx(
      &error, MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail), &out_blob));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error->GetCode());
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicy_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));

  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RetrievePolicyForUser(&error, kSaneEmail, &out_blob));
  EXPECT_FALSE(error.get());
  EXPECT_EQ(policy_blob, out_blob);
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicyEx_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));

  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RetrievePolicyEx(
      &error, MakePolicyDescriptor(ACCOUNT_TYPE_USER, kSaneEmail), &out_blob));
  EXPECT_FALSE(error.get());
  EXPECT_EQ(policy_blob, out_blob);
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicy_SecondSession) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(user_policy_services_[kSaneEmail]);

  // Retrieve policy for the signed-in user.
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));
  {
    std::vector<uint8_t> out_blob;
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->RetrievePolicyForUser(&error, kSaneEmail, &out_blob));
    EXPECT_FALSE(error.get());
    Mock::VerifyAndClearExpectations(user_policy_services_[kSaneEmail]);
    EXPECT_EQ(policy_blob, out_blob);
  }

  // Retrieving policy for another username fails before their session starts.
  constexpr char kEmail2[] = "user2@somewhere.com";
  {
    std::vector<uint8_t> out_blob;
    brillo::ErrorPtr error;
    EXPECT_FALSE(impl_->RetrievePolicyForUser(&error, kEmail2, &out_blob));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kSessionDoesNotExist, error->GetCode());
  }

  // Now start another session for the 2nd user.
  ExpectAndRunStartSession(kEmail2);
  ASSERT_TRUE(user_policy_services_[kEmail2]);

  // Retrieving policy for that user now succeeds.
  EXPECT_CALL(*user_policy_services_[kEmail2], Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));
  {
    std::vector<uint8_t> out_blob;
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->RetrievePolicyForUser(&error, kEmail2, &out_blob));
    EXPECT_FALSE(error.get());
    Mock::VerifyAndClearExpectations(user_policy_services_[kEmail2]);
    EXPECT_EQ(policy_blob, out_blob);
  }
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicyEx_SecondSession) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(user_policy_services_[kSaneEmail]);

  // Retrieve policy for the signed-in user.
  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));
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
    EXPECT_EQ(dbus_error::kSessionDoesNotExist, error->GetCode());
  }

  // Now start another session for the 2nd user.
  ExpectAndRunStartSession(kEmail2);
  ASSERT_TRUE(user_policy_services_[kEmail2]);

  // Retrieving policy for that user now succeeds.
  EXPECT_CALL(*user_policy_services_[kEmail2], Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));
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

TEST_F(SessionManagerImplTest, RetrieveUserPolicyWithoutSession) {
  ASSERT_FALSE(user_policy_services_.count(kSaneEmail));

  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");

  // Set up what MockUserPolicyServiceFactory will return.
  hidden_user_home_expected_username_ = kSaneEmail;
  hidden_user_home_policy_service_ = std::make_unique<MockPolicyService>();
  MockPolicyService* policy_service = hidden_user_home_policy_service_.get();

  EXPECT_CALL(*policy_service, Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));

  // Retrieve policy for a user who does not have a session.
  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RetrievePolicyForUserWithoutSession(&error, kSaneEmail,
                                                         &out_blob));
  Mock::VerifyAndClearExpectations(policy_service);
  EXPECT_FALSE(error.get());
  EXPECT_EQ(policy_blob, out_blob);
  // Retrieval of policy without user session should not create a persistent
  // PolicyService.
  ASSERT_FALSE(user_policy_services_.count(kSaneEmail));
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicyExWithoutSession) {
  ASSERT_FALSE(user_policy_services_.count(kSaneEmail));

  const std::vector<uint8_t> policy_blob = StringToBlob("fake policy");

  // Set up what MockUserPolicyServiceFactory will return.
  hidden_user_home_expected_username_ = kSaneEmail;
  hidden_user_home_policy_service_ = std::make_unique<MockPolicyService>();
  MockPolicyService* policy_service = hidden_user_home_policy_service_.get();

  EXPECT_CALL(*policy_service, Retrieve(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_blob), Return(true)));

  // Retrieve policy for a user who does not have a session.
  std::vector<uint8_t> out_blob;
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RetrievePolicyEx(
      &error, MakePolicyDescriptor(ACCOUNT_TYPE_SESSIONLESS_USER, kSaneEmail),
      &out_blob));
  Mock::VerifyAndClearExpectations(policy_service);
  EXPECT_FALSE(error.get());
  EXPECT_EQ(policy_blob, out_blob);
  // Retrieval of policy without user session should not create a persistent
  // PolicyService.
  ASSERT_FALSE(user_policy_services_.count(kSaneEmail));
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
    EXPECT_EQ(active_users[kSaneEmail], SanitizeUserName(kSaneEmail));
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
    EXPECT_EQ(active_users[kSaneEmail], SanitizeUserName(kSaneEmail));
    EXPECT_EQ(active_users[kEmail2], SanitizeUserName(kEmail2));
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
  EXPECT_FALSE(impl_->RestartJob(&error, dbus::FileDescriptor(), {}));
  ASSERT_TRUE(error.get());
  EXPECT_EQ("GetPeerCredsFailed", error->GetCode());
}

TEST_F(SessionManagerImplTest, RestartJobBadPid) {
  int sockets[2] = {-1, -1};
  ASSERT_GE(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  base::ScopedFD fd0_closer(sockets[0]);
  dbus::FileDescriptor fd1;
  fd1.PutValue(sockets[1]);
  fd1.CheckValidity();

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(false));
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->RestartJob(&error, fd1, {}));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kUnknownPid, error->GetCode());
}

TEST_F(SessionManagerImplTest, RestartJobSuccess) {
  int sockets[2] = {-1, -1};
  ASSERT_GE(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  base::ScopedFD fd0_closer(sockets[0]);
  dbus::FileDescriptor fd1;
  fd1.PutValue(sockets[1]);
  fd1.CheckValidity();

  const std::vector<std::string> argv = {
      "program",
      "--switch1",
      "--switch2=switch2_value",
      "--switch3=escaped_\"_quote",
      "--switch4=white space",
      "arg1",
      "arg 2",
  };

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, RestartBrowserWithArgs(ElementsAreArray(argv), false))
      .Times(1);
  ExpectGuestSession();

  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RestartJob(&error, fd1, argv));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, SupervisedUserCreation) {
  impl_->HandleSupervisedUserCreationStarting();
  EXPECT_TRUE(impl_->ShouldEndSession());
  impl_->HandleSupervisedUserCreationFinished();
  EXPECT_FALSE(impl_->ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockScreen) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->LockScreen(&error));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(impl_->ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockScreen_DuringSupervisedUserCreation) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  EXPECT_CALL(*exported_object(), SendSignal(_)).Times(AnyNumber());

  impl_->HandleSupervisedUserCreationStarting();
  EXPECT_TRUE(impl_->ShouldEndSession());
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->LockScreen(&error));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(impl_->ShouldEndSession());
  impl_->HandleLockScreenShown();
  EXPECT_TRUE(impl_->ShouldEndSession());
  impl_->HandleLockScreenDismissed();
  EXPECT_TRUE(impl_->ShouldEndSession());
  impl_->HandleSupervisedUserCreationFinished();
  EXPECT_FALSE(impl_->ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockScreen_InterleavedSupervisedUserCreation) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  EXPECT_CALL(*exported_object(), SendSignal(_)).Times(AnyNumber());

  impl_->HandleSupervisedUserCreationStarting();
  EXPECT_TRUE(impl_->ShouldEndSession());
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->LockScreen(&error));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(impl_->ShouldEndSession());
  impl_->HandleLockScreenShown();
  EXPECT_TRUE(impl_->ShouldEndSession());
  impl_->HandleSupervisedUserCreationFinished();
  EXPECT_TRUE(impl_->ShouldEndSession());
  impl_->HandleLockScreenDismissed();
  EXPECT_FALSE(impl_->ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockScreen_MultiSession) {
  ExpectAndRunStartSession("user@somewhere");
  ExpectAndRunStartSession("user2@somewhere");
  ExpectLockScreen();
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->LockScreen(&error));
  EXPECT_FALSE(error.get());
  EXPECT_EQ(TRUE, impl_->ShouldEndSession());
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
  EXPECT_EQ(TRUE, impl_->ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockUnlockScreen) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->LockScreen(&error));
  EXPECT_FALSE(error.get());
  EXPECT_EQ(TRUE, impl_->ShouldEndSession());

  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kScreenIsLockedSignal)))
      .Times(1);
  impl_->HandleLockScreenShown();
  EXPECT_EQ(TRUE, impl_->ShouldEndSession());

  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kScreenIsUnlockedSignal)))
      .Times(1);
  impl_->HandleLockScreenDismissed();
  EXPECT_EQ(FALSE, impl_->ShouldEndSession());
}

TEST_F(SessionManagerImplTest, StartDeviceWipe) {
  // Just make sure the device is being restart as sanity check of
  // InitiateDeviceWipe() invocation.
  ExpectDeviceRestart();

  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartDeviceWipe(&error));
  EXPECT_FALSE(error.get());
}

TEST_F(SessionManagerImplTest, StartDeviceWipe_AlreadyLoggedIn) {
  base::FilePath logged_in_path(SessionManagerImpl::kLoggedInFlag);
  ASSERT_FALSE(utils_.Exists(logged_in_path));
  ASSERT_TRUE(utils_.AtomicFileWrite(logged_in_path, "1"));
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->StartDeviceWipe(&error));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSessionExists, error->GetCode());
}

TEST_F(SessionManagerImplTest, InitiateDeviceWipe_TooLongReason) {
  ASSERT_TRUE(
      utils_.RemoveFile(base::FilePath(SessionManagerImpl::kLoggedInFlag)));
  ExpectDeviceRestart();
  impl_->InitiateDeviceWipe(
      "overly long test message with\nspecial/chars$\t\xa4\xd6 1234567890");
  std::string contents;
  base::FilePath reset_path = real_utils_.PutInsideBaseDirForTesting(
      base::FilePath(SessionManagerImpl::kResetFile));
  ASSERT_TRUE(base::ReadFileToString(reset_path, &contents));
  ASSERT_EQ(
      "fast safe keepimg reason="
      "overly_long_test_message_with_special_chars_____12",
      contents);
}

TEST_F(SessionManagerImplTest, ImportValidateAndStoreGeneratedKey) {
  base::FilePath key_file_path;
  string key("key_contents");
  ASSERT_TRUE(base::CreateTemporaryFileInDir(tmpdir_.path(), &key_file_path));
  ASSERT_EQ(base::WriteFile(key_file_path, key.c_str(), key.size()),
            key.size());

  // Start a session, to set up NSSDB for the user.
  ExpectStartOwnerSession(kSaneEmail);
  brillo::ErrorPtr error;
  ASSERT_TRUE(impl_->StartSession(&error, kSaneEmail, kNothing));
  EXPECT_FALSE(error.get());

  EXPECT_CALL(*device_policy_service_,
              ValidateAndStoreOwnerKey(StrEq(kSaneEmail), StringToBlob(key),
                                       nss_.GetSlot()))
      .WillOnce(Return(true));

  impl_->OnKeyGenerated(kSaneEmail, key_file_path);
  EXPECT_FALSE(base::PathExists(key_file_path));
}

TEST_F(SessionManagerImplTest, ContainerValidChars) {
  const std::string kContainerName = "testc";
  const std::string kInvalidContainerName = "test/c";
  const std::string kContainerPath = "test_c+-.ext4";
  const std::string kInvalidContainerPath = "testc*.ext4";
  const std::string kParentContainerPath = "../testc.ext4";
  const std::string kHashedUserName = "";

  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->StartContainer(&error, kContainerPath, kContainerName,
                                    kHashedUserName, false));
  EXPECT_FALSE(impl_->StartContainer(
      &error, kContainerPath, kInvalidContainerName, kHashedUserName, false));
  EXPECT_FALSE(impl_->StartContainer(&error, kInvalidContainerPath,
                                     kContainerName, kHashedUserName, false));
  EXPECT_FALSE(impl_->StartContainer(&error, kParentContainerPath,
                                     kContainerName, kHashedUserName, false));
}

#if USE_CHEETS

// Android master container doesn't support launching for login screen.
#if !USE_ANDROID_MASTER_CONTAINER
TEST_F(SessionManagerImplTest, ArcInstanceStart_ForLoginScreen) {
  {
    int64_t start_time = 0;
    brillo::ErrorPtr error;
    EXPECT_FALSE(impl_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcInstanceForLoginScreenImpulse,
                  ElementsAre("CHROMEOS_DEV_MODE=0", "CHROMEOS_INSIDE_VM=0",
                              "NATIVE_BRIDGE_EXPERIMENT=0"),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));

  brillo::ErrorPtr error;
  StartArcInstanceRequest request;
  request.set_for_login_screen(true);
  // When starting an instance for the login screen, CreateServerHandle() should
  // never be called.
  EXPECT_CALL(utils_, CreateServerHandle(_)).Times(0);
  std::string container_instance_id;
  dbus::FileDescriptor server_socket_fd;
  EXPECT_TRUE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                      &container_instance_id,
                                      &server_socket_fd));
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(container_instance_id.empty());
  EXPECT_TRUE(server_socket_fd.is_valid());  // a dummy fd is set.
  EXPECT_TRUE(android_container_.running());

  // StartArcInstance() does not update start time for login screen.
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_FALSE(impl_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStopArcInstanceImpulse, ElementsAre(),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  // StartArcInstance does not emit kStartArcNetworkImpulse for login screen.
  // Its OnStop closure does emit kStartArcNetworkStopImpulse but Upstart will
  // ignore it.
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(SessionManagerImpl::kStopArcNetworkImpulse,
                                     ElementsAre(),
                                     InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kArcInstanceStopped, true,
                                  container_instance_id)))
      .Times(1);
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StopArcInstance(&error));
    EXPECT_FALSE(error.get());
  }

  EXPECT_FALSE(android_container_.running());
}
#endif  // !USE_ANDROID_MASTER_CONTAINER

TEST_F(SessionManagerImplTest, ArcInstanceStart_ForUser) {
  ExpectAndRunStartSession(kSaneEmail);
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_FALSE(impl_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcInstanceImpulse,
                  ElementsAre("CHROMEOS_DEV_MODE=0", "CHROMEOS_INSIDE_VM=0",
                              "NATIVE_BRIDGE_EXPERIMENT=0",
                              StartsWith("ANDROID_DATA_DIR="),
                              StartsWith("ANDROID_DATA_OLD_DIR="),
                              std::string("CHROMEOS_USER=") + kSaneEmail,
                              "DISABLE_BOOT_COMPLETED_BROADCAST=0",
                              "ENABLE_VENDOR_PRIVILEGED=1"),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStopArcInstanceImpulse, ElementsAre(),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcNetworkImpulse,
                  ElementsAre(std::string("CONTAINER_NAME=") +
                                  SessionManagerImpl::kArcContainerName,
                              "CONTAINER_PID=" + std::to_string(kAndroidPid)),
                  InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(SessionManagerImpl::kStopArcNetworkImpulse,
                                     ElementsAre(),
                                     InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));

  brillo::ErrorPtr error;
  StartArcInstanceRequest request = CreateStartArcInstanceRequestForUser();
  request.set_scan_vendor_priv_app(true);
  ExpectStartArcInstance();
  std::string container_instance_id;
  dbus::FileDescriptor server_socket_fd;
  EXPECT_TRUE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                      &container_instance_id,
                                      &server_socket_fd));
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(container_instance_id.empty());
  EXPECT_TRUE(server_socket_fd.is_valid());
  EXPECT_TRUE(android_container_.running());
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_TRUE(impl_->GetArcStartTimeTicks(&error, &start_time));
    EXPECT_NE(0, start_time);
    ASSERT_FALSE(error.get());
  }
  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kArcInstanceStopped, true,
                                  container_instance_id)))
      .Times(1);

  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StopArcInstance(&error));
    EXPECT_FALSE(error.get());
  }
  EXPECT_FALSE(android_container_.running());
}

// Android master container doesn't support launching in login screen.
#if !USE_ANDROID_MASTER_CONTAINER
TEST_F(SessionManagerImplTest, ArcInstanceStart_ContinueBooting) {
  ExpectAndRunStartSession(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcInstanceForLoginScreenImpulse,
                  ElementsAre("CHROMEOS_DEV_MODE=0", "CHROMEOS_INSIDE_VM=0",
                              "NATIVE_BRIDGE_EXPERIMENT=0"),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));

  brillo::ErrorPtr error;
  StartArcInstanceRequest request;
  request.set_for_login_screen(true);
  EXPECT_CALL(utils_, CreateServerHandle(_)).Times(0);
  std::string container_instance_id;
  dbus::FileDescriptor server_socket_fd;
  EXPECT_TRUE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                      &container_instance_id,
                                      &server_socket_fd));
  EXPECT_FALSE(container_instance_id.empty());
  EXPECT_TRUE(server_socket_fd.is_valid());

  // Then, upgrade it to a fully functional one.
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_FALSE(impl_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kContinueArcBootImpulse,
                  ElementsAre("CHROMEOS_DEV_MODE=0", "CHROMEOS_INSIDE_VM=0",
                              "NATIVE_BRIDGE_EXPERIMENT=0",
                              StartsWith("ANDROID_DATA_DIR="),
                              StartsWith("ANDROID_DATA_OLD_DIR="),
                              std::string("CHROMEOS_USER=") + kSaneEmail,
                              "DISABLE_BOOT_COMPLETED_BROADCAST=0",
                              "ENABLE_VENDOR_PRIVILEGED=1",
                              // The upgrade signal has a PID.
                              "CONTAINER_PID=" + std::to_string(kAndroidPid)),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStopArcInstanceImpulse, ElementsAre(),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcNetworkImpulse,
                  ElementsAre(std::string("CONTAINER_NAME=") +
                                  SessionManagerImpl::kArcContainerName,
                              "CONTAINER_PID=" + std::to_string(kAndroidPid)),
                  InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(SessionManagerImpl::kStopArcNetworkImpulse,
                                     ElementsAre(),
                                     InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));

  request = CreateStartArcInstanceRequestForUser();
  request.set_scan_vendor_priv_app(true);
  ExpectStartArcInstance();
  std::string container_instance_id_for_upgrade = "not-empty";
  dbus::FileDescriptor server_socket_fd_for_upgrade;
  EXPECT_TRUE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                      &container_instance_id_for_upgrade,
                                      &server_socket_fd_for_upgrade));
  EXPECT_FALSE(error.get());
  // Unlike the regular start, an empty ID is returned.
  EXPECT_TRUE(container_instance_id_for_upgrade.empty());
  EXPECT_TRUE(server_socket_fd_for_upgrade.is_valid());
  EXPECT_TRUE(android_container_.running());
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_TRUE(impl_->GetArcStartTimeTicks(&error, &start_time));
    EXPECT_NE(0, start_time);
    ASSERT_FALSE(error.get());
  }
  // The ID for the container for login screen is passed to the dbus call.
  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kArcInstanceStopped, true,
                                  container_instance_id)))
      .Times(1);

  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StopArcInstance(&error));
    EXPECT_FALSE(error.get());
  }
  EXPECT_FALSE(android_container_.running());
}

TEST_F(SessionManagerImplTest, ArcInstanceStart_NativeBridgeExperiment) {
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcInstanceForLoginScreenImpulse,
                  ElementsAre("CHROMEOS_DEV_MODE=0", "CHROMEOS_INSIDE_VM=0",
                              "NATIVE_BRIDGE_EXPERIMENT=1"),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));

  brillo::ErrorPtr error;
  StartArcInstanceRequest request;
  // Use for login screen mode for minimalistic test.
  request.set_for_login_screen(true);
  request.set_native_bridge_experiment(true);
  std::string container_instance_id;
  dbus::FileDescriptor server_socket_fd;
  EXPECT_TRUE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                      &container_instance_id,
                                      &server_socket_fd));
  EXPECT_FALSE(error.get());
}
#endif  // !USE_ANDROID_MASTER_CONTAINER

TEST_F(SessionManagerImplTest, ArcInstanceStart_NoSession) {
  brillo::ErrorPtr error;
  StartArcInstanceRequest request = CreateStartArcInstanceRequestForUser();
  ExpectStartArcInstance();
  std::string container_instance_id;
  dbus::FileDescriptor server_socket_fd;
  EXPECT_FALSE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                       &container_instance_id,
                                       &server_socket_fd));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error->GetCode());
  EXPECT_TRUE(container_instance_id.empty());
  EXPECT_FALSE(server_socket_fd.is_valid());
}

TEST_F(SessionManagerImplTest, ArcInstanceStart_LowDisk) {
  ExpectAndRunStartSession(kSaneEmail);

  // Emulate no free disk space.
  ON_CALL(utils_, AmountOfFreeDiskSpace(_)).WillByDefault(Return(0));

  brillo::ErrorPtr error;
  StartArcInstanceRequest request = CreateStartArcInstanceRequestForUser();
  ExpectStartArcInstance();
  std::string container_instance_id;
  dbus::FileDescriptor server_socket_fd;
  EXPECT_FALSE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                       &container_instance_id,
                                       &server_socket_fd));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kLowFreeDisk, error->GetCode());
  EXPECT_TRUE(container_instance_id.empty());
  EXPECT_FALSE(server_socket_fd.is_valid());
}

TEST_F(SessionManagerImplTest, ArcStartInstance_ArcSetupFailure) {
  ExpectAndRunStartSession(kSaneEmail);

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcInstanceImpulse,
                  ElementsAre("CHROMEOS_DEV_MODE=0", "CHROMEOS_INSIDE_VM=0",
                              "NATIVE_BRIDGE_EXPERIMENT=0",
                              StartsWith("ANDROID_DATA_DIR="),
                              StartsWith("ANDROID_DATA_OLD_DIR="),
                              std::string("CHROMEOS_USER=") + kSaneEmail,
                              "DISABLE_BOOT_COMPLETED_BROADCAST=0",
                              "ENABLE_VENDOR_PRIVILEGED=0"),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(nullptr));
  // After a failure, the StopArcInstance impulse must be sent to clean up the
  // system's state.
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStopArcInstanceImpulse, ElementsAre(),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));

  brillo::ErrorPtr error;
  StartArcInstanceRequest request = CreateStartArcInstanceRequestForUser();
  ExpectStartArcInstance();
  std::string container_instance_id;
  dbus::FileDescriptor server_socket_fd;
  EXPECT_FALSE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                       &container_instance_id,
                                       &server_socket_fd));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kEmitFailed, error->GetCode());
  EXPECT_TRUE(container_instance_id.empty());
  EXPECT_FALSE(server_socket_fd.is_valid());
}

TEST_F(SessionManagerImplTest, ArcInstanceCrash) {
  ExpectAndRunStartSession(kSaneEmail);

  // Overrides dev mode state.
  ON_CALL(utils_, GetDevModeState())
      .WillByDefault(Return(DevModeState::DEV_MODE_ON));

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcInstanceImpulse,
                  ElementsAre("CHROMEOS_DEV_MODE=1", "CHROMEOS_INSIDE_VM=0",
                              "NATIVE_BRIDGE_EXPERIMENT=0",
                              StartsWith("ANDROID_DATA_DIR="),
                              StartsWith("ANDROID_DATA_OLD_DIR="),
                              std::string("CHROMEOS_USER=") + kSaneEmail,
                              "DISABLE_BOOT_COMPLETED_BROADCAST=0",
                              "ENABLE_VENDOR_PRIVILEGED=0"),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStopArcInstanceImpulse, ElementsAre(),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcNetworkImpulse,
                  ElementsAre(std::string("CONTAINER_NAME=") +
                                  SessionManagerImpl::kArcContainerName,
                              "CONTAINER_PID=" + std::to_string(kAndroidPid)),
                  InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(SessionManagerImpl::kStopArcNetworkImpulse,
                                     ElementsAre(),
                                     InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));

  std::string container_instance_id;
  {
    brillo::ErrorPtr error;
    StartArcInstanceRequest request = CreateStartArcInstanceRequestForUser();
    ExpectStartArcInstance();
    dbus::FileDescriptor server_socket_fd;
    EXPECT_TRUE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                        &container_instance_id,
                                        &server_socket_fd));
    EXPECT_FALSE(error.get());
    EXPECT_FALSE(container_instance_id.empty());
    EXPECT_TRUE(server_socket_fd.is_valid());
  }
  EXPECT_TRUE(android_container_.running());

  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kArcInstanceStopped, false,
                                  container_instance_id)))
      .Times(1);

  android_container_.SimulateCrash();
  EXPECT_FALSE(android_container_.running());

  // This should now fail since the container was cleaned up already.
  {
    brillo::ErrorPtr error;
    EXPECT_FALSE(impl_->StopArcInstance(&error));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kContainerShutdownFail, error->GetCode());
  }
}

#else  // !USE_CHEETS

TEST_F(SessionManagerImplTest, ArcStartInstance_Fail) {
  ExpectAndRunStartSession(kSaneEmail);

  brillo::ErrorPtr error;
  StartArcInstanceRequest request = CreateStartArcInstanceRequestForUser();
  std::string container_instance_id;
  dbus::FileDescriptor server_socket_fd;
  EXPECT_CALL(utils_, CreateServerHandle(_)).Times(0);
  EXPECT_FALSE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                       &container_instance_id,
                                       &server_socket_fd));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kNotAvailable, error->GetCode());
  EXPECT_TRUE(container_instance_id.empty());
  EXPECT_FALSE(server_socket_fd.is_valid());
}
#endif

#if USE_CHEETS
TEST_F(SessionManagerImplTest, ArcRemoveData) {
  // Test that RemoveArcData() removes |android_data_dir_| and reports success
  // even if the directory is not empty.
  ASSERT_TRUE(utils_.CreateDir(android_data_dir_));
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_FALSE(utils_.Exists(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RemoveArcData(&error, kSaneEmail));
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveData_NoSourceDirectory) {
  // Test that RemoveArcData() reports success when the directory does not
  // exist.
  ASSERT_FALSE(utils_.Exists(android_data_dir_));
  ASSERT_FALSE(utils_.Exists(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_MISSING,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RemoveArcData(&error, kSaneEmail));
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveData_OldDirectoryExists) {
  // Test that RemoveArcData() can remove |android_data_dir_| and
  // reports success even if the "old" directory already exists.
  ASSERT_TRUE(utils_.CreateDir(android_data_dir_));
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_TRUE(utils_.CreateDir(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RemoveArcData(&error, kSaneEmail));
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveData_NonEmptyOldDirectoryExists) {
  // Test that RemoveArcData() can remove |android_data_dir_| and
  // reports success even if the "old" directory already exists and is not
  // empty.
  ASSERT_TRUE(utils_.CreateDir(android_data_dir_));
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_TRUE(utils_.CreateDir(android_data_old_dir_));
  ASSERT_TRUE(
      utils_.AtomicFileWrite(android_data_old_dir_.Append("bar"), "test2"));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_NOT_EMPTY);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RemoveArcData(&error, kSaneEmail));
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest,
       ArcRemoveData_NoSourceDirectoryButOldDirectoryExists) {
  // Test that RemoveArcData() removes the "old" directory and reports success
  // even when |android_data_dir_| does not exist at all.
  ASSERT_FALSE(utils_.Exists(android_data_dir_));
  ASSERT_TRUE(utils_.CreateDir(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_MISSING,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RemoveArcData(&error, kSaneEmail));
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest,
       ArcRemoveData_NoSourceDirectoryButNonEmptyOldDirectoryExists) {
  // Test that RemoveArcData() removes the "old" directory and returns
  // true even when |android_data_dir_| does not exist at all.
  ASSERT_FALSE(utils_.Exists(android_data_dir_));
  ASSERT_TRUE(utils_.CreateDir(android_data_old_dir_));
  ASSERT_TRUE(
      utils_.AtomicFileWrite(android_data_old_dir_.Append("foo"), "test"));
  ExpectRemoveArcData(DataDirType::DATA_DIR_MISSING,
                      OldDataDirType::OLD_DATA_DIR_NOT_EMPTY);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RemoveArcData(&error, kSaneEmail));
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveData_OldFileExists) {
  // Test that RemoveArcData() can remove |android_data_dir_| and
  // returns true even if the "old" path exists as a file. This should never
  // happen, but RemoveArcData() can handle the case.
  ASSERT_TRUE(utils_.CreateDir(android_data_dir_));
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_old_dir_, "test2"));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_FILE_EXISTS);
  brillo::ErrorPtr error;
  EXPECT_TRUE(impl_->RemoveArcData(&error, kSaneEmail));
  EXPECT_FALSE(error.get());
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveData_ArcRunning) {
  // Test that RemoveArcData does nothing when ARC is running.
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(utils_.CreateDir(android_data_dir_));
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_FALSE(utils_.Exists(android_data_old_dir_));

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcInstanceImpulse,
                  ElementsAre("CHROMEOS_DEV_MODE=0", "CHROMEOS_INSIDE_VM=0",
                              "NATIVE_BRIDGE_EXPERIMENT=0",
                              StartsWith("ANDROID_DATA_DIR="),
                              StartsWith("ANDROID_DATA_OLD_DIR="),
                              std::string("CHROMEOS_USER=") + kSaneEmail,
                              "DISABLE_BOOT_COMPLETED_BROADCAST=0",
                              "ENABLE_VENDOR_PRIVILEGED=0"),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcNetworkImpulse,
                  ElementsAre(std::string("CONTAINER_NAME=") +
                                  SessionManagerImpl::kArcContainerName,
                              "CONTAINER_PID=" + std::to_string(kAndroidPid)),
                  InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(nullptr));
  {
    brillo::ErrorPtr error;
    StartArcInstanceRequest request = CreateStartArcInstanceRequestForUser();
    ExpectStartArcInstance();
    std::string container_instance_id;
    dbus::FileDescriptor server_socket_fd;
    EXPECT_TRUE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                        &container_instance_id,
                                        &server_socket_fd));
    EXPECT_FALSE(error.get());
    EXPECT_FALSE(container_instance_id.empty());
    EXPECT_TRUE(server_socket_fd.is_valid());
  }
  {
    brillo::ErrorPtr error;
    EXPECT_FALSE(impl_->RemoveArcData(&error, kSaneEmail));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kArcInstanceRunning, error->GetCode());
    EXPECT_TRUE(utils_.Exists(android_data_dir_));
  }
}

TEST_F(SessionManagerImplTest, ArcRemoveData_ArcStopped) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(utils_.CreateDir(android_data_dir_));
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_TRUE(utils_.CreateDir(android_data_old_dir_));
  ASSERT_TRUE(
      utils_.AtomicFileWrite(android_data_old_dir_.Append("bar"), "test2"));

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcInstanceImpulse,
                  ElementsAre("CHROMEOS_DEV_MODE=0", "CHROMEOS_INSIDE_VM=0",
                              "NATIVE_BRIDGE_EXPERIMENT=0",
                              StartsWith("ANDROID_DATA_DIR="),
                              StartsWith("ANDROID_DATA_OLD_DIR="),
                              std::string("CHROMEOS_USER=") + kSaneEmail,
                              "DISABLE_BOOT_COMPLETED_BROADCAST=0",
                              "ENABLE_VENDOR_PRIVILEGED=0"),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStartArcNetworkImpulse,
                  ElementsAre(std::string("CONTAINER_NAME=") +
                                  SessionManagerImpl::kArcContainerName,
                              "CONTAINER_PID=" + std::to_string(kAndroidPid)),
                  InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(nullptr));

  std::string container_instance_id;
  {
    brillo::ErrorPtr error;
    StartArcInstanceRequest request = CreateStartArcInstanceRequestForUser();
    ExpectStartArcInstance();
    dbus::FileDescriptor server_socket_fd;
    EXPECT_TRUE(impl_->StartArcInstance(&error, SerializeAsBlob(request),
                                        &container_instance_id,
                                        &server_socket_fd));
    EXPECT_FALSE(error.get());
    EXPECT_FALSE(container_instance_id.empty());
    EXPECT_TRUE(server_socket_fd.is_valid());
  }

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(
                  SessionManagerImpl::kStopArcInstanceImpulse, ElementsAre(),
                  InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(SessionManagerImpl::kStopArcNetworkImpulse,
                                     ElementsAre(),
                                     InitDaemonController::TriggerMode::SYNC))
      .WillOnce(WithoutArgs(Invoke(CreateEmptyResponse)));
  EXPECT_CALL(*exported_object(),
              SendSignal(SignalEq(login_manager::kArcInstanceStopped, true,
                                  container_instance_id)))
      .Times(1);
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->StopArcInstance(&error));
    EXPECT_FALSE(error.get());
  }

  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_NOT_EMPTY);
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->RemoveArcData(&error, kSaneEmail));
    EXPECT_FALSE(error.get());
  }
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}
#else
// When USE_CHEETS is not defined, ArcRemoveData should immediately return
// dbus_error::kNotAvailable.
TEST_F(SessionManagerImplTest, ArcRemoveData) {
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->RemoveArcData(&error, kSaneEmail));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kNotAvailable, error->GetCode());
}
#endif

TEST_F(SessionManagerImplTest, SetArcCpuRestrictionFails) {
#if USE_CHEETS
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->SetArcCpuRestriction(
      &error, static_cast<uint32_t>(NUM_CONTAINER_CPU_RESTRICTION_STATES)));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kArcCpuCgroupFail, error->GetCode());
#else
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->SetArcCpuRestriction(
      &error, static_cast<uint32_t>(CONTAINER_CPU_RESTRICTION_BACKGROUND)));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kNotAvailable, error->GetCode());
#endif
}

TEST_F(SessionManagerImplTest, EmitArcBooted) {
#if USE_CHEETS
  EXPECT_CALL(
      *init_controller_,
      TriggerImpulseInternal(SessionManagerImpl::kArcBootedImpulse,
                             ElementsAre(StartsWith("ANDROID_DATA_OLD_DIR=")),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(nullptr));
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->EmitArcBooted(&error, kSaneEmail));
    EXPECT_FALSE(error.get());
  }

  EXPECT_CALL(*init_controller_,
              TriggerImpulseInternal(SessionManagerImpl::kArcBootedImpulse,
                                     ElementsAre(),
                                     InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(nullptr));
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(impl_->EmitArcBooted(&error, std::string()));
    EXPECT_FALSE(error.get());
  }
#else
  brillo::ErrorPtr error;
  EXPECT_FALSE(impl_->EmitArcBooted(&error, kSaneEmail));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kNotAvailable, error->GetCode());
#endif
}

class StartTPMFirmwareUpdateTest : public SessionManagerImplTest {
 public:
  void SetUp() override {
    SessionManagerImplTest::SetUp();

    ON_CALL(utils_, Exists(_))
        .WillByDefault(Invoke(this, &StartTPMFirmwareUpdateTest::FileExists));
    ON_CALL(utils_, GetAppOutput(_, _))
        .WillByDefault(Invoke(this, &StartTPMFirmwareUpdateTest::GetAppOutput));
    ON_CALL(*device_policy_service_, InstallAttributesEnterpriseMode())
        .WillByDefault(Return(false));
    ON_CALL(vpd_process_, RunInBackground(_, _, _))
        .WillByDefault(
            Invoke(this, &StartTPMFirmwareUpdateTest::RunVpdProcess));
    ON_CALL(*device_policy_store_, Get()).WillByDefault(ReturnRef(policy_));

    SetFileExists(SessionManagerImpl::kTPMFirmwareUpdateAvailableFile, true);
  }

  void TearDown() override {
    ResponseCapturer capturer;
    impl_->StartTPMFirmwareUpdate(capturer.CreateMethodResponse<>(),
                                  update_mode_);
    if (!completion_.is_null()) {
      completion_.Run(vpd_status_);
    }

    EXPECT_EQ(expected_error_, capturer.response()->GetErrorName());
    SessionManagerImplTest::TearDown();
  }

  void SetFileExists(const std::string& path, bool exists) {
    file_existence_[path] = exists;
  }

  bool FileExists(const base::FilePath& path) {
    const auto entry = file_existence_.find(path.MaybeAsASCII());
    return entry != file_existence_.end() && entry->second;
  }

  void ExpectError(const std::string& error) { expected_error_ = error; }

  void SetUpdateMode(const std::string& mode) { update_mode_ = mode; }

  void SetExistingVPDParams(const std::string& params) {
    existing_vpd_params_ = params;
  }

  void SetExpectedVPDParams(const std::string& params) {
    expected_vpd_params_ = params;
  }

  bool GetAppOutput(const std::vector<std::string>& argv, std::string* output) {
    if (argv.size() != 2) {
      return false;
    }
    if (argv[1] == SessionManagerImpl::kTPMFirmwareUpdateParamsVPDKey) {
      *output = existing_vpd_params_;
    }
    return true;
  }

  bool RunVpdProcess(const VpdProcess::KeyValuePairs& updates,
                     bool ignore_cache,
                     const VpdProcess::CompletionCallback& completion) {
    EXPECT_EQ(1, updates.size());
    EXPECT_TRUE(ignore_cache);
    if (updates.size() == 1) {
      EXPECT_EQ(SessionManagerImpl::kTPMFirmwareUpdateParamsVPDKey,
                updates[0].first);
      EXPECT_EQ(expected_vpd_params_, updates[0].second);
    }
    if (vpd_spawned_) {
      completion_ = std::move(completion);
    }
    return vpd_spawned_;
  }

  void SetVpdSpawned(bool spawned) { vpd_spawned_ = spawned; }

  void SetVpdStatus(bool status) { vpd_status_ = status; }

  void SetPolicy(const ChromeDeviceSettingsProto& settings) {
    PolicyData policy_data;
    CHECK(settings.SerializeToString(policy_data.mutable_policy_value()));
    CHECK(policy_data.SerializeToString(policy_.mutable_policy_data()));
  }

  std::string update_mode_ = "first_boot";
  std::string existing_vpd_params_;
  std::string expected_vpd_params_ = "mode:first_boot";
  std::string expected_error_;
  std::map<std::string, bool> file_existence_;
  bool vpd_spawned_ = true;
  bool vpd_status_ = true;
  VpdProcess::CompletionCallback completion_;
  PolicyFetchResponse policy_;
};

TEST_F(StartTPMFirmwareUpdateTest, Success_FirstBoot) {
  ExpectDeviceRestart();
}

TEST_F(StartTPMFirmwareUpdateTest, Success_Recovery) {
  SetUpdateMode("recovery");
  SetExpectedVPDParams("mode:recovery");
}

TEST_F(StartTPMFirmwareUpdateTest, Success_DryRunPreserved) {
  SetExistingVPDParams("attempts:2,dryrun:1,mode:complete");
  SetExpectedVPDParams("mode:first_boot,dryrun:1");
  ExpectDeviceRestart();
}

TEST_F(StartTPMFirmwareUpdateTest, AlreadyLoggedIn) {
  SetFileExists(SessionManagerImpl::kLoggedInFlag, true);
  ExpectError(dbus_error::kSessionExists);
}

TEST_F(StartTPMFirmwareUpdateTest, BadUpdateMode) {
  SetUpdateMode("no_such_thing");
  ExpectError(dbus_error::kInvalidParameter);
}

TEST_F(StartTPMFirmwareUpdateTest, EnterpriseNotSet) {
  EXPECT_CALL(*device_policy_service_, InstallAttributesEnterpriseMode())
      .WillRepeatedly(Return(true));
  ExpectError(dbus_error::kNotAvailable);
}

TEST_F(StartTPMFirmwareUpdateTest, EnterpriseAllowed) {
  EXPECT_CALL(*device_policy_service_, InstallAttributesEnterpriseMode())
      .WillRepeatedly(Return(true));
  ChromeDeviceSettingsProto settings;
  settings.mutable_tpm_firmware_update_settings()
      ->set_allow_user_initiated_powerwash(true);
  SetPolicy(settings);
  ExpectDeviceRestart();
}

TEST_F(StartTPMFirmwareUpdateTest, VpdSpawnError) {
  SetVpdSpawned(false);
  ExpectError(dbus_error::kVpdUpdateFailed);
}

TEST_F(StartTPMFirmwareUpdateTest, VpdStatusError) {
  SetVpdStatus(false);
  ExpectError(dbus_error::kVpdUpdateFailed);
}

class SessionManagerImplStaticTest : public ::testing::Test {
 public:
  SessionManagerImplStaticTest() {}
  virtual ~SessionManagerImplStaticTest() {}

  bool ValidateEmail(const string& email_address) {
    return SessionManagerImpl::ValidateEmail(email_address);
  }

  bool ValidateAccountIdKey(const string& account_id) {
    return SessionManagerImpl::ValidateAccountIdKey(account_id);
  }

#if USE_CHEETS
  bool ValidateStartArcInstanceRequest(const StartArcInstanceRequest& request,
                                       brillo::ErrorPtr* error) {
    return SessionManagerImpl::ValidateStartArcInstanceRequest(request, error);
  }
#endif
};

TEST_F(SessionManagerImplStaticTest, EmailAddressTest) {
  EXPECT_TRUE(ValidateEmail("user_who+we.like@some-where.com"));
  EXPECT_TRUE(ValidateEmail("john_doe's_mail@some-where.com"));
}

TEST_F(SessionManagerImplStaticTest, EmailAddressNonAsciiTest) {
  char invalid[4] = "a@m";
  invalid[2] = static_cast<char>(254);
  EXPECT_FALSE(ValidateEmail(invalid));
}

TEST_F(SessionManagerImplStaticTest, EmailAddressNoAtTest) {
  const char no_at[] = "user";
  EXPECT_FALSE(ValidateEmail(no_at));
}

TEST_F(SessionManagerImplStaticTest, EmailAddressTooMuchAtTest) {
  const char extra_at[] = "user@what@where";
  EXPECT_FALSE(ValidateEmail(extra_at));
}

TEST_F(SessionManagerImplStaticTest, AccountIdKeyTest) {
  EXPECT_TRUE(ValidateAccountIdKey("g-1234567890123456"));
  // email string is invalid GaiaIdKey
  EXPECT_FALSE(ValidateAccountIdKey("john@some.where.com"));
  // Only alphanumeric characters plus a colon are allowed.
  EXPECT_TRUE(ValidateAccountIdKey("g-1234567890"));
  EXPECT_TRUE(ValidateAccountIdKey("g-abcdef0123456789"));
  EXPECT_TRUE(ValidateAccountIdKey("g-ABCDEF0123456789"));
  EXPECT_FALSE(ValidateAccountIdKey("g-123@some.where.com"));
  EXPECT_FALSE(ValidateAccountIdKey("g-123@localhost"));
  // Active Directory account keys.
  EXPECT_TRUE(ValidateAccountIdKey("a-abcdef0123456789"));
  EXPECT_FALSE(ValidateAccountIdKey("a-123@localhost"));
}

#if USE_CHEETS
TEST_F(SessionManagerImplStaticTest, StartArcInstanceRequestForUser) {
  StartArcInstanceRequest request;
  request.set_for_login_screen(false);
  request.set_account_id("dummy_account_id");
  request.set_skip_boot_completed_broadcast(true);
  request.set_scan_vendor_priv_app(true);
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(ValidateStartArcInstanceRequest(request, &error));
    EXPECT_FALSE(error.get());
  }

  // If a required field is not set, validation should fail.
  {
    brillo::ErrorPtr error;
    StartArcInstanceRequest request2 = request;
    request2.clear_account_id();
    EXPECT_FALSE(ValidateStartArcInstanceRequest(request2, &error));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(DBUS_ERROR_INVALID_ARGS, error->GetCode());
  }
  {
    brillo::ErrorPtr error;
    StartArcInstanceRequest request2 = request;
    request2.clear_skip_boot_completed_broadcast();
    EXPECT_FALSE(ValidateStartArcInstanceRequest(request2, &error));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(DBUS_ERROR_INVALID_ARGS, error->GetCode());
  }
  {
    brillo::ErrorPtr error;
    StartArcInstanceRequest request2 = request;
    request2.clear_scan_vendor_priv_app();
    EXPECT_FALSE(ValidateStartArcInstanceRequest(request2, &error));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(DBUS_ERROR_INVALID_ARGS, error->GetCode());
  }
}

TEST_F(SessionManagerImplStaticTest, StartArcInstanceRequestForLoginScreen) {
  StartArcInstanceRequest request;
  request.set_for_login_screen(true);
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(ValidateStartArcInstanceRequest(request, &error));
    EXPECT_FALSE(error.get());
  }

  // If any other fields is set, validation should fail.
  {
    brillo::ErrorPtr error;
    StartArcInstanceRequest request2 = request;
    request2.set_account_id("dummy_account_id");
    EXPECT_FALSE(ValidateStartArcInstanceRequest(request2, &error));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(DBUS_ERROR_INVALID_ARGS, error->GetCode());
  }
  {
    brillo::ErrorPtr error;
    StartArcInstanceRequest request2 = request;
    request2.set_skip_boot_completed_broadcast(true);
    EXPECT_FALSE(ValidateStartArcInstanceRequest(request2, &error));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(DBUS_ERROR_INVALID_ARGS, error->GetCode());
  }
  {
    brillo::ErrorPtr error;
    StartArcInstanceRequest request2 = request;
    request2.set_scan_vendor_priv_app(true);
    EXPECT_FALSE(ValidateStartArcInstanceRequest(request2, &error));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(DBUS_ERROR_INVALID_ARGS, error->GetCode());
  }
}

#endif

}  // namespace login_manager
