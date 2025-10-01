// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_manager.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <arc/arc.pb.h>
#include <base/memory/ptr_util.h>
#include <base/memory/raw_ptr.h>
#include <base/memory/ref_counted.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>
#include <brillo/errors/error.h>
#include <dbus/debugd/dbus-constants.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/blob_util.h"
#include "login_manager/dbus_test_util.h"
#include "login_manager/fake_container_manager.h"
#include "login_manager/fake_system_utils.h"
#include "login_manager/init_daemon_controller.h"
#include "login_manager/mock_arc_sideload_status.h"
#include "login_manager/mock_init_daemon_controller.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/session_manager_impl.h"

namespace login_manager {

using ::testing::_;
using ::testing::ByMove;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnNull;
using ::testing::StartsWith;
using ::testing::WithArg;

constexpr pid_t kAndroidPid = 10;

constexpr char kSaneEmail[] = "user@somewhere.com";

#if USE_CHEETS
constexpr char kDefaultLocale[] = "en_US";

arc::UpgradeArcContainerRequest CreateUpgradeArcContainerRequest() {
  arc::UpgradeArcContainerRequest request;
  request.set_account_id(kSaneEmail);
  request.set_locale(kDefaultLocale);
  return request;
}

std::string ExpectedSkipPackagesCacheSetupFlagValue(bool enabled) {
  return base::StringPrintf("SKIP_PACKAGES_CACHE_SETUP=%d", enabled);
}

std::string ExpectedCopyPackagesCacheFlagValue(bool enabled) {
  return base::StringPrintf("COPY_PACKAGES_CACHE=%d", enabled);
}

std::string ExpectedSkipGmsCoreCacheSetupFlagValue(bool enabled) {
  return base::StringPrintf("SKIP_GMS_CORE_CACHE_SETUP=%d", enabled);
}

std::string ExpectedSkipTtsCacheSetupFlagValue(bool enabled) {
  return base::StringPrintf("SKIP_TTS_CACHE_SETUP=%d", enabled);
}

class StartArcInstanceExpectationsBuilder {
 public:
  StartArcInstanceExpectationsBuilder() = default;
  StartArcInstanceExpectationsBuilder(
      const StartArcInstanceExpectationsBuilder&) = delete;
  StartArcInstanceExpectationsBuilder& operator=(
      const StartArcInstanceExpectationsBuilder&) = delete;

