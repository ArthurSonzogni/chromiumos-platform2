// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/oobe_config_restore_service.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "dbus/dbus-protocol.h"
#include "libhwsec/factory/factory.h"
#include "libhwsec/frontend/oobe_config/frontend.h"
#include "oobe_config/flex_oobe_config.h"
#include "oobe_config/load_oobe_config_rollback.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_handler.h"
#include "oobe_config/oobe_config.h"
#include "oobe_config/proto_bindings/oobe_config.pb.h"

#include <base/check.h>
#include <base/logging.h>

using brillo::dbus_utils::AsyncEventSequencer;

namespace oobe_config {

OobeConfigRestoreService::OobeConfigRestoreService(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
    : org::chromium::OobeConfigRestoreAdaptor(this),
      dbus_object_(std::move(dbus_object)) {}

OobeConfigRestoreService::~OobeConfigRestoreService() = default;

void OobeConfigRestoreService::RegisterAsync(
    AsyncEventSequencer::CompletionAction completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(std::move(completion_callback));
}

namespace {

std::optional<std::string> ReadRollbackConfig() {
  hwsec::FactoryImpl hwsec_factory(hwsec::ThreadingMode::kCurrentThread);
  std::unique_ptr<const hwsec::OobeConfigFrontend> hwsec_oobe_config =
      hwsec_factory.GetOobeConfigFrontend();
  OobeConfig oobe_config(hwsec_oobe_config.get());
  EnterpriseRollbackMetricsHandler rollback_metrics;
  LoadOobeConfigRollback load_oobe_config_rollback(&oobe_config,
                                                   &rollback_metrics);
  std::string rollback_config;
  // There is rollback data so attempt to parse it.
  const bool rollback_success =
      load_oobe_config_rollback.GetOobeConfigJson(&rollback_config);
  return rollback_success ? std::optional<std::string>(rollback_config)
                          : std::nullopt;
}

std::optional<std::string> ReadFlexConfig() {
  FlexOobeConfig flex_oobe_config;
  std::string flex_config;
  const bool flex_config_success =
      flex_oobe_config.GetOobeConfigJson(&flex_config);
  return flex_config_success ? std::optional<std::string>(flex_config)
                             : std::nullopt;
}

}  // namespace

void OobeConfigRestoreService::ProcessAndGetOobeAutoConfig(
    int32_t* error, OobeRestoreData* oobe_config_proto) {
  DCHECK(error);
  DCHECK(oobe_config_proto);

  *error = 0;

  LOG(INFO) << "Chrome requested OOBE config.";

  std::optional<std::string> rollback_config = ReadRollbackConfig();
  if (rollback_config.has_value()) {
    LOG(INFO) << "Rollback oobe config sent.";
    oobe_config_proto->set_chrome_config_json(*rollback_config);
    return;
  }

  LOG(INFO) << "No rollback OOBE config found.";

  if (USE_REVEN_OOBE_CONFIG) {
    std::optional<std::string> flex_config = ReadFlexConfig();
    if (flex_config.has_value()) {
      LOG(INFO) << "Flex oobe config sent.";
      oobe_config_proto->set_chrome_config_json(*flex_config);
      return;
    }
  }

  LOG(INFO) << "No Flex OOBE config found.";

  oobe_config_proto->set_chrome_config_json("");
}

// TODO(b/316944501): Implement Flex OOBE config deletion.
bool OobeConfigRestoreService::DeleteFlexOobeConfig(brillo::ErrorPtr* error) {
  FlexOobeConfig flex_oobe_config;
  return flex_oobe_config.DeleteFlexOobeConfig(error);
}

}  // namespace oobe_config
