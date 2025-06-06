// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_FAKE_SYSTEM_STATE_H_
#define UPDATE_ENGINE_CROS_FAKE_SYSTEM_STATE_H_

#include <memory>

#include <base/logging.h>
#include <gmock/gmock.h>
#include <policy/mock_device_policy.h>

#include "metrics/metrics_library_mock.h"
#include "update_engine/common/fake_boot_control.h"
#include "update_engine/common/fake_clock.h"
#include "update_engine/common/fake_cros_healthd.h"
#include "update_engine/common/fake_hardware.h"
#include "update_engine/common/fake_prefs.h"
#include "update_engine/common/mock_call_wrapper.h"
#include "update_engine/common/mock_metrics_reporter.h"
#include "update_engine/common/mock_prefs.h"
#include "update_engine/common/system_state.h"
#include "update_engine/cros/mock_connection_manager.h"
#include "update_engine/cros/mock_dlc_utils.h"
#include "update_engine/cros/mock_omaha_request_params.h"
#include "update_engine/cros/mock_p2p_manager.h"
#include "update_engine/cros/mock_payload_state.h"
#include "update_engine/cros/mock_power_manager.h"
#include "update_engine/cros/mock_update_attempter.h"
#include "update_engine/update_manager/fake_update_manager.h"

namespace chromeos_update_engine {

// Mock the SystemStateInterface so that we could lie that
// OOBE is completed even when there's no such marker file, etc.
class FakeSystemState : public SystemState {
 public:
  static void CreateInstance() {
    static std::unique_ptr<FakeSystemState> system_state;
    system_state.reset(new FakeSystemState());
    g_pointer_ = system_state.get();
  }

  static FakeSystemState* Get() {
    return reinterpret_cast<FakeSystemState*>(g_pointer_);
  }

  // Base class overrides. All getters return the current implementation of
  // various members, either the default (fake/mock) or the one set to override
  // it by client code.

  BootControlInterface* boot_control() override { return boot_control_; }

  inline ClockInterface* clock() override { return clock_; }

  inline void set_device_policy(
      const policy::DevicePolicy* device_policy) override {
    device_policy_ = device_policy;
  }

  inline const policy::DevicePolicy* device_policy() override {
    return device_policy_;
  }

  inline ConnectionManagerInterface* connection_manager() override {
    return connection_manager_;
  }

  inline HardwareInterface* hardware() override { return hardware_; }

  inline MetricsReporterInterface* metrics_reporter() override {
    CHECK(metrics_reporter_ != nullptr);
    return metrics_reporter_;
  }

  inline PrefsInterface* prefs() override { return prefs_; }

  inline PrefsInterface* powerwash_safe_prefs() override {
    return powerwash_safe_prefs_;
  }

  inline PayloadStateInterface* payload_state() override {
    return payload_state_;
  }

  inline UpdateAttempter* update_attempter() override {
    return update_attempter_;
  }

  inline OmahaRequestParams* request_params() override {
    return request_params_;
  }

  inline P2PManager* p2p_manager() override { return p2p_manager_; }

  inline chromeos_update_manager::UpdateManager* update_manager() override {
    return update_manager_;
  }

  inline PowerManagerInterface* power_manager() override {
    return power_manager_;
  }

  inline DlcServiceInterface* dlcservice() override { return dlcservice_; }

  inline DlcUtilsInterface* dlc_utils() override { return dlc_utils_; }

  inline CrosHealthdInterface* cros_healthd() override { return cros_healthd_; }

  inline CallWrapperInterface* call_wrapper() override { return call_wrapper_; }

  inline bool system_rebooted() override { return fake_system_rebooted_; }

  // Setters for the various members, can be used for overriding the default
  // implementations. For convenience, setting to a null pointer will restore
  // the default implementation.

  void set_boot_control(BootControlInterface* boot_control) {
    boot_control_ = boot_control ? boot_control : &fake_boot_control_;
  }

  inline void set_clock(ClockInterface* clock) {
    clock_ = clock ? clock : &fake_clock_;
  }

