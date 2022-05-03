// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/values.h>
#include <dbus/runtime_probe/dbus-constants.h>
#include <google/protobuf/util/json_util.h>

#include "runtime_probe/daemon.h"
#include "runtime_probe/probe_config.h"
#include "runtime_probe/probe_config_loader_impl.h"
#include "runtime_probe/proto_bindings/runtime_probe.pb.h"

namespace runtime_probe {

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusObject;

Daemon::Daemon(ProbeConfigLoader* config_loader)
    : brillo::DBusServiceDaemon(kRuntimeProbeServiceName),
      org::chromium::RuntimeProbeAdaptor(this),
      config_loader_(config_loader) {}

int Daemon::OnInit() {
  VLOG(1) << "Starting D-Bus service";
  const auto exit_code = brillo::DBusServiceDaemon::OnInit();
  return exit_code;
}

void Daemon::RegisterDBusObjectsAsync(AsyncEventSequencer* sequencer) {
  DCHECK(!dbus_object_);
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr, bus_, dbus::ObjectPath(kRuntimeProbeServicePath));
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

void Daemon::ProbeCategories(Daemon::DBusCallback<ProbeResult> cb,
                             const ProbeRequest& request) {
  ProbeResult reply;

  const auto probe_config_data = config_loader_->LoadDefault();
  if (!probe_config_data) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
    cb->Return(reply);
    return Quit();
  }
  LOG(INFO) << "Load probe config from: " << probe_config_data->path
            << " (checksum: " << probe_config_data->sha1_hash << ")";

  reply.set_probe_config_checksum(probe_config_data->sha1_hash);

  const auto probe_config = ProbeConfig::FromValue(probe_config_data->config);
  if (!probe_config) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INCOMPLETE_PROBE_FUNCTION);
    cb->Return(reply);
    return Quit();
  }

  base::Value probe_result;
  if (request.probe_default_category()) {
    probe_result = probe_config->Eval();
  } else {
    // Convert the ProbeReuslt from enum into array of string.
    std::vector<std::string> categories_to_probe;
    const google::protobuf::EnumDescriptor* descriptor =
        ProbeRequest_SupportCategory_descriptor();

    for (int j = 0; j < request.categories_size(); j++)
      categories_to_probe.push_back(
          descriptor->FindValueByNumber(request.categories(j))->name());

    probe_result = probe_config->Eval(categories_to_probe);
  }

  // TODO(itspeter): Report assigned but not in the probe config's category.
  std::string output_js;
  base::JSONWriter::Write(probe_result, &output_js);
  DVLOG(3) << "Raw JSON probe result\n" << output_js;

  // Convert JSON to Protocol Buffer.
  auto options = google::protobuf::util::JsonParseOptions();
  options.ignore_unknown_fields = true;
  ProbeResult placeholder;
  const auto json_parse_status = google::protobuf::util::JsonStringToMessage(
      output_js, &placeholder, options);
  reply.MergeFrom(placeholder);
  VLOG(3) << "serialize JSON to Protobuf status: " << json_parse_status;
  if (!json_parse_status.ok()) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_RESULT_INVALID);
  }

  cb->Return(reply);
  return Quit();
}

void Daemon::GetKnownComponents(
    Daemon::DBusCallback<GetKnownComponentsResult> cb,
    const GetKnownComponentsRequest& request) {
  GetKnownComponentsResult reply;

  const auto probe_config_data = config_loader_->LoadDefault();
  if (!probe_config_data) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
    cb->Return(reply);
    return Quit();
  }

  const auto probe_config = ProbeConfig::FromValue(probe_config_data->config);
  if (!probe_config) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INCOMPLETE_PROBE_FUNCTION);
    cb->Return(reply);
    return Quit();
  }

  std::string category_name =
      ProbeRequest_SupportCategory_Name(request.category());
  if (auto category = probe_config->GetComponentCategory(category_name);
      category != nullptr) {
    for (const auto& name : category->GetComponentNames()) {
      reply.add_component_names(name);
    }
  }

  cb->Return(reply);
  return Quit();
}

}  // namespace runtime_probe