  StartArcInstanceExpectationsBuilder& SetDevMode(bool v) {
    dev_mode_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetNativeBridgeExperiment(bool v) {
    native_bridge_experiment_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetArcCustomTabExperiment(bool v) {
    arc_custom_tab_experiment_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetDisableMediaStoreMaintenance(bool v) {
    disable_media_store_maintenance_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetDisableDownloadProvider(bool v) {
    disable_download_provider_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetEnableConsumerAutoUpdateToggle(
      int v) {
    enable_consumer_auto_update_toggle_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetEnablePrivacyHubForChrome(int v) {
    enable_privacy_hub_for_chrome_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetArcGeneratePai(bool v) {
    arc_generate_pai_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetPlayStoreAutoUpdate(
      arc::StartArcMiniInstanceRequest_PlayStoreAutoUpdate v) {
    play_store_auto_update_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetArcLcdDensity(int v) {
    arc_lcd_density_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetDalvikMemoryProfile(
      arc::StartArcMiniInstanceRequest_DalvikMemoryProfile v) {
    dalvik_memory_profile_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetEnableTTSCaching(bool v) {
    enable_tts_caching_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetHostUreadaheadMode(
      arc::StartArcMiniInstanceRequest_HostUreadaheadMode v) {
    host_ureadahead_mode_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetUseDevCaches(bool v) {
    use_dev_caches_ = v;
    return *this;
  }

  StartArcInstanceExpectationsBuilder& SetArcSignedIn(bool v) {
    arc_signed_in_ = v;
    return *this;
  }

  std::vector<std::string> Build() const {
    std::vector<std::string> result({
        "CHROMEOS_DEV_MODE=" + std::to_string(dev_mode_),
        "CHROMEOS_INSIDE_VM=0",
        "NATIVE_BRIDGE_EXPERIMENT=" + std::to_string(native_bridge_experiment_),
        "DISABLE_MEDIA_STORE_MAINTENANCE=" +
            std::to_string(disable_media_store_maintenance_),
        "DISABLE_DOWNLOAD_PROVIDER=" +
            std::to_string(disable_download_provider_),
        "ENABLE_CONSUMER_AUTO_UPDATE_TOGGLE=" +
            std::to_string(enable_consumer_auto_update_toggle_),
        "ENABLE_PRIVACY_HUB_FOR_CHROME=" +
            std::to_string(enable_privacy_hub_for_chrome_),
        "ENABLE_TTS_CACHING=" + std::to_string(enable_tts_caching_),
        "USE_DEV_CACHES=" + std::to_string(use_dev_caches_),
        "ARC_SIGNED_IN=" + std::to_string(arc_signed_in_),
    });

    if (arc_generate_pai_) {
      result.emplace_back("ARC_GENERATE_PAI=1");
    }

    if (arc_lcd_density_ >= 0) {
      result.emplace_back(
          base::StringPrintf("ARC_LCD_DENSITY=%d", arc_lcd_density_));
    }

    switch (play_store_auto_update_) {
      case arc::StartArcMiniInstanceRequest::AUTO_UPDATE_DEFAULT:
        break;
      case arc::StartArcMiniInstanceRequest::AUTO_UPDATE_ON:
        result.emplace_back("PLAY_STORE_AUTO_UPDATE=1");
        break;
      case arc::StartArcMiniInstanceRequest::AUTO_UPDATE_OFF:
        result.emplace_back("PLAY_STORE_AUTO_UPDATE=0");
        break;
      default:
        NOTREACHED_IN_MIGRATION();
    }

    switch (dalvik_memory_profile_) {
      case arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_DEFAULT:
        break;
      case arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_4G:
        result.emplace_back("DALVIK_MEMORY_PROFILE=4G");
        break;
      case arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_8G:
        result.emplace_back("DALVIK_MEMORY_PROFILE=8G");
        break;
      case arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_16G:
        result.emplace_back("DALVIK_MEMORY_PROFILE=16G");
        break;
      default:
        NOTREACHED_IN_MIGRATION();
    }

    switch (host_ureadahead_mode_) {
      case arc::StartArcMiniInstanceRequest::MODE_DEFAULT:
        result.emplace_back("HOST_UREADAHEAD_MODE=DEFAULT");
        break;
      case arc::StartArcMiniInstanceRequest::MODE_GENERATE:
        result.emplace_back("HOST_UREADAHEAD_MODE=GENERATE");
        break;
      case arc::StartArcMiniInstanceRequest::MODE_DISABLED:
        result.emplace_back("HOST_UREADAHEAD_MODE=DISABLED");
        break;
      default:
        NOTREACHED_IN_MIGRATION();
    }

    return result;
  }

 private:
  bool dev_mode_ = false;
  bool native_bridge_experiment_ = false;
  bool arc_custom_tab_experiment_ = false;

  bool disable_media_store_maintenance_ = false;
  bool disable_download_provider_ = false;
  bool enable_consumer_auto_update_toggle_ = false;
  bool enable_privacy_hub_for_chrome_ = false;
  bool enable_tts_caching_ = false;
  bool use_dev_caches_ = false;
  bool arc_generate_pai_ = false;
  bool arc_signed_in_ = false;
  arc::StartArcMiniInstanceRequest_PlayStoreAutoUpdate play_store_auto_update_ =
      arc::StartArcMiniInstanceRequest_PlayStoreAutoUpdate_AUTO_UPDATE_DEFAULT;
  int arc_lcd_density_ = -1;
  arc::StartArcMiniInstanceRequest_DalvikMemoryProfile dalvik_memory_profile_ =
      arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_DEFAULT;
  arc::StartArcMiniInstanceRequest_HostUreadaheadMode host_ureadahead_mode_ =
      arc::StartArcMiniInstanceRequest::MODE_DEFAULT;
};

class UpgradeContainerExpectationsBuilder {
 public:
  UpgradeContainerExpectationsBuilder() = default;
  UpgradeContainerExpectationsBuilder(
      const UpgradeContainerExpectationsBuilder&) = delete;
  UpgradeContainerExpectationsBuilder& operator=(
      const UpgradeContainerExpectationsBuilder&) = delete;

  UpgradeContainerExpectationsBuilder& SetDevMode(bool v) {
    dev_mode_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetDisableBootCompletedCallback(bool v) {
    disable_boot_completed_callback_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetIsDemoSession(bool v) {
    is_demo_session_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetDemoSessionAppsPath(
      const std::string& v) {
    demo_session_apps_path_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetSkipPackagesCache(bool v) {
    skip_packages_cache_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetCopyPackagesCache(bool v) {
    copy_packages_cache_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetSkipGmsCoreCache(bool v) {
    skip_gms_core_cache_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetLocale(const std::string& v) {
    locale_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetPreferredLanguages(
      const std::string& v) {
    preferred_languages_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetEnableAdbSideload(int v) {
    enable_adb_sideload_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetEnableArcNearbyShare(int v) {
    enable_arc_nearby_share_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetManagementTransition(bool v) {
    management_transition_ = v;
    return *this;
  }

  UpgradeContainerExpectationsBuilder& SetSkipTtsCache(bool v) {
    skip_tts_cache_ = v;
    return *this;
  }

  std::vector<std::string> Build() const {
    return {
        "CHROMEOS_DEV_MODE=" + std::to_string(dev_mode_),
        "CHROMEOS_INSIDE_VM=0", std::string("CHROMEOS_USER=") + kSaneEmail,
        "DISABLE_BOOT_COMPLETED_BROADCAST=" +
            std::to_string(disable_boot_completed_callback_),
        // The upgrade signal has a PID.
        "CONTAINER_PID=" + std::to_string(kAndroidPid),
        "DEMO_SESSION_APPS_PATH=" + demo_session_apps_path_,
        "IS_DEMO_SESSION=" + std::to_string(is_demo_session_),
        "MANAGEMENT_TRANSITION=" + std::to_string(management_transition_),
        "ENABLE_ADB_SIDELOAD=" + std::to_string(enable_adb_sideload_),
        "ENABLE_ARC_NEARBY_SHARE=" + std::to_string(enable_arc_nearby_share_),
        ExpectedSkipPackagesCacheSetupFlagValue(skip_packages_cache_),
        ExpectedCopyPackagesCacheFlagValue(copy_packages_cache_),
        ExpectedSkipGmsCoreCacheSetupFlagValue(skip_gms_core_cache_),
        ExpectedSkipTtsCacheSetupFlagValue(skip_tts_cache_),
        "LOCALE=" + locale_, "PREFERRED_LANGUAGES=" + preferred_languages_};
  }

 private:
  bool dev_mode_ = false;
  bool disable_boot_completed_callback_ = false;
  bool is_demo_session_ = false;
  std::string demo_session_apps_path_;
  bool skip_packages_cache_ = false;
  bool copy_packages_cache_ = false;
  bool skip_gms_core_cache_ = false;
  std::string locale_ = kDefaultLocale;
  std::string preferred_languages_;
  int management_transition_ = 0;
  bool enable_adb_sideload_ = false;
  bool enable_arc_nearby_share_ = false;
  bool skip_tts_cache_ = false;
};
#endif  // USE_CHEETS

class TestArcManagerObserver : public ArcManager::Observer {
 public:
  TestArcManagerObserver() = default;
  TestArcManagerObserver(const TestArcManagerObserver&) = delete;
  TestArcManagerObserver& operator=(const TestArcManagerObserver&) = delete;
  ~TestArcManagerObserver() override = default;

  // ArcManager::Observer:
  void OnArcInstanceStopped(uint32_t value) override {
    values_.push_back(value);
  }

  const std::vector<uint32_t>& values() const { return values_; }

 private:
  std::vector<uint32_t> values_;
};

// TODO(crbug.com/390297821): Move out into independent file.
class ArcManagerTest : public testing::Test {
 public:
  void SetUp() override {
    arc_init_controller_ = new MockInitDaemonController();
    arc_sideload_status_ = new MockArcSideloadStatus();
    android_container_ = new FakeContainerManager(kAndroidPid);
    arc_manager_ = ArcManager::CreateForTesting(
        system_utils_, metrics_, /*bus=*/nullptr,
        base::WrapUnique(arc_init_controller_), debugd_proxy_.get(),
        base::WrapUnique(android_container_),
        std::unique_ptr<ArcSideloadStatusInterface>(arc_sideload_status_));

    observation_.Observe(arc_manager_.get());
  }

  void TearDown() override {
    observation_.Reset();

    android_container_ = nullptr;
    arc_init_controller_ = nullptr;
    arc_sideload_status_ = nullptr;
    arc_manager_.reset();
  }

#if USE_CHEETS
  void SetUpArcMiniContainer() {
    EXPECT_CALL(*arc_init_controller_,
                TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                               StartArcInstanceExpectationsBuilder().Build(),
                               InitDaemonController::TriggerMode::ASYNC))
        .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

    brillo::ErrorPtr error;
    EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
        &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));
    testing::Mock::VerifyAndClearExpectations(arc_init_controller_);
  }
#endif

 protected:
  FakeSystemUtils system_utils_;
  MockMetrics metrics_;
  FakeContainerManager* android_container_ = nullptr;
  MockInitDaemonController* arc_init_controller_ = nullptr;
  MockArcSideloadStatus* arc_sideload_status_ = nullptr;
  scoped_refptr<dbus::MockObjectProxy> debugd_proxy_ =
      new dbus::MockObjectProxy(nullptr, "", dbus::ObjectPath("/fake/debugd"));
  std::unique_ptr<ArcManager> arc_manager_;
  TestArcManagerObserver observer_;
  base::ScopedObservation<ArcManager, ArcManager::Observer> observation_{
      &observer_};
};

class ArcManagerPackagesCacheTest
    : public ArcManagerTest,
      public testing::WithParamInterface<
          std::tuple<arc::UpgradeArcContainerRequest_PackageCacheMode,
                     bool,
                     bool>> {
 public:
  ArcManagerPackagesCacheTest() = default;
  ArcManagerPackagesCacheTest(const ArcManagerPackagesCacheTest&) = delete;
  ArcManagerPackagesCacheTest& operator=(const ArcManagerPackagesCacheTest&) =
      delete;

  ~ArcManagerPackagesCacheTest() override = default;
};

class ArcManagerPlayStoreAutoUpdateTest
    : public ArcManagerTest,
      public testing::WithParamInterface<
          arc::StartArcMiniInstanceRequest_PlayStoreAutoUpdate> {
 public:
  ArcManagerPlayStoreAutoUpdateTest() = default;
  ArcManagerPlayStoreAutoUpdateTest(const ArcManagerPlayStoreAutoUpdateTest&) =
      delete;
  ArcManagerPlayStoreAutoUpdateTest& operator=(
      const ArcManagerPlayStoreAutoUpdateTest&) = delete;

  ~ArcManagerPlayStoreAutoUpdateTest() override = default;
};

class ArcManagerDalvikMemoryProfileTest
    : public ArcManagerTest,
      public testing::WithParamInterface<
          arc::StartArcMiniInstanceRequest_DalvikMemoryProfile> {
 public:
  ArcManagerDalvikMemoryProfileTest() = default;
  ArcManagerDalvikMemoryProfileTest(const ArcManagerDalvikMemoryProfileTest&) =
      delete;
  ArcManagerDalvikMemoryProfileTest& operator=(
      const ArcManagerDalvikMemoryProfileTest&) = delete;

  ~ArcManagerDalvikMemoryProfileTest() override = default;
};

class ArcManagerHostUreadaheadModeTest
    : public ArcManagerTest,
      public testing::WithParamInterface<
          arc::StartArcMiniInstanceRequest_HostUreadaheadMode> {
 public:
  ArcManagerHostUreadaheadModeTest() = default;
  ArcManagerHostUreadaheadModeTest(const ArcManagerHostUreadaheadModeTest&) =
      delete;
  ArcManagerHostUreadaheadModeTest& operator=(
      const ArcManagerHostUreadaheadModeTest&) = delete;
  ~ArcManagerHostUreadaheadModeTest() override = default;
};

#if USE_CHEETS
TEST_F(ArcManagerTest, StopArcInstance) {
  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));
  EXPECT_FALSE(error.get());

  EXPECT_TRUE(observer_.values().empty());
  EXPECT_TRUE(arc_manager_->StopArcInstance(
      &error, std::string() /*account_id*/, false /*should_backup_log*/));
  EXPECT_FALSE(error.get());

  ASSERT_EQ(observer_.values().size(), 1u);
  EXPECT_EQ(observer_.values()[0],
            static_cast<uint32_t>(ArcContainerStopReason::USER_REQUEST));
}

TEST_F(ArcManagerTest, StopArcInstance_BackupsArcBugReport) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  EXPECT_CALL(*debugd_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(WithArg<0>(Invoke([](dbus::MethodCall* method_call) {
        EXPECT_EQ(method_call->GetInterface(), debugd::kDebugdInterface);
        EXPECT_EQ(method_call->GetMember(), debugd::kBackupArcBugReport);
        return base::ok(dbus::Response::CreateEmpty());
      })));

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));
  EXPECT_FALSE(error.get());

  EXPECT_TRUE(observer_.values().empty());
  EXPECT_TRUE(arc_manager_->StopArcInstance(&error, kSaneEmail,
                                            true /*should_backup_log*/));
  EXPECT_FALSE(error.get());

  ASSERT_EQ(observer_.values().size(), 1u);
  EXPECT_EQ(observer_.values()[0],
            static_cast<uint32_t>(ArcContainerStopReason::USER_REQUEST));
}

TEST_F(ArcManagerTest, StartArcMiniContainer) {
  {
    int64_t start_time = 0;
    brillo::ErrorPtr error;
    EXPECT_FALSE(arc_manager_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder().Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(android_container_->running());

  // StartArcInstance() does not update start time for login screen.
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_FALSE(arc_manager_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStopArcInstanceImpulse, ElementsAre(),
                             InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  EXPECT_TRUE(observer_.values().empty());
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(arc_manager_->StopArcInstance(
        &error, std::string() /*account_id*/, false /*should_backup_log*/));
    EXPECT_FALSE(error.get());
  }

  ASSERT_EQ(observer_.values().size(), 1u);
  EXPECT_EQ(observer_.values()[0],
            static_cast<uint32_t>(ArcContainerStopReason::USER_REQUEST));
  EXPECT_FALSE(android_container_->running());
}

TEST_F(ArcManagerTest, UpgradeArcContainer) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder().Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));

  // Then, upgrade it to a fully functional one.
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_FALSE(arc_manager_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulseWithTimeoutAndError(
                  ArcManager::kContinueArcBootImpulse,
                  UpgradeContainerExpectationsBuilder().Build(),
                  InitDaemonController::TriggerMode::SYNC,
                  ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStopArcInstanceImpulse, ElementsAre(),
                             InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(android_container_->running());
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_TRUE(arc_manager_->GetArcStartTimeTicks(&error, &start_time));
    EXPECT_NE(0, start_time);
    ASSERT_FALSE(error.get());
  }
  // The ID for the container for login screen is passed to the dbus call.

  EXPECT_TRUE(observer_.values().empty());
  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(arc_manager_->StopArcInstance(
        &error, std::string() /*account_id*/, false /*should_backup_log*/));
    EXPECT_FALSE(error.get());
  }
  ASSERT_EQ(observer_.values().size(), 1u);
  EXPECT_EQ(observer_.values()[0],
            static_cast<uint32_t>(ArcContainerStopReason::USER_REQUEST));
  EXPECT_FALSE(android_container_->running());
}

TEST_F(ArcManagerTest, UpgradeArcContainer_BackupsArcBugReportOnFailure) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder().Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));

  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulseWithTimeoutAndError(
                  ArcManager::kContinueArcBootImpulse,
                  UpgradeContainerExpectationsBuilder().Build(),
                  InitDaemonController::TriggerMode::SYNC,
                  ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(ReturnNull());
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStopArcInstanceImpulse, ElementsAre(),
                             InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  EXPECT_CALL(*arc_sideload_status_, IsAdbSideloadAllowed())
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*debugd_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(WithArg<0>(Invoke([](dbus::MethodCall* method_call) {
        EXPECT_EQ(method_call->GetInterface(), debugd::kDebugdInterface);
        EXPECT_EQ(method_call->GetMember(), debugd::kBackupArcBugReport);
        return base::ok(dbus::Response::CreateEmpty());
      })));

  EXPECT_TRUE(observer_.values().empty());
  auto upgrade_request = CreateUpgradeArcContainerRequest();
  EXPECT_FALSE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_TRUE(error.get());

  ASSERT_EQ(observer_.values().size(), 1u);
  EXPECT_EQ(observer_.values()[0],
            static_cast<uint32_t>(ArcContainerStopReason::UPGRADE_FAILURE));
  EXPECT_FALSE(android_container_->running());
}

TEST_F(ArcManagerTest, UpgradeArcContainerWithManagementTransition) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  SetUpArcMiniContainer();

  // Expect continue-arc-boot and start-arc-network impulses.
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulseWithTimeoutAndError(ArcManager::kContinueArcBootImpulse,
                                        UpgradeContainerExpectationsBuilder()
                                            .SetManagementTransition(1)
                                            .Build(),
                                        InitDaemonController::TriggerMode::SYNC,
                                        ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  upgrade_request.set_management_transition(
      arc::UpgradeArcContainerRequest_ManagementTransition_CHILD_TO_REGULAR);

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(android_container_->running());
}

TEST_F(ArcManagerTest, DisableMediaStoreMaintenance) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  arc::StartArcMiniInstanceRequest request;
  request.set_disable_media_store_maintenance(true);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetDisableMediaStoreMaintenance(true)
                                 .Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
}

TEST_F(ArcManagerTest, EnableConsumerAutoUpdateToggle) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetEnableConsumerAutoUpdateToggle(true)
                                 .Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  arc::StartArcMiniInstanceRequest request;
  request.set_enable_consumer_auto_update_toggle(true);

  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
  EXPECT_FALSE(error.get());
}

TEST_F(ArcManagerTest, EnablePrivacyHubForChrome) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetEnablePrivacyHubForChrome(true)
                                 .Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  arc::StartArcMiniInstanceRequest request;
  request.set_enable_privacy_hub_for_chrome(true);

  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
  EXPECT_FALSE(error.get());
}

TEST_F(ArcManagerTest, DisableDownloadProvider) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  arc::StartArcMiniInstanceRequest request;
  request.set_disable_download_provider(true);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetDisableDownloadProvider(true)
                                 .Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
}

TEST_F(ArcManagerTest, EnableTTSCaching) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  arc::StartArcMiniInstanceRequest request;
  request.set_enable_tts_caching(true);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetEnableTTSCaching(true)
                                 .Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
}

TEST_F(ArcManagerTest, UseDevCaches) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  arc::StartArcMiniInstanceRequest request;
  request.set_use_dev_caches(true);

  // First, start ARC for login screen.
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulse(
          ArcManager::kStartArcInstanceImpulse,
          StartArcInstanceExpectationsBuilder().SetUseDevCaches(true).Build(),
          InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
}

TEST_F(ArcManagerTest, ArcSignedIn) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulse(
          ArcManager::kStartArcInstanceImpulse,
          StartArcInstanceExpectationsBuilder().SetArcSignedIn(true).Build(),
          InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  arc::StartArcMiniInstanceRequest request;
  request.set_arc_signed_in(true);

  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
  EXPECT_FALSE(error.get());
}

TEST_P(ArcManagerPackagesCacheTest, PackagesCache) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder().Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));

  bool skip_packages_cache_setup = false;
  bool copy_cache_setup = false;
  switch (std::get<0>(GetParam())) {
    case arc::
        UpgradeArcContainerRequest_PackageCacheMode_SKIP_SETUP_COPY_ON_INIT:
      skip_packages_cache_setup = true;
      [[fallthrough]];
    case arc::UpgradeArcContainerRequest_PackageCacheMode_COPY_ON_INIT:
      copy_cache_setup = true;
      break;
    case arc::UpgradeArcContainerRequest_PackageCacheMode_DEFAULT:
      break;
    default:
      NOTREACHED_IN_MIGRATION();
  }

  // Then, upgrade it to a fully functional one.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulseWithTimeoutAndError(
                  ArcManager::kContinueArcBootImpulse,
                  UpgradeContainerExpectationsBuilder()
                      .SetSkipPackagesCache(skip_packages_cache_setup)
                      .SetCopyPackagesCache(copy_cache_setup)
                      .SetSkipGmsCoreCache(std::get<1>(GetParam()))
                      .SetSkipTtsCache(std::get<2>(GetParam()))
                      .Build(),
                  InitDaemonController::TriggerMode::SYNC,
                  ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStopArcInstanceImpulse, ElementsAre(),
                             InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  upgrade_request.set_packages_cache_mode(std::get<0>(GetParam()));
  upgrade_request.set_skip_gms_core_cache(std::get<1>(GetParam()));
  upgrade_request.set_skip_tts_cache(std::get<2>(GetParam()));
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_TRUE(android_container_->running());

  EXPECT_TRUE(arc_manager_->StopArcInstance(
      &error, std::string() /*account_id*/, false /*should_backup_log*/));
  EXPECT_FALSE(android_container_->running());
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ArcManagerPackagesCacheTest,
    ::testing::Combine(
        ::testing::Values(
            arc::UpgradeArcContainerRequest::DEFAULT,
            arc::UpgradeArcContainerRequest::COPY_ON_INIT,
            arc::UpgradeArcContainerRequest::SKIP_SETUP_COPY_ON_INIT),
        ::testing::Bool(),
        ::testing::Bool()));

TEST_P(ArcManagerPlayStoreAutoUpdateTest, PlayStoreAutoUpdate) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  arc::StartArcMiniInstanceRequest request;
  request.set_play_store_auto_update(GetParam());

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetPlayStoreAutoUpdate(GetParam())
                                 .Build(),

                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ArcManagerPlayStoreAutoUpdateTest,
    ::testing::ValuesIn(
        {arc::StartArcMiniInstanceRequest::AUTO_UPDATE_DEFAULT,
         arc::StartArcMiniInstanceRequest_PlayStoreAutoUpdate_AUTO_UPDATE_ON,
         arc::
             StartArcMiniInstanceRequest_PlayStoreAutoUpdate_AUTO_UPDATE_OFF}));

TEST_P(ArcManagerDalvikMemoryProfileTest, DalvikMemoryProfile) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  arc::StartArcMiniInstanceRequest request;
  request.set_dalvik_memory_profile(GetParam());

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetDalvikMemoryProfile(GetParam())
                                 .Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ArcManagerDalvikMemoryProfileTest,
    ::testing::ValuesIn(
        {arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_DEFAULT,
         arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_4G,
         arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_8G,
         arc::StartArcMiniInstanceRequest::MEMORY_PROFILE_16G}));

TEST_P(ArcManagerHostUreadaheadModeTest, HostUreadaheadMode) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  arc::StartArcMiniInstanceRequest request;
  request.set_host_ureadahead_mode(GetParam());

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetHostUreadaheadMode(GetParam())
                                 .Build(),

                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ArcManagerHostUreadaheadModeTest,
    ::testing::ValuesIn(
        {arc::StartArcMiniInstanceRequest_HostUreadaheadMode_MODE_DEFAULT,
         arc::StartArcMiniInstanceRequest_HostUreadaheadMode_MODE_GENERATE,
         arc::StartArcMiniInstanceRequest_HostUreadaheadMode_MODE_DISABLED}));

TEST_F(ArcManagerTest, UpgradeArcContainerForDemoSession) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder().Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));