  inline void set_connection_manager(
      ConnectionManagerInterface* connection_manager) {
    connection_manager_ =
        (connection_manager ? connection_manager : &mock_connection_manager_);
  }

  inline void set_hardware(HardwareInterface* hardware) {
    hardware_ = hardware ? hardware : &fake_hardware_;
  }

  inline void set_metrics_reporter(MetricsReporterInterface* metrics_reporter) {
    metrics_reporter_ =
        metrics_reporter ? metrics_reporter : &mock_metrics_reporter_;
  }

  inline void set_prefs(PrefsInterface* prefs) {
    prefs_ = prefs ? prefs : &mock_prefs_;
  }

  inline void set_powerwash_safe_prefs(PrefsInterface* powerwash_safe_prefs) {
    powerwash_safe_prefs_ =
        (powerwash_safe_prefs ? powerwash_safe_prefs
                              : &mock_powerwash_safe_prefs_);
  }

  inline void set_payload_state(PayloadStateInterface* payload_state) {
    payload_state_ = payload_state ? payload_state : &mock_payload_state_;
  }

  inline void set_update_attempter(UpdateAttempter* update_attempter) {
    update_attempter_ =
        (update_attempter ? update_attempter : &mock_update_attempter_);
  }

  inline void set_request_params(OmahaRequestParams* request_params) {
    request_params_ = (request_params ? request_params : &mock_request_params_);
  }

  inline void set_p2p_manager(P2PManager* p2p_manager) {
    p2p_manager_ = p2p_manager ? p2p_manager : &mock_p2p_manager_;
  }

  inline void set_update_manager(
      chromeos_update_manager::UpdateManager* update_manager) {
    update_manager_ = update_manager ? update_manager : &fake_update_manager_;
  }

  inline void set_system_rebooted(bool system_rebooted) {
    fake_system_rebooted_ = system_rebooted;
  }

  inline void set_dlcservice(DlcServiceInterface* dlcservice) {
    dlcservice_ = dlcservice;
  }

  inline void set_dlc_utils(DlcUtilsInterface* dlc_utils) {
    dlc_utils_ = dlc_utils;
  }

  inline void set_cros_healthd(CrosHealthdInterface* cros_healthd) {
    cros_healthd_ = (cros_healthd ? cros_healthd : &fake_cros_healthd_);
  }

  inline void set_call_wrapper(CallWrapperInterface* call_wrapper) {
    call_wrapper_ = call_wrapper;
  }

  inline testing::StrictMock<MockCallWrapper>* mock_call_wrapper() {
    CHECK(call_wrapper_ == &mock_call_wrapper_);
    return &mock_call_wrapper_;
  }

  // Getters for the built-in default implementations. These return the actual
  // concrete type of each implementation. For additional safety, they will fail
  // whenever the requested default was overridden by a different
  // implementation.

  inline FakeBootControl* fake_boot_control() {
    CHECK(boot_control_ == &fake_boot_control_);
    return &fake_boot_control_;
  }

  inline FakeClock* fake_clock() {
    CHECK(clock_ == &fake_clock_);
    return &fake_clock_;
  }

  inline testing::NiceMock<MockConnectionManager>* mock_connection_manager() {
    CHECK(connection_manager_ == &mock_connection_manager_);
    return &mock_connection_manager_;
  }

  inline FakeHardware* fake_hardware() {
    CHECK(hardware_ == &fake_hardware_);
    return &fake_hardware_;
  }

  inline FakePrefs* fake_prefs() {
    CHECK(prefs_ == &fake_prefs_);
    return &fake_prefs_;
  }

  inline FakePrefs* fake_powerwash_safe_prefs() {
    CHECK(powerwash_safe_prefs_ == &fake_powerwash_safe_prefs_);
    return &fake_powerwash_safe_prefs_;
  }

  inline testing::NiceMock<MockMetricsReporter>* mock_metrics_reporter() {
    CHECK(metrics_reporter_ == &mock_metrics_reporter_);
    return &mock_metrics_reporter_;
  }

