// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/system_state.h"

#include <climits>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "dlcservice/boot/boot_device.h"
#include "dlcservice/state_change_reporter_interface.h"

namespace dlcservice {

std::unique_ptr<SystemState> SystemState::g_instance_ = nullptr;

SystemState::SystemState(
#if USE_LVM_STATEFUL_PARTITION
    std::unique_ptr<LvmdProxyWrapperInterface> lvmd_proxy_wrapper,
#endif  // USE_LVM_STATEFUL_PARTITION
    std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
        image_loader_proxy,
    std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
        update_engine_proxy,
    std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
        session_manager_proxy,
    StateChangeReporterInterface* state_change_reporter,
    std::unique_ptr<BootSlotInterface> boot_slot,
    std::unique_ptr<Metrics> metrics,
    std::unique_ptr<SystemProperties> system_properties,
    const base::FilePath& manifest_dir,
    const base::FilePath& preloaded_content_dir,
    const base::FilePath& factory_install_dir,
    const base::FilePath& content_dir,
    const base::FilePath& prefs_dir,
    const base::FilePath& users_dir,
    const base::FilePath& verification_file,
    base::Clock* clock)
    :
#if USE_LVM_STATEFUL_PARTITION
      lvmd_proxy_wrapper_(std::move(lvmd_proxy_wrapper)),
#endif  // USE_LVM_STATEFUL_PARTITION
      image_loader_proxy_(std::move(image_loader_proxy)),
      update_engine_proxy_(std::move(update_engine_proxy)),
      session_manager_proxy_(std::move(session_manager_proxy)),
      state_change_reporter_(state_change_reporter),
      boot_slot_(std::move(boot_slot)),
      metrics_(std::move(metrics)),
      system_properties_(std::move(system_properties)),
      manifest_dir_(manifest_dir),
      preloaded_content_dir_(preloaded_content_dir),
      factory_install_dir_(factory_install_dir),
      content_dir_(content_dir),
      prefs_dir_(prefs_dir),
      users_dir_(users_dir),
      verification_file_(verification_file),
      clock_(clock) {
}

// static
void SystemState::Initialize(
#if USE_LVM_STATEFUL_PARTITION
    std::unique_ptr<LvmdProxyWrapperInterface> lvmd_proxy_wrapper,
#endif  // USE_LVM_STATEFUL_PARTITION
    std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
        image_loader_proxy,
    std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
        update_engine_proxy,
    std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
        session_manager_proxy,
    StateChangeReporterInterface* state_change_reporter,
    std::unique_ptr<BootSlotInterface> boot_slot,
    std::unique_ptr<Metrics> metrics,
    std::unique_ptr<SystemProperties> system_properties,
    const base::FilePath& manifest_dir,
    const base::FilePath& preloaded_content_dir,
    const base::FilePath& factory_install_dir,
    const base::FilePath& content_dir,
    const base::FilePath& prefs_dir,
    const base::FilePath& users_dir,
    const base::FilePath& verification_file,
    base::Clock* clock,
    bool for_test) {
  if (!for_test)
    CHECK(!g_instance_) << "SystemState::Initialize() called already.";
  g_instance_.reset(new SystemState(
#if USE_LVM_STATEFUL_PARTITION
      std::move(lvmd_proxy_wrapper),
#endif  // USE_LVM_STATEFUL_PARTITION
      std::move(image_loader_proxy), std::move(update_engine_proxy),
      std::move(session_manager_proxy), state_change_reporter,
      std::move(boot_slot), std::move(metrics), std::move(system_properties),
      manifest_dir, preloaded_content_dir, factory_install_dir, content_dir,
      prefs_dir, users_dir, verification_file, clock));
}

// static
SystemState* SystemState::Get() {
  CHECK(g_instance_);
  return g_instance_.get();
}

bool SystemState::IsUpdateEngineServiceAvailable() const {
  return update_engine_service_available_;
}

void SystemState::set_update_engine_service_available(bool available) {
  update_engine_service_available_ = available;
}

#if USE_LVM_STATEFUL_PARTITION
LvmdProxyWrapperInterface* SystemState::lvmd_wrapper() const {
  return lvmd_proxy_wrapper_.get();
}
#endif  // USE_LVM_STATEFUL_PARTITION

org::chromium::ImageLoaderInterfaceProxyInterface* SystemState::image_loader()
    const {
  return image_loader_proxy_.get();
}

org::chromium::UpdateEngineInterfaceProxyInterface* SystemState::update_engine()
    const {
  return update_engine_proxy_.get();
}

org::chromium::SessionManagerInterfaceProxyInterface*
SystemState::session_manager() const {
  return session_manager_proxy_.get();
}

Metrics* SystemState::metrics() const {
  return metrics_.get();
}

SystemProperties* SystemState::system_properties() const {
  return system_properties_.get();
}

StateChangeReporterInterface* SystemState::state_change_reporter() const {
  return state_change_reporter_;
}

BootSlotInterface* SystemState::boot_slot() const {
  return boot_slot_.get();
}

BootSlotInterface::Slot SystemState::active_boot_slot() const {
  return boot_slot()->GetSlot();
}

BootSlotInterface::Slot SystemState::inactive_boot_slot() const {
  switch (active_boot_slot()) {
    case BootSlotInterface::Slot::A:
      return BootSlotInterface::Slot::B;
    case BootSlotInterface::Slot::B:
      return BootSlotInterface::Slot::A;
  }
}

bool SystemState::IsDeviceRemovable() const {
  return boot_slot()->IsDeviceRemovable();
}

const base::FilePath& SystemState::manifest_dir() const {
  return manifest_dir_;
}

const base::FilePath& SystemState::preloaded_content_dir() const {
  return preloaded_content_dir_;
}

const base::FilePath& SystemState::factory_install_dir() const {
  return factory_install_dir_;
}

const base::FilePath& SystemState::content_dir() const {
  return content_dir_;
}

const base::FilePath& SystemState::prefs_dir() const {
  return prefs_dir_;
}

base::FilePath SystemState::dlc_prefs_dir() const {
  return prefs_dir_.Append("dlc");
}

const base::FilePath& SystemState::users_dir() const {
  return users_dir_;
}

const base::FilePath& SystemState::verification_file() const {
  return verification_file_;
}

base::Clock* SystemState::clock() const {
  return clock_;
}

void SystemState::set_update_engine_status(
    const update_engine::StatusResult& status) {
  last_update_engine_status_ = status;
  last_update_engine_status_timestamp_ = clock_->Now();
}

const update_engine::StatusResult& SystemState::update_engine_status() {
  return last_update_engine_status_;
}

const base::Time& SystemState::update_engine_status_timestamp() {
  return last_update_engine_status_timestamp_;
}

}  // namespace dlcservice