  // Then, upgrade it to a fully functional one.
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_FALSE(arc_manager_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulseWithTimeoutAndError(
                  ArcManager::kContinueArcBootImpulse,
                  UpgradeContainerExpectationsBuilder()
                      .SetIsDemoSession(true)
                      .SetDemoSessionAppsPath(
                          "/run/imageloader/0.1/demo_apps/img.squash")
                      .Build(),
                  InitDaemonController::TriggerMode::SYNC,
                  ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStopArcInstanceImpulse, ElementsAre(),
                             InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  upgrade_request.set_is_demo_session(true);
  upgrade_request.set_demo_session_apps_path(
      "/run/imageloader/0.1/demo_apps/img.squash");
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_TRUE(android_container_->running());

  EXPECT_TRUE(arc_manager_->StopArcInstance(
      &error, std::string() /*account_id*/, false /*should_backup_log*/));
  EXPECT_FALSE(android_container_->running());
}

TEST_F(ArcManagerTest, UpgradeArcContainerForDemoSessionWithoutDemoApps) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder().Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));

  // Then, upgrade it to a fully functional one.
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_FALSE(arc_manager_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulseWithTimeoutAndError(
          ArcManager::kContinueArcBootImpulse,
          UpgradeContainerExpectationsBuilder().SetIsDemoSession(true).Build(),
          InitDaemonController::TriggerMode::SYNC,
          ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStopArcInstanceImpulse, ElementsAre(),
                             InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  upgrade_request.set_is_demo_session(true);
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_TRUE(android_container_->running());

  EXPECT_TRUE(arc_manager_->StopArcInstance(
      &error, std::string() /*account_id*/, false /*should_backup_log*/));
  EXPECT_FALSE(android_container_->running());
}

TEST_F(ArcManagerTest, UpgradeArcContainer_AdbSideloadingEnabled) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);
  SetUpArcMiniContainer();
  // Expect continue-arc-boot and start-arc-network impulses.
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulseWithTimeoutAndError(ArcManager::kContinueArcBootImpulse,
                                        UpgradeContainerExpectationsBuilder()
                                            .SetEnableAdbSideload(true)
                                            .Build(),
                                        InitDaemonController::TriggerMode::SYNC,
                                        ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  // Pretend ADB sideloading is already enabled.
  EXPECT_CALL(*arc_sideload_status_, IsAdbSideloadAllowed())
      .WillRepeatedly(Return(true));

  auto upgrade_request = CreateUpgradeArcContainerRequest();

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(android_container_->running());
}

TEST_F(ArcManagerTest,
       UpgradeArcContainer_AdbSideloadingEnabled_ManagedAccount_Disallowed) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);
  SetUpArcMiniContainer();
  // Expect continue-arc-boot and start-arc-network impulses.
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulseWithTimeoutAndError(ArcManager::kContinueArcBootImpulse,
                                        UpgradeContainerExpectationsBuilder()
                                            .SetEnableAdbSideload(false)
                                            .Build(),
                                        InitDaemonController::TriggerMode::SYNC,
                                        ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  // Pretend ADB sideloading is already enabled.
  EXPECT_CALL(*arc_sideload_status_, IsAdbSideloadAllowed())
      .WillRepeatedly(Return(true));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  upgrade_request.set_is_account_managed(true);
  upgrade_request.set_is_managed_adb_sideloading_allowed(false);

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(android_container_->running());
}

