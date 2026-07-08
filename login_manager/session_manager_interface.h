// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_SESSION_MANAGER_INTERFACE_H_
#define LOGIN_MANAGER_SESSION_MANAGER_INTERFACE_H_

#include <string>
#include <vector>

namespace login_manager {

// Identifies the trigger for initiating a device wipe (powerwash).
// Each reason maps to specific clobber command arguments and an annotation
// string persisted to clobber.log.
enum class WipeReason {
  // Initiated via D-Bus request (e.g., user-triggered reset from UI/OOBE).
  kSessionManagerDBusRequest,
  // Remote device wipe initiated by an admin without config saving.
  kRemoteWipe,
  // Remote device wipe that preserves network config and OOBE state across
  // reboot.
  kRemoteWipePreserveConfig,
  // TPM firmware update wipe on first boot.
  kTpmFirmwareUpdateFirstBoot,
  // TPM firmware update cleanup wipe.
  kTpmFirmwareUpdateCleanup,
  // Initiated due to a corrupt or invalid policy key.
  kBadPolicyKey,
};

class SessionManagerInterface {
 public:
  SessionManagerInterface() = default;
  virtual ~SessionManagerInterface() {}

  // Initializes policy subsystems. Failure to initialize must be fatal.
  // Note: Initialize() does not start D-Bus service, yet.
  virtual bool Initialize() = 0;
  virtual void Finalize() = 0;

  // Starts SessionManagerInterface D-Bus service.
  // Returns true on success. Failure to start must be fatal.
  virtual bool StartDBusService() = 0;

  // Gets feature flags specified in device settings to pass to Chrome on
  // startup.
  virtual std::vector<std::string> GetFeatureFlags() = 0;

  // Gets extra command line arguments to pass to Chrome on startup.
  virtual std::vector<std::string> GetExtraCommandLineArguments() = 0;

  // Emits state change signals.
  virtual void AnnounceSessionStoppingIfNeeded() = 0;
  virtual void AnnounceSessionStopped() = 0;

  // Returns true if the user's session should be ended (rather than the browser
  // being restarted) if the browser crashes right now. This is performed as a
  // security measure (e.g. if the screen is currently locked). If |reason_out|
  // is non-null, a human-readable explanation is saved to it if true is
  // returned.
  virtual bool ShouldEndSession(std::string* reason_out) = 0;

  // Starts a 'Powerwash' of the device. `reason` determines clobber parameters
  // and the string persisted to clobber.log to annotate the cause of the
  // powerwash.
  virtual void InitiateDeviceWipe(WipeReason reason) = 0;
};

}  // namespace login_manager
#endif  // LOGIN_MANAGER_SESSION_MANAGER_INTERFACE_H_
