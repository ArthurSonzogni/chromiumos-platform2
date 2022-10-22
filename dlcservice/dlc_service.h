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
#include <brillo/message_loops/message_loop.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <imageloader/proto_bindings/imageloader.pb.h>
#include <imageloader/dbus-proxies.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <update_engine/dbus-proxies.h>

#include "dlcservice/dlc.h"
#include "dlcservice/dlc_manager.h"
#include "dlcservice/system_state.h"

namespace dlcservice {

class DlcServiceInterface {
 public:
  virtual ~DlcServiceInterface() = default;

  // Initializes the state of dlcservice.
  virtual void Initialize() = 0;
  virtual bool Install(const InstallRequest& install_request,
                       brillo::ErrorPtr* err) = 0;
  virtual bool Uninstall(const std::string& id, brillo::ErrorPtr* err) = 0;
  virtual const DlcBase* GetDlc(const DlcId& id, brillo::ErrorPtr* err) = 0;
  virtual DlcIdList GetInstalled() = 0;
  virtual DlcIdList GetExistingDlcs() = 0;
  virtual DlcIdList GetDlcsToUpdate() = 0;
  virtual bool InstallCompleted(const DlcIdList& ids,
                                brillo::ErrorPtr* err) = 0;
  virtual bool UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) = 0;
};

// DlcService manages life-cycles of DLCs (Downloadable Content) and provides an
// API for the rest of the system to install/uninstall DLCs.
class DlcService : public DlcServiceInterface {
 public:
  static constexpr base::TimeDelta kUECheckTimeout = base::Seconds(5);

  DlcService();
  ~DlcService() override;

  void Initialize() override;
  // Calls |InstallInternal| and sends the metrics for unsuccessful installs.
  bool Install(const InstallRequest& install_request,
               brillo::ErrorPtr* err) override;
  bool Uninstall(const std::string& id, brillo::ErrorPtr* err) override;
  DlcIdList GetInstalled() override;
  DlcIdList GetExistingDlcs() override;
  const DlcBase* GetDlc(const DlcId& id, brillo::ErrorPtr* err) override;
  DlcIdList GetDlcsToUpdate() override;
  bool InstallCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) override;
  bool UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) override;

  void SetDlcManagerForTest(std::unique_ptr<DlcManagerInterface> dlc_manager) {
    dlc_manager_ = std::move(dlc_manager);
  }

 private:
  friend class DlcServiceTest;
  FRIEND_TEST(DlcServiceTest, InstallCannotSetDlcActiveValue);
  FRIEND_TEST(DlcServiceTest, OnStatusUpdateSignalTest);
  FRIEND_TEST(DlcServiceTest, MountFailureTest);
  FRIEND_TEST(DlcServiceTest, OnStatusUpdateSignalDlcRootTest);
  FRIEND_TEST(DlcServiceTest, OnStatusUpdateSignalNoRemountTest);
  FRIEND_TEST(DlcServiceTest, ReportingFailureCleanupTest);
  FRIEND_TEST(DlcServiceTest, ReportingFailureSignalTest);
  FRIEND_TEST(DlcServiceTest, SignalToleranceCapTest);
  FRIEND_TEST(DlcServiceTest, SignalToleranceCapResetTest);
  FRIEND_TEST(DlcServiceTest, OnStatusUpdateSignalDownloadProgressTest);
  FRIEND_TEST(
      DlcServiceTest,
      OnStatusUpdateSignalSubsequentialBadOrNonInstalledDlcsNonBlocking);
  FRIEND_TEST(DlcServiceTest, PeriodicInstallCheck);
  FRIEND_TEST(DlcServiceTest, InstallUpdateEngineBusyThenFreeTest);
  FRIEND_TEST(DlcServiceTest, InstallSchedulesPeriodicInstallCheck);
  FRIEND_TEST(DlcServiceTest, UpdateEngineBecomesAvailable);

  // Install the DLC with ID |id| through update_engine by sending a request to
  // it.
  bool InstallWithUpdateEngine(const InstallRequest& install_request,
                               brillo::ErrorPtr* err);

  // Finishes the currently running installation. Returns true if the
  // installation finished successfully, false otherwise.
  bool FinishInstall(brillo::ErrorPtr* err);

  // Cancels the currently running installation.
  // The |err_in| argument is the error that causes the install to be cancelled.
  void CancelInstall(const brillo::ErrorPtr& err_in);

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
  bool GetUpdateEngineStatus();

  // Installs a DLC without sending metrics when the install fails.
  bool InstallInternal(const InstallRequest& install_request,
                       brillo::ErrorPtr* err);

  // Called on receiving update_engine's |StatusUpdate| signal.
  void OnStatusUpdateAdvancedSignal(
      const update_engine::StatusResult& status_result);

  // Called on being connected to update_engine's |StatusUpdate| signal.
  void OnStatusUpdateAdvancedSignalConnected(const std::string& interface_name,
                                             const std::string& signal_name,
                                             bool success);

  // Called on when update_engine service becomes available.
  void OnWaitForUpdateEngineServiceToBeAvailable(bool available);

  // Holds the DLC that is being installed by update_engine.
  std::optional<DlcId> installing_dlc_id_;

  // Holds the tolerance signal count during an installation.
  size_t tolerance_count_ = 0;

  std::unique_ptr<DlcManagerInterface> dlc_manager_;

  // Holds the ML task id of the delayed |PeriodicInstallCheck()| if an install
  // is in progress.
  brillo::MessageLoop::TaskId periodic_install_check_id_;

  base::WeakPtrFactory<DlcService> weak_ptr_factory_;

  DlcService(const DlcService&) = delete;
  DlcService& operator=(const DlcService&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_SERVICE_H_