TEST_F(ArcManagerTest,
       UpgradeArcContainer_AdbSideloadingEnabled_ManagedAccount_Allowed) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);
  SetUpArcMiniContainer();
  // Expect continue-arc-boot and start-arc-network impulses.
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulseWithTimeoutAndError(ArcManager::kContinueArcBootImpulse,
                                        UpgradeContainerExpectationsBuilder()
                                            .SetEnableAdbSideload(true)
                                            .Build(),
                                        InitDaemonController::TriggerMode::SYNC,
                                        ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  // Pretend ADB sideloading is already enabled.
  EXPECT_CALL(*arc_sideload_status_, IsAdbSideloadAllowed())
      .WillRepeatedly(Return(true));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  upgrade_request.set_is_account_managed(true);
  upgrade_request.set_is_managed_adb_sideloading_allowed(true);

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(android_container_->running());
}

TEST_F(ArcManagerTest, ArcNativeBridgeExperiment) {
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetNativeBridgeExperiment(true)
                                 .Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  arc::StartArcMiniInstanceRequest request;
  request.set_native_bridge_experiment(true);
  // Use for login screen mode for minimalistic test.
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
  EXPECT_FALSE(error.get());
}

TEST_F(ArcManagerTest, ArcGeneratePai) {
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulse(
          ArcManager::kStartArcInstanceImpulse,
          StartArcInstanceExpectationsBuilder().SetArcGeneratePai(true).Build(),
          InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  arc::StartArcMiniInstanceRequest request;
  request.set_arc_generate_pai(true);
  // Use for login screen mode for minimalistic test.
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
  EXPECT_FALSE(error.get());
}

TEST_F(ArcManagerTest, ArcLcdDensity) {
  constexpr int arc_lcd_density = 240;
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder()
                                 .SetArcLcdDensity(arc_lcd_density)
                                 .Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  arc::StartArcMiniInstanceRequest request;
  request.set_lcd_density(arc_lcd_density);
  // Use for login screen mode for minimalistic test.
  EXPECT_TRUE(
      arc_manager_->StartArcMiniContainer(&error, SerializeAsBlob(request)));
  EXPECT_FALSE(error.get());
}

TEST_F(ArcManagerTest, ArcNoSession) {
  SetUpArcMiniContainer();

  brillo::ErrorPtr error;
  arc::UpgradeArcContainerRequest request = CreateUpgradeArcContainerRequest();
  EXPECT_FALSE(
      arc_manager_->UpgradeArcContainer(&error, SerializeAsBlob(request)));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error->GetCode());
}

