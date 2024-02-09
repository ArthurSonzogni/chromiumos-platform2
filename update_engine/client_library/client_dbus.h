// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CLIENT_LIBRARY_CLIENT_DBUS_H_
#define UPDATE_ENGINE_CLIENT_LIBRARY_CLIENT_DBUS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <update_engine/proto_bindings/update_engine.pb.h>

#include "update_engine/client_library/include/update_engine/client.h"
#include "update_engine/dbus-proxies.h"

namespace update_engine {
namespace internal {

class DBusUpdateEngineClient : public UpdateEngineClient {
 public:
  DBusUpdateEngineClient() = default;
  DBusUpdateEngineClient(const DBusUpdateEngineClient&) = delete;
  DBusUpdateEngineClient& operator=(const DBusUpdateEngineClient&) = delete;

  bool Init();

  virtual ~DBusUpdateEngineClient() = default;

  bool Update(const update_engine::UpdateParams& update_params) override;

  bool ApplyDeferredUpdate() override;

  bool AttemptInstall(const std::string& omaha_url,
                      const std::vector<std::string>& dlc_ids) override;

  bool Install(const update_engine::InstallParams& install_params) override;

  bool SetDlcActiveValue(bool is_active, const std::string& dlc_id) override;

  bool GetStatus(UpdateEngineStatus* out_status) const override;
  bool SetStatus(UpdateStatus update_status) const override;

  bool SetCohortHint(const std::string& cohort_hint) override;
  bool GetCohortHint(std::string* cohort_hint) const override;

  bool SetUpdateOverCellularPermission(bool allowed) override;
  bool GetUpdateOverCellularPermission(bool* allowed) const override;

  bool SetP2PUpdatePermission(bool enabled) override;
  bool GetP2PUpdatePermission(bool* enabled) const override;

  bool Rollback(bool powerwash) override;

  bool GetRollbackPartition(std::string* rollback_partition) const override;

  void RebootIfNeeded() override;

  bool GetPrevVersion(std::string* prev_version) const override;

  bool ResetStatus() override;

  bool SetTargetChannel(const std::string& target_channel,
                        bool allow_powerwash) override;

  bool GetTargetChannel(std::string* out_channel) const override;

  bool GetChannel(std::string* out_channel) const override;

  bool ToggleFeature(const std::string& feature, bool enable) override;
  bool IsFeatureEnabled(const std::string& feature, bool* out_enabled) override;

  bool RegisterStatusUpdateHandler(StatusUpdateHandler* handler) override;
  bool UnregisterStatusUpdateHandler(StatusUpdateHandler* handler) override;

  bool GetLastAttemptError(int32_t* last_attempt_error) const override;

 private:
  void DBusStatusHandlersRegistered(const std::string& interface,
                                    const std::string& signal_name,
                                    bool success) const;

  // Send an initial event to new StatusUpdateHandlers. If the handler argument
  // is not nullptr, only that handler receives the event. Otherwise all
  // registered handlers receive the event.
  void StatusUpdateHandlersRegistered(StatusUpdateHandler* handler) const;

  void RunStatusUpdateHandlers(const StatusResult& status);

  std::unique_ptr<org::chromium::UpdateEngineInterfaceProxy> proxy_;
  std::vector<update_engine::StatusUpdateHandler*> handlers_;
  bool dbus_handler_registered_{false};
};  // class DBusUpdateEngineClient

}  // namespace internal
}  // namespace update_engine

#endif  // UPDATE_ENGINE_CLIENT_LIBRARY_CLIENT_DBUS_H_
