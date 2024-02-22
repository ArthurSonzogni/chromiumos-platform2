// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DLC_SERVICE_H_
#define DLCSERVICE_DLC_SERVICE_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/errors/error.h>
#include <brillo/message_loops/message_loop.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <imageloader/proto_bindings/imageloader.pb.h>
#include <imageloader/dbus-proxies.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <update_engine/dbus-proxies.h>

#include "dlcservice/dlc_base.h"
#include "dlcservice/dlc_creator_interface.h"
#include "dlcservice/system_state.h"
#include "dlcservice/types.h"
#include "dlcservice/utils/utils_interface.h"

namespace dlcservice {

class DlcServiceInterface {
 public:
  virtual ~DlcServiceInterface() = default;

  // Initializes the state of dlcservice.
  virtual void Initialize() = 0;

  // DLC Installation Flow
  //
  // To start an install, the initial requirement is to call this function.
  // During this phase, all necessary setup for update_engine to successfully
  // install DLC(s) and other files that require creation are handled.
  // Args:
  //   install_request: The DLC install request.
  //   response: The DBus method response to respond on.
  virtual void Install(
      const InstallRequest& install_request,
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) = 0;

  // DLC Uninstall/Purge Flow
  //
  // To delete the DLC this can be invoked, no prior step is required.
  // Args:
  //   id: The DLC ID that is to be uninstalled.
  //   err: The error that's set when returned false.
  // Return:
  //   True if the DLC with the ID passed in is successfully uninstalled,
  //   otherwise false. Deleting a valid DLC that's not installed is considered
  //   successfully uninstalled, however uninstalling a DLC that's not supported
  //   is a failure. Uninstalling a DLC that is installing is also a failure.
  virtual bool Uninstall(const std::string& id, brillo::ErrorPtr* err) = 0;

  // Create DLC slots and load deployed DLC image into the slots.
  // Args:
  //   id: The DLC ID that is to be deployed.
  //   err: The error that's set when returned false.
  // Return:
  //   True on success, otherwise false.
  virtual bool Deploy(const DlcId& id, brillo::ErrorPtr* err) = 0;

  // Returns a reference to a DLC object given a DLC ID. If the ID is not
  // supported, it will set the error and return |nullptr|.
  virtual DlcInterface* GetDlc(const DlcId& id, brillo::ErrorPtr* err) = 0;

  // Returns the list of installed DLCs.
  virtual DlcIdList GetInstalled() = 0;

  // Returns the list of DLCs with installed content.
  virtual DlcIdList GetExistingDlcs() = 0;

  // Unmount DLCs and change their states to `NOT_INSTALLED`.
  virtual bool Unload(const std::string& id, brillo::ErrorPtr* err) = 0;
  virtual bool Unload(const UnloadRequest::SelectDlc& select,
                      const base::FilePath& mount_base,
                      brillo::ErrorPtr* err) = 0;

  // Returns the list of DLCs that need to be updated.
  virtual DlcIdList GetDlcsToUpdate() = 0;

  // Persists the verified pref for given DLC(s) on install completion.
  virtual bool InstallCompleted(const DlcIdList& ids,
                                brillo::ErrorPtr* err) = 0;

  // Persists the verified pref for given DLC(s) on update completion.
  virtual bool UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) = 0;
};

// DlcService manages life-cycles of DLCs (Downloadable Content) and provides an
// API for the rest of the system to install/uninstall DLCs.
class DlcService : public DlcServiceInterface {
 public:
  static constexpr base::TimeDelta kUECheckTimeout = base::Seconds(5);

  DlcService(std::unique_ptr<DlcCreatorInterface> dlc_creator,
             std::shared_ptr<UtilsInterface> utils);
  ~DlcService() override;