TEST_F(ArcManagerTest, ArcLowDisk) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);
  SetUpArcMiniContainer();
  // Emulate no free disk space.
  system_utils_.set_free_disk_space(0);

  brillo::ErrorPtr error;

  EXPECT_TRUE(observer_.values().empty());
  arc::UpgradeArcContainerRequest request = CreateUpgradeArcContainerRequest();
  EXPECT_FALSE(
      arc_manager_->UpgradeArcContainer(&error, SerializeAsBlob(request)));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kLowFreeDisk, error->GetCode());
  ASSERT_EQ(observer_.values().size(), 1u);
  EXPECT_EQ(observer_.values()[0],
            static_cast<uint32_t>(ArcContainerStopReason::LOW_DISK_SPACE));
}

TEST_F(ArcManagerTest, ArcUpgradeCrash) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);
  system_utils_.set_dev_mode_state(DevModeState::DEV_MODE_ON);

  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulse(
          ArcManager::kStartArcInstanceImpulse,
          StartArcInstanceExpectationsBuilder().SetDevMode(true).Build(),
          InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulseWithTimeoutAndError(
          ArcManager::kContinueArcBootImpulse,
          UpgradeContainerExpectationsBuilder().SetDevMode(true).Build(),
          InitDaemonController::TriggerMode::SYNC,
          ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStopArcInstanceImpulse, ElementsAre(),
                             InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  {
    brillo::ErrorPtr error;
    EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
        &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));
    EXPECT_FALSE(error.get());
  }

  {
    brillo::ErrorPtr error;
    arc::UpgradeArcContainerRequest request =
        CreateUpgradeArcContainerRequest();
    EXPECT_TRUE(
        arc_manager_->UpgradeArcContainer(&error, SerializeAsBlob(request)));
    EXPECT_FALSE(error.get());
  }
  EXPECT_TRUE(android_container_->running());

  EXPECT_TRUE(observer_.values().empty());

  android_container_->SimulateCrash();
  EXPECT_FALSE(android_container_->running());

  ASSERT_EQ(observer_.values().size(), 1u);
  EXPECT_EQ(observer_.values()[0],
            static_cast<uint32_t>(ArcContainerStopReason::CRASH));
  // This should now fail since the container was cleaned up already.
  {
    brillo::ErrorPtr error;
    EXPECT_FALSE(arc_manager_->StopArcInstance(
        &error, std::string() /*account_id*/, false /*should_backup_log*/));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kContainerShutdownFail, error->GetCode());
  }
}

