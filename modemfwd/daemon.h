// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_DAEMON_H_
#define MODEMFWD_DAEMON_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/timer/timer.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/process/process.h>
#include <dbus/bus.h>

#include "modemfwd/daemon_delegate.h"
#include "modemfwd/dbus_adaptors/org.chromium.Modemfwd.h"
#include "modemfwd/dlc_manager.h"
#include "modemfwd/heartbeat_task.h"
#include "modemfwd/journal.h"
#include "modemfwd/metrics.h"
#include "modemfwd/modem.h"
#include "modemfwd/modem_flasher.h"
#include "modemfwd/modem_helper.h"
#include "modemfwd/modem_sandbox.h"
#include "modemfwd/modem_tracker.h"
#include "modemfwd/notification_manager.h"
#include "modemfwd/prefs.h"
#include "modemfwd/suspend_checker.h"

namespace modemfwd {

class DBusAdaptor : public org::chromium::ModemfwdInterface,
                    public org::chromium::ModemfwdAdaptor {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus, Delegate* delegate);
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  // org::chromium::ModemfwdInterface overrides.
  void SetDebugMode(bool debug_mode) override;
  bool ForceFlash(const std::string& device_id,
                  const brillo::VariantDictionary& args) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
  Delegate* delegate_;  // weak
};

class Daemon : public brillo::DBusServiceDaemon, public Delegate {
 public:
  // Constructor for Daemon which loads from already set-up
  // directories.
  Daemon(const std::string& journal_file,
         const std::string& helper_directory,
         const std::string& firmware_directory);
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

  ~Daemon() override = default;

  // Delegate overrides.
  bool ForceFlashForTesting(const std::string& device_id,
                            const std::string& carrier_uuid,
                            const std::string& variant,
                            bool use_modems_fw_info) override;
  bool ResetModem(const std::string& device_id) override;
  void RegisterOnModemReappearanceCallback(const std::string& equipment_id,
                                           base::OnceClosure callback) override;

 protected:
  // brillo::Daemon overrides.
  int OnInit() override;

  // brillo::DBusServiceDaemon overrides.
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Force-flash the modem (with generic firmware).
  bool ForceFlash(const std::string& device_id);

 private:
  // Once we have a path for the firmware directory we can parse
  // the manifest and set up the DLC manager.
  int SetupFirmwareDirectory();

  // Setup the journal, flasher, and post delayed tasks.
  void CompleteInitialization();

  // Install DLC callback.
  void InstallDlcCompleted(const std::string&, const brillo::Error*);

  void RunModemReappearanceCallback(const std::string& equipment_id);

  // Called when a modem gets its home operator carrier ID and might
  // need a new main firmware or carrier customization.
  // Generally this means on startup but can also be called in response
  // to e.g. rebooting the modem or SIM hot swapping.
  void OnModemCarrierIdReady(
      std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> modem);
  // Called when a modem device is seen (detected by ModemManager)
  // Possibly called multiple times.
  void OnModemDeviceSeen(std::string device_id, std::string equipment_id);

  // Update state of the modem with |device_id|
  void OnModemStateChange(std::string device_id, Modem::State new_state);

  // Update power state of the modem with |device_id|
  void OnModemPowerStateChange(std::string device_id,
                               Modem::PowerState new_power_state);

  // Try to flash a modem.
  void DoFlash(const std::string& device_id, const std::string& equipment_id);

  // Check if modem is in Flash mode and force-flash them if necessary.
  void ForceFlashIfInFlashMode(const std::string& device_id,
                               ModemHelper* modem_helper);
  // Check for wedged modems and force-flash them if necessary.
  void CheckForWedgedModems();
  void ForceFlashIfWedged(const std::string& device_id,
                          ModemHelper* modem_helper);
  void ForceFlashIfNeverAppeared(const std::string& device_id);

  void SetupHeartbeatTask(const std::string& device_id);
  void StartHeartbeatTask(const std::string& device_id);
  void StopHeartbeatTask(const std::string& device_id);

  base::FilePath journal_file_path_;
  base::FilePath helper_dir_path_;
  base::FilePath fw_manifest_dir_path_;

  std::string variant_;
  std::unique_ptr<DlcManager> dlc_manager_;
  std::unique_ptr<FirmwareDirectory> fw_manifest_directory_;
  std::unique_ptr<FirmwareIndex> fw_index_;
  std::unique_ptr<ModemHelperDirectory> helper_directory_;
  std::unique_ptr<Metrics> metrics_;
  std::unique_ptr<Journal> journal_;
  std::unique_ptr<Prefs> prefs_;
  std::unique_ptr<Prefs> modems_seen_since_oobe_prefs_;

  std::map<std::string, std::unique_ptr<Modem>> modems_;
  std::map<std::string, std::unique_ptr<HeartbeatTask>> heartbeat_tasks_;

  std::unique_ptr<ModemTracker> modem_tracker_;
  std::unique_ptr<ModemFlasher> modem_flasher_;
  std::unique_ptr<NotificationManager> notification_mgr_;
  std::unique_ptr<SuspendChecker> suspend_checker_;

  std::map<std::string, base::OnceClosure> modem_reappear_callbacks_;
  std::set<std::string> device_ids_seen_;

  std::unique_ptr<DBusAdaptor> dbus_adaptor_;

  base::WeakPtrFactory<Daemon> weak_ptr_factory_;
};

}  // namespace modemfwd

#endif  // MODEMFWD_DAEMON_H_
