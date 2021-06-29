// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_BIOMETRICS_MANAGER_H_
#define BIOD_CROS_FP_BIOMETRICS_MANAGER_H_

#include "biod/biometrics_manager.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/values.h>
#include <dbus/bus.h>
#include <base/timer/timer.h>

#include "biod/biod_storage.h"
#include "biod/cros_fp_device.h"
#include "biod/power_button_filter_interface.h"

namespace biod {

class BiodMetrics;

class CrosFpBiometricsManager : public BiometricsManager {
 public:
  CrosFpBiometricsManager(
      std::unique_ptr<PowerButtonFilterInterface> power_button_filter,
      std::unique_ptr<CrosFpDeviceInterface> cros_fp_device,
      std::unique_ptr<BiodMetricsInterface> biod_metrics,
      std::unique_ptr<BiodStorageInterface> biod_storage);
  CrosFpBiometricsManager(const CrosFpBiometricsManager&) = delete;
  CrosFpBiometricsManager& operator=(const CrosFpBiometricsManager&) = delete;

  // BiometricsManager overrides:
  ~CrosFpBiometricsManager() override;

  BiometricType GetType() override;
  BiometricsManager::EnrollSession StartEnrollSession(
      std::string user_id, std::string label) override;
  BiometricsManager::AuthSession StartAuthSession() override;
  std::vector<std::unique_ptr<BiometricsManagerRecord>> GetRecords() override;
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

  bool ResetEntropy(bool factory_init) override;

 protected:
  void EndEnrollSession() override;
  void EndAuthSession() override;

  virtual void OnMaintenanceTimerFired();
  virtual bool WriteRecord(const BiometricsManagerRecord& record,
                           uint8_t* tmpl_data,
                           size_t tmpl_size);

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

 private:
  // For testing.
  friend class CrosFpBiometricsManagerPeer;

  using SessionAction = base::RepeatingCallback<void(const uint32_t event)>;

  class Record : public BiometricsManagerRecord {
   public:
    Record(const base::WeakPtr<CrosFpBiometricsManager>& biometrics_manager,
           int index)
        : biometrics_manager_(biometrics_manager), index_(index) {}

    // BiometricsManager::Record overrides:
    const std::string& GetId() const override;
    const std::string& GetUserId() const override;
    const std::string& GetLabel() const override;
    const std::vector<uint8_t>& GetValidationVal() const override;
    bool SetLabel(std::string label) override;
    bool Remove() override;
    bool SupportsPositiveMatchSecret() const override;

   private:
    base::WeakPtr<CrosFpBiometricsManager> biometrics_manager_;
    int index_;
  };

  void OnEnrollScanDone(ScanResult result,
                        const BiometricsManager::EnrollStatus& enroll_status);
  void OnAuthScanDone(ScanResult result,
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
  bool ValidationValueIsCorrect(uint32_t match_idx);
  BiometricsManager::AttemptMatches CalculateMatches(int match_idx,
                                                     bool matched);

  void KillMcuSession();

  void OnTaskComplete();

  // Clear FPMCU context and re-upload all records from storage.
  bool ReloadAllRecords(std::string user_id);
  // BiodMetrics must come before CrosFpDevice, since CrosFpDevice has a
  // raw pointer to BiodMetrics. We must ensure CrosFpDevice is destructed
  // first.
  std::unique_ptr<BiodMetricsInterface> biod_metrics_;
  std::unique_ptr<CrosFpDeviceInterface> cros_dev_;

  SessionAction next_session_action_;

  // This list of records should be matching the templates loaded on the MCU.
  std::vector<BiodStorageInterface::RecordMetadata> records_;

  // Set of templates that came with a wrong validation value in matching.
  std::unordered_set<uint32_t> suspicious_templates_;

  BiometricsManager::EnrollScanDoneCallback on_enroll_scan_done_;
  BiometricsManager::AuthScanDoneCallback on_auth_scan_done_;
  BiometricsManager::SessionFailedCallback on_session_failed_;

  base::WeakPtrFactory<CrosFpBiometricsManager> session_weak_factory_;
  base::WeakPtrFactory<CrosFpBiometricsManager> weak_factory_;

  std::unique_ptr<PowerButtonFilterInterface> power_button_filter_;

  std::unique_ptr<BiodStorageInterface> biod_storage_;

  bool use_positive_match_secret_;

  std::unique_ptr<base::RepeatingTimer> maintenance_timer_;
};

}  // namespace biod

#endif  // BIOD_CROS_FP_BIOMETRICS_MANAGER_H_