TEST_F(ArcManagerTest, LocaleAndPreferredLanguages) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  // First, start ARC for login screen.
  EXPECT_CALL(*arc_init_controller_,
              TriggerImpulse(ArcManager::kStartArcInstanceImpulse,
                             StartArcInstanceExpectationsBuilder().Build(),
                             InitDaemonController::TriggerMode::ASYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));

  // Then, upgrade it to a fully functional one.
  {
    brillo::ErrorPtr error;
    int64_t start_time = 0;
    EXPECT_FALSE(arc_manager_->GetArcStartTimeTicks(&error, &start_time));
    ASSERT_TRUE(error.get());
    EXPECT_EQ(dbus_error::kNotStarted, error->GetCode());
  }

  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulseWithTimeoutAndError(ArcManager::kContinueArcBootImpulse,
                                        UpgradeContainerExpectationsBuilder()
                                            .SetLocale("fr_FR")
                                            .SetPreferredLanguages("ru,en")
                                            .Build(),
                                        InitDaemonController::TriggerMode::SYNC,
                                        ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  upgrade_request.set_locale("fr_FR");
  upgrade_request.add_preferred_languages("ru");
  upgrade_request.add_preferred_languages("en");
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(android_container_->running());
}

TEST_F(ArcManagerTest, UpgradeArcContainer_ArcNearbyShareEnabled) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);
  SetUpArcMiniContainer();

  // Expect continue-arc-boot and start-arc-network impulses.
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulseWithTimeoutAndError(ArcManager::kContinueArcBootImpulse,
                                        UpgradeContainerExpectationsBuilder()
                                            .SetEnableArcNearbyShare(true)
                                            .Build(),
                                        InitDaemonController::TriggerMode::SYNC,
                                        ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  upgrade_request.set_enable_arc_nearby_share(true);

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(android_container_->running());
}