  void Initialize() override;
  void Install(const InstallRequest& install_request,
               std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>>
                   response) override;
  bool Uninstall(const std::string& id, brillo::ErrorPtr* err) override;
  bool Deploy(const DlcId& id, brillo::ErrorPtr* err) override;
  DlcIdList GetInstalled() override;
  DlcIdList GetExistingDlcs() override;
  bool Unload(const std::string& id, brillo::ErrorPtr* err) override;
  bool Unload(const UnloadRequest::SelectDlc& select,
              const base::FilePath& mount_base,
              brillo::ErrorPtr* err) override;
  DlcInterface* GetDlc(const DlcId& id, brillo::ErrorPtr* err) override;
  DlcIdList GetDlcsToUpdate() override;
  bool InstallCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) override;
  bool UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) override;

  // For testing only.
  void SetSupportedForTesting(DlcMap supported) {
    supported_ = std::move(supported);
  }

 private:
  friend class DlcServiceTest;
  friend class DlcServiceTestLegacy;
  FRIEND_TEST(DlcServiceTestLegacy, InstallCannotSetDlcActiveValue);
  FRIEND_TEST(DlcServiceTestLegacy, OnStatusUpdateSignalTest);
  FRIEND_TEST(DlcServiceTestLegacy, MountFailureTest);
  FRIEND_TEST(DlcServiceTestLegacy, OnStatusUpdateSignalDlcRootTest);
  FRIEND_TEST(DlcServiceTestLegacy, OnStatusUpdateSignalNoRemountTest);
  FRIEND_TEST(DlcServiceTestLegacy, ReportingFailureCleanupTest);
  FRIEND_TEST(DlcServiceTestLegacy, ReportingFailureSignalTest);
  FRIEND_TEST(DlcServiceTestLegacy, SignalToleranceCapTest);
  FRIEND_TEST(DlcServiceTestLegacy, SignalToleranceCapResetTest);
  FRIEND_TEST(DlcServiceTestLegacy, OnStatusUpdateSignalDownloadProgressTest);
  FRIEND_TEST(
      DlcServiceTestLegacy,
      OnStatusUpdateSignalSubsequentialBadOrNonInstalledDlcsNonBlocking);
  FRIEND_TEST(DlcServiceTestLegacy, PeriodicInstallCheck);
  FRIEND_TEST(DlcServiceTestLegacy, InstallUpdateEngineBusyThenFreeTest);
  FRIEND_TEST(DlcServiceTestLegacy, InstallSchedulesPeriodicInstallCheck);
  FRIEND_TEST(DlcServiceTestLegacy, UpdateEngineBecomesAvailable);

  // Install the DLC with ID |id| through update_engine by sending a request to
  // it.
  void InstallWithUpdateEngine(
      const InstallRequest& install_request,
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response);

  // Finishes the currently running installation. Returns true if the
  // installation finished successfully, false otherwise.
  bool FinishInstall(brillo::ErrorPtr* err);
  FRIEND_TEST(DlcServiceTest, FinishInstallTestNothingInstalling);
  FRIEND_TEST(DlcServiceTest, FinishInstallTestUnsupported);
  FRIEND_TEST(DlcServiceTest, FinishInstallTestNotInstalling);
  FRIEND_TEST(DlcServiceTest, FinishInstallTestSuccess);

  // Cancels the currently running installation.
  // The |err_in| argument is the error that causes the install to be cancelled.
  void CancelInstall(const brillo::ErrorPtr& err_in);
  FRIEND_TEST(DlcServiceTest, CancelInstallNoOpTest);
  FRIEND_TEST(DlcServiceTest, CancelInstallNotInstallingResetsTest);
  FRIEND_TEST(DlcServiceTest, CancelInstallDlcCancelFailureResetsTest);
  FRIEND_TEST(DlcServiceTest, CancelInstallResetsTest);

  // Handles status result from update_engine. Returns true if the installation
  // is going fine, false otherwise.
  bool HandleStatusResult(brillo::ErrorPtr* err);

  // The periodic check that runs as a delayed task that checks update_engine
  // status during an install to make sure update_engine is active. This is
  // basically a fallback mechanism in case we miss some of the update_engine's
  // signals so we don't block forever.
  void PeriodicInstallCheck();

  // Schedules the method |PeriodicInstallCheck()| to be ran at a later time,
  void SchedulePeriodicInstallCheck();

  // Gets update_engine's operation status and saves it in |SystemState|.
  void GetUpdateEngineStatusAsync();
  void OnGetUpdateEngineStatusAsyncError(brillo::Error* err);

  // Callback when interacting with update_engine's |InstallAsync| DBus API.
  void OnUpdateEngineInstallAsyncSuccess(
      base::OnceCallback<void(brillo::ErrorPtr)> response_func);
  void OnUpdateEngineInstallAsyncError(
      base::OnceCallback<void(brillo::ErrorPtr)> response_func,
      brillo::Error* err);

  FRIEND_TEST(DlcServiceTest, OnUpdateEngineInstallAsyncErrorNoInstallation);

  // Called on receiving update_engine's |StatusUpdate| signal.
  void OnStatusUpdateAdvancedSignal(
      const update_engine::StatusResult& status_result);

  // Called on being connected to update_engine's |StatusUpdate| signal.
  void OnStatusUpdateAdvancedSignalConnected(const std::string& interface_name,
                                             const std::string& signal_name,
                                             bool success);
  FRIEND_TEST(DlcServiceTest,
              OnStatusUpdateAdvancedSignalConnectedTestVerifyFailureAlert);

  // Called on when update_engine service becomes available.
  void OnWaitForUpdateEngineServiceToBeAvailable(bool available);

  // Removes all unsupported/deprecated DLCs.
  void CleanupUnsupported();
  FRIEND_TEST(DlcServiceTest, CleanupUnsupportedTest);

#if USE_LVM_STATEFUL_PARTITION
  void CleanupUnsupportedLvs();
  FRIEND_TEST(DlcServiceTest, CleanupUnsupportedLvsLvmFailure);
  FRIEND_TEST(DlcServiceTest, CleanupUnsupportedLvsNoDlcs);
  FRIEND_TEST(DlcServiceTest, CleanupUnsupportedLvsAllSupportedDlcs);
  FRIEND_TEST(DlcServiceTest, CleanupUnsupportedLvs);
#endif  // USE_LVM_STATEFUL_PARTITION

  // Holds the DLC that is being installed by update_engine.
  std::optional<DlcId> installing_dlc_id_;

  // Holds the tolerance signal count during an installation.
  size_t tolerance_count_ = 0;

  // Holds the ML task id of the delayed |PeriodicInstallCheck()| if an install
  // is in progress.
  brillo::MessageLoop::TaskId periodic_install_check_id_;

  // Holds the list of supported DLCs.
  DlcMap supported_;

  // Holds the DLC creator.
  std::unique_ptr<DlcCreatorInterface> dlc_creator_;

  // Holds utils.
  std::shared_ptr<UtilsInterface> utils_;

  base::WeakPtrFactory<DlcService> weak_ptr_factory_;

  DlcService(const DlcService&) = delete;
  DlcService& operator=(const DlcService&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_SERVICE_H_
