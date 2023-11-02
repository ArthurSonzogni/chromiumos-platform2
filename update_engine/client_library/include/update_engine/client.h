//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef UPDATE_ENGINE_CLIENT_LIBRARY_INCLUDE_UPDATE_ENGINE_CLIENT_H_
#define UPDATE_ENGINE_CLIENT_LIBRARY_INCLUDE_UPDATE_ENGINE_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <update_engine/proto_bindings/update_engine.pb.h>

#include "update_engine/status_update_handler.h"
#include "update_engine/update_status.h"

namespace update_engine {

class UpdateEngineClient {
 public:
  static std::unique_ptr<UpdateEngineClient> CreateInstance();

  virtual ~UpdateEngineClient() = default;

  // Force the update_engine to update.
  // |update_params|
  //     Refer to proto defined in system_api.
  virtual bool Update(const update_engine::UpdateParams& update_params) = 0;

  // Applies the deferred update if there is one.
  virtual bool ApplyDeferredUpdate() = 0;

  // Request the update_engine to install a list of DLC modules.
  // |omaha_url|
  //     Force update_engine to look for updates from the given server. Passing
  //     empty indicates update_engine should use its default value. Note that
  //     update_engine will ignore this parameter in production mode to avoid
  //     pulling untrusted updates.
  // |dlc_ids|
  //     A list of DLC module IDs.
  virtual bool AttemptInstall(const std::string& omaha_url,
                              const std::vector<std::string>& dlc_ids) = 0;
  virtual bool Install(const update_engine::InstallParams& install_params) = 0;

  // Returns the entire update engine status struct.
  virtual bool GetStatus(UpdateEngineStatus* out_status) const = 0;
  // Overrides the current update status. Only used for testing.
  virtual bool SetStatus(UpdateStatus update_status) const = 0;

  // Sets the DLC as active or inactive. When set to active, the ping metadata
  // for the DLC is updated accordingly. When set to inactive, the metadata
  // for the DLC is deleted.
  virtual bool SetDlcActiveValue(bool is_active, const std::string& dlc_id) = 0;

  // Getter and setter for the cohort hint.
  virtual bool SetCohortHint(const std::string& cohort_hint) = 0;
  virtual bool GetCohortHint(std::string* cohort_hint) const = 0;

  // Getter and setter for the updates over cellular connections.
  virtual bool SetUpdateOverCellularPermission(bool allowed) = 0;
  virtual bool GetUpdateOverCellularPermission(bool* allowed) const = 0;

  // Getter and setter for the updates from P2P permission.
  virtual bool SetP2PUpdatePermission(bool enabled) = 0;
  virtual bool GetP2PUpdatePermission(bool* enabled) const = 0;

  // Attempt a rollback. Set 'powerwash' to reset the device while rolling
  // back.
  virtual bool Rollback(bool powerwash) = 0;

  // Get the rollback partition if available. Gives empty string if not.
  virtual bool GetRollbackPartition(std::string* rollback_partition) const = 0;

  // Reboot the system if needed.
  virtual void RebootIfNeeded() = 0;

  // Get the previous version
  virtual bool GetPrevVersion(std::string* prev_version) const = 0;

  // Resets the status of the Update Engine
  virtual bool ResetStatus() = 0;

  // Changes the current channel of the device to the target channel.
  virtual bool SetTargetChannel(const std::string& target_channel,
                                bool allow_powerwash) = 0;

  // Get the channel the device will switch to on reboot.
  virtual bool GetTargetChannel(std::string* out_channel) const = 0;

  // Get the channel the device is currently on.
  virtual bool GetChannel(std::string* out_channel) const = 0;

  // Handle status updates. The handler must exist until the client is
  // destroyed or UnregisterStatusUpdateHandler is called for it. Its IPCError
  // method will be called if the handler could not be registered. Otherwise
  // its HandleStatusUpdate method will be called every time update_engine's
  // status changes. Will always report the status on registration to prevent
  // race conditions.
  virtual bool RegisterStatusUpdateHandler(StatusUpdateHandler* handler) = 0;

  // Unregister a status update handler
  virtual bool UnregisterStatusUpdateHandler(StatusUpdateHandler* handler) = 0;

  // Get the last UpdateAttempt error code.
  virtual bool GetLastAttemptError(int32_t* last_attempt_error) const = 0;

  virtual bool ToggleFeature(const std::string& feature, bool enable) = 0;

  virtual bool IsFeatureEnabled(const std::string& feature,
                                bool* out_enabled) = 0;

 protected:
  // Use CreateInstance().
  UpdateEngineClient() = default;

 private:
  UpdateEngineClient(const UpdateEngineClient&) = delete;
  void operator=(const UpdateEngineClient&) = delete;
};  // class UpdateEngineClient

}  // namespace update_engine

#endif  // UPDATE_ENGINE_CLIENT_LIBRARY_INCLUDE_UPDATE_ENGINE_CLIENT_H_