TEST_F(ArcManagerTest, UpgradeArcContainer_ArcNearbyShareDisabled) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);
  SetUpArcMiniContainer();

  // Expect continue-arc-boot and start-arc-network impulses.
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulseWithTimeoutAndError(ArcManager::kContinueArcBootImpulse,
                                        UpgradeContainerExpectationsBuilder()
                                            .SetEnableArcNearbyShare(false)
                                            .Build(),
                                        InitDaemonController::TriggerMode::SYNC,
                                        ArcManager::kArcBootContinueTimeout, _))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));

  auto upgrade_request = CreateUpgradeArcContainerRequest();
  upgrade_request.set_enable_arc_nearby_share(false);

  brillo::ErrorPtr error;
  EXPECT_TRUE(arc_manager_->UpgradeArcContainer(
      &error, SerializeAsBlob(upgrade_request)));
  EXPECT_FALSE(error.get());
  EXPECT_TRUE(android_container_->running());
}
#else  // !USE_CHEETS

TEST_F(ArcManagerTest, ArcUnavailable) {
  arc_manager_->OnUserSessionStarted(kSaneEmail);

  brillo::ErrorPtr error;
  EXPECT_FALSE(arc_manager_->StartArcMiniContainer(
      &error, SerializeAsBlob(arc::StartArcMiniInstanceRequest())));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kNotAvailable, error->GetCode());
}

