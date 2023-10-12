// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_BIOMETRICS_MANAGER_H_
#define BIOD_CROS_FP_BIOMETRICS_MANAGER_H_

#include "biod/biometrics_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/values.h>
#include <dbus/bus.h>

#include "biod/cros_fp_device.h"
#include "biod/cros_fp_record_manager.h"
#include "biod/maintenance_scheduler.h"
#include "biod/power_button_filter_interface.h"

namespace biod {

class BiodMetrics;

class CrosFpBiometricsManager : public BiometricsManager {
 public:
  CrosFpBiometricsManager(
      std::unique_ptr<PowerButtonFilterInterface> power_button_filter,
      std::unique_ptr<ec::CrosFpDeviceInterface> cros_fp_device,
      BiodMetricsInterface* biod_metrics,
      std::unique_ptr<CrosFpRecordManagerInterface> record_manager);
  CrosFpBiometricsManager(const CrosFpBiometricsManager&) = delete;
  CrosFpBiometricsManager& operator=(const CrosFpBiometricsManager&) = delete;

  // BiometricsManager overrides:
  ~CrosFpBiometricsManager() override;

  BiometricType GetType() override;
  BiometricsManager::EnrollSession StartEnrollSession(
      std::string user_id, std::string label) override;
  BiometricsManager::AuthSession StartAuthSession() override;
  std::vector<std::unique_ptr<BiometricsManagerRecordInterface>>
  GetLoadedRecords() override;
  bool DestroyAllRecords() override;
  void RemoveRecordsFromMemory() override;
  bool ReadRecordsForSingleUser(const std::string& user_id) override;

  void SetEnrollScanDoneHandler(const BiometricsManager::EnrollScanDoneCallback&
                                    on_enroll_scan_done) override;
  void SetAuthScanDoneHandler(const BiometricsManager::AuthScanDoneCallback&
                                  on_auth_scan_done) override;
  void SetSessionFailedHandler(const BiometricsManager::SessionFailedCallback&
                                   on_session_failed) override;

  bool SendStatsOnLogin() override;

  void SetDiskAccesses(bool allow) override;

  bool ResetSensor() override;

  // Returns RecordMetadata for given record.
  virtual std::optional<BiodStorageInterface::RecordMetadata> GetRecordMetadata(
      const std::string& record_id) const;

  // Clear FPMCU context and re-upload all records from storage.
  bool ReloadAllRecords(std::string user_id);

  // Updates record metadata on disk.
  bool UpdateRecordMetadata(
      const BiodStorageInterface::RecordMetadata& record_metadata);

  // Removes record from disk and from FPMCU.
  bool RemoveRecord(const std::string& id);

 protected:
  void EndEnrollSession() override;
  void EndAuthSession() override;

  // Returns RecordId for given template id.
  virtual std::optional<std::string> GetLoadedRecordId(int id);

  std::vector<int> GetDirtyList();
  /**
   * @param dirty_list            templates that have been updated on the
   * FPMCU, but not written to disk.
   * @param suspicious_templates  templates that have incorrect validation
   * values.
   * @return True if all templates were successfully written to disk. False
   * otherwise.
   */
  bool UpdateTemplatesOnDisk(
      const std::vector<int>& dirty_list,
      const std::unordered_set<uint32_t>& suspicious_templates);

  bool LoadRecord(const BiodStorage::Record record);

  base::WeakPtrFactory<CrosFpBiometricsManager> session_weak_factory_;
  base::WeakPtrFactory<CrosFpBiometricsManager> weak_factory_;

 private:
  // For testing.
  friend class CrosFpBiometricsManagerPeer;

  using SessionAction = base::RepeatingCallback<void(const uint32_t event)>;

  void OnEnrollScanDone(ScanResult result,
                        const BiometricsManager::EnrollStatus& enroll_status);
  void OnAuthScanDone(FingerprintMessage result,
                      const BiometricsManager::AttemptMatches& matches);
  void OnSessionFailed();

  void OnMkbpEvent(uint32_t event);

  // Request an action from the Fingerprint MCU and set the appropriate callback
  // when the event with the result will arrive.
  bool RequestEnrollImage(BiodStorageInterface::RecordMetadata record);
  bool RequestEnrollFingerUp(BiodStorageInterface::RecordMetadata record);
  bool RequestMatch(int attempt = 0);
  bool RequestMatchFingerUp();

  // Actions taken when the corresponding Fingerprint MKBP events happen.
  void DoEnrollImageEvent(BiodStorageInterface::RecordMetadata record,
                          uint32_t event);
  void DoEnrollFingerUpEvent(BiodStorageInterface::RecordMetadata record,
                             uint32_t event);
  void DoMatchEvent(int attempt, uint32_t event);
  void DoMatchFingerUpEvent(uint32_t event);
  bool CheckPositiveMatchSecret(const std::string& record_id, int match_idx);

  void KillMcuSession();

  void OnTaskComplete();

  BiodMetricsInterface* biod_metrics_ = nullptr;  // Not owned.
  std::unique_ptr<ec::CrosFpDeviceInterface> cros_dev_;

  SessionAction next_session_action_;

  // This vector contains RecordIds of templates loaded into the MCU.
  std::vector<std::string> loaded_records_;

  // Set of templates that came with a wrong validation value in matching.
  std::unordered_set<uint32_t> suspicious_templates_;

  BiometricsManager::EnrollScanDoneCallback on_enroll_scan_done_;
  BiometricsManager::AuthScanDoneCallback on_auth_scan_done_;
  BiometricsManager::SessionFailedCallback on_session_failed_;

  std::unique_ptr<PowerButtonFilterInterface> power_button_filter_;

  std::unique_ptr<CrosFpRecordManagerInterface> record_manager_;

  std::unique_ptr<MaintenanceScheduler> maintenance_scheduler_;

  uint8_t num_enrollment_captures_ = 0;
};

}  // namespace biod

#endif  // BIOD_CROS_FP_BIOMETRICS_MANAGER_H_