  inline testing::NiceMock<MockPrefs>* mock_prefs() {
    CHECK(prefs_ == &mock_prefs_);
    return &mock_prefs_;
  }

  inline testing::NiceMock<MockPrefs>* mock_powerwash_safe_prefs() {
    CHECK(powerwash_safe_prefs_ == &mock_powerwash_safe_prefs_);
    return &mock_powerwash_safe_prefs_;
  }

  inline testing::NiceMock<MockPayloadState>* mock_payload_state() {
    CHECK(payload_state_ == &mock_payload_state_);
    return &mock_payload_state_;
  }

  inline testing::NiceMock<MockUpdateAttempter>* mock_update_attempter() {
    CHECK(update_attempter_ == &mock_update_attempter_);
    return &mock_update_attempter_;
  }

  inline testing::NiceMock<MockOmahaRequestParams>* mock_request_params() {
    CHECK(request_params_ == &mock_request_params_);
    return &mock_request_params_;
  }

  inline testing::NiceMock<MockP2PManager>* mock_p2p_manager() {
    CHECK(p2p_manager_ == &mock_p2p_manager_);
    return &mock_p2p_manager_;
  }

  inline chromeos_update_manager::FakeUpdateManager* fake_update_manager() {
    CHECK(update_manager_ == &fake_update_manager_);
    return &fake_update_manager_;
  }

  inline FakeCrosHealthd* fake_cros_healthd() {
    CHECK(cros_healthd_ == &fake_cros_healthd_);
    return &fake_cros_healthd_;
  }

 private:
  // Don't allow for direct initialization of this class.
  FakeSystemState();

  // Default mock/fake implementations (owned).
  chromeos_update_manager::FakeUpdateManager fake_update_manager_;
  FakeBootControl fake_boot_control_;
  FakeClock fake_clock_;
  FakeHardware fake_hardware_;
  FakePrefs fake_prefs_;
  FakePrefs fake_powerwash_safe_prefs_;
  FakeCrosHealthd fake_cros_healthd_;

  testing::NiceMock<MockConnectionManager> mock_connection_manager_;
  testing::NiceMock<MockMetricsReporter> mock_metrics_reporter_;
  testing::NiceMock<MockPrefs> mock_prefs_;
  testing::NiceMock<MockPrefs> mock_powerwash_safe_prefs_;
  testing::NiceMock<MockPayloadState> mock_payload_state_;
  testing::NiceMock<MockUpdateAttempter> mock_update_attempter_;
  testing::NiceMock<MockOmahaRequestParams> mock_request_params_;
  testing::NiceMock<MockP2PManager> mock_p2p_manager_;
  testing::NiceMock<MockPowerManager> mock_power_manager_;
  testing::StrictMock<MockCallWrapper> mock_call_wrapper_;
  testing::StrictMock<MockDlcUtils> mock_dlc_utils_;

  // Pointers to objects that client code can override. They are initialized to
  // the default implementations above.
  BootControlInterface* boot_control_{&fake_boot_control_};
  ClockInterface* clock_;
  ConnectionManagerInterface* connection_manager_;
  HardwareInterface* hardware_;
  MetricsReporterInterface* metrics_reporter_;
  PrefsInterface* prefs_;
  PrefsInterface* powerwash_safe_prefs_;
  PayloadStateInterface* payload_state_;
  UpdateAttempter* update_attempter_;
  OmahaRequestParams* request_params_;
  P2PManager* p2p_manager_;
  chromeos_update_manager::UpdateManager* update_manager_;
  PowerManagerInterface* power_manager_{&mock_power_manager_};
  DlcServiceInterface* dlcservice_;
  CrosHealthdInterface* cros_healthd_{&fake_cros_healthd_};
  CallWrapperInterface* call_wrapper_;
  DlcUtilsInterface* dlc_utils_;

  // Other object pointers (not preinitialized).
  const policy::DevicePolicy* device_policy_;

  // Other data members.
  bool fake_system_rebooted_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_FAKE_SYSTEM_STATE_H_