TEST_F(ArcManagerTest, EmitStopArcVmInstanceImpulse) {
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulse(ArcManager::kStopArcVmInstanceImpulse, ElementsAre(),
                     InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  arc_manager_->EmitStopArcVmInstanceImpulse();
}
#endif

TEST_F(ArcManagerTest, SetArcCpuRestrictionFails) {
#if USE_CHEETS
  brillo::ErrorPtr error;
  EXPECT_FALSE(arc_manager_->SetArcCpuRestriction(
      &error, static_cast<uint32_t>(NUM_CONTAINER_CPU_RESTRICTION_STATES)));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kArcCpuCgroupFail, error->GetCode());
#else
  brillo::ErrorPtr error;
  EXPECT_FALSE(arc_manager_->SetArcCpuRestriction(
      &error, static_cast<uint32_t>(CONTAINER_CPU_RESTRICTION_BACKGROUND)));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kNotAvailable, error->GetCode());
#endif
}

TEST_F(ArcManagerTest, EmitArcBooted) {
#if USE_CHEETS
  {
    EXPECT_CALL(*arc_init_controller_,
                TriggerImpulse(ArcManager::kArcBootedImpulse,
                               ElementsAre(StartsWith("CHROMEOS_USER=")),
                               InitDaemonController::TriggerMode::ASYNC))
        .WillOnce(Return(ByMove(nullptr)));
    brillo::ErrorPtr error;
    EXPECT_TRUE(arc_manager_->EmitArcBooted(&error, kSaneEmail));
    EXPECT_FALSE(error.get());
    testing::Mock::VerifyAndClearExpectations(arc_init_controller_);
  }

  {
    EXPECT_CALL(*arc_init_controller_,
                TriggerImpulse(ArcManager::kArcBootedImpulse, ElementsAre(),
                               InitDaemonController::TriggerMode::ASYNC))
        .WillOnce(Return(ByMove(nullptr)));
    brillo::ErrorPtr error;
    EXPECT_TRUE(arc_manager_->EmitArcBooted(&error, std::string()));
    EXPECT_FALSE(error.get());
    testing::Mock::VerifyAndClearExpectations(arc_init_controller_);
  }
#else
  brillo::ErrorPtr error;
  EXPECT_FALSE(arc_manager_->EmitArcBooted(&error, kSaneEmail));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kNotAvailable, error->GetCode());
#endif
}

TEST_F(ArcManagerTest, EnableAdbSideload) {
  EXPECT_CALL(*arc_sideload_status_, EnableAdbSideload(_));
  ResponseCapturer capturer;
  arc_manager_->EnableAdbSideload(capturer.CreateMethodResponse<bool>());
}

TEST_F(ArcManagerTest, EnableAdbSideloadAfterLoggedIn) {
  base::FilePath logged_in_path(SessionManagerImpl::kLoggedInFlag);
  ASSERT_FALSE(system_utils_.Exists(logged_in_path));
  ASSERT_TRUE(system_utils_.WriteStringToFile(logged_in_path, "1"));

  EXPECT_CALL(*arc_sideload_status_, EnableAdbSideload(_)).Times(0);

  ResponseCapturer capturer;
  arc_manager_->EnableAdbSideload(capturer.CreateMethodResponse<bool>());

  ASSERT_NE(capturer.response(), nullptr);
  EXPECT_EQ(dbus_error::kSessionExists, capturer.response()->GetErrorName());
}

TEST_F(ArcManagerTest, QueryAdbSideload) {
  EXPECT_CALL(*arc_sideload_status_, QueryAdbSideload(_));
  ResponseCapturer capturer;
  arc_manager_->QueryAdbSideload(capturer.CreateMethodResponse<bool>());
}

}  // namespace login_manager
