// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>
#include <base/values.h>
#include <dbus/runtime_probe/dbus-constants.h>
#include <google/protobuf/util/json_util.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "runtime_probe/avl_probe_config_loader.h"
#include "runtime_probe/daemon.h"
#include "runtime_probe/probe_config.h"
#include "runtime_probe/proto_bindings/runtime_probe.pb.h"
#include "runtime_probe/ssfc_probe_config_loader.h"

namespace runtime_probe {

namespace {

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusObject;

}  // namespace

Daemon::Daemon()
    : brillo::DBusServiceDaemon(kRuntimeProbeServiceName),
      org::chromium::RuntimeProbeAdaptor(this) {}

int Daemon::OnInit() {
  VLOG(1) << "Starting D-Bus service";
  const auto exit_code = brillo::DBusServiceDaemon::OnInit();

  // Required by mojo
  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

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
  AvlProbeConfigLoader config_loader;
  const auto probe_config = config_loader.Load();
  if (!probe_config) {
    ProbeResult reply;
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
    cb->Return(reply);
    return Quit();
  }

  if (request.probe_default_category()) {
    probe_config->Eval(base::BindOnce(&Daemon::ProbeCallback<ProbeResult>,
                                      base::Unretained(this), std::move(cb)));
  } else {
    // Convert the ProbeReuslt from enum into array of string.
    std::vector<std::string> categories_to_probe;
    const google::protobuf::EnumDescriptor* descriptor =
        ProbeRequest_SupportCategory_descriptor();

    for (int j = 0; j < request.categories_size(); j++)
      categories_to_probe.push_back(
          descriptor->FindValueByNumber(request.categories(j))->name());

    probe_config->Eval(categories_to_probe,
                       base::BindOnce(&Daemon::ProbeCallback<ProbeResult>,
                                      base::Unretained(this), std::move(cb)));
  }
}

void Daemon::GetKnownComponents(
    Daemon::DBusCallback<GetKnownComponentsResult> cb,
    const GetKnownComponentsRequest& request) {
  GetKnownComponentsResult reply;

  AvlProbeConfigLoader config_loader;
  const auto probe_config = config_loader.Load();
  if (!probe_config) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
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

void Daemon::ProbeSsfcComponents(
    Daemon::DBusCallback<ProbeSsfcComponentsResponse> cb,
    const ProbeSsfcComponentsRequest& request) {
  SsfcProbeConfigLoader config_loader;
  const auto probe_config = config_loader.Load();
  if (!probe_config) {
    ProbeSsfcComponentsResponse reply;
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
    cb->Return(reply);
    return Quit();
  }

  probe_config->Eval(
      base::BindOnce(&Daemon::ProbeCallback<ProbeSsfcComponentsResponse>,
                     base::Unretained(this), std::move(cb)));
}

template <typename MessageType>
void Daemon::ProbeCallback(Daemon::DBusCallback<MessageType> cb,
                           base::Value::Dict probe_result) {
  std::string output_json;
  base::JSONWriter::Write(probe_result, &output_json);
  DVLOG(3) << "Raw JSON probe result\n" << output_json;

  // Convert JSON to Protocol Buffer.
  MessageType reply;
  auto options = google::protobuf::util::JsonParseOptions();
  options.ignore_unknown_fields = true;
  const auto json_parse_status =
      google::protobuf::util::JsonStringToMessage(output_json, &reply, options);

  VLOG(3) << "serialize JSON to Protobuf status: " << json_parse_status;
  if (!json_parse_status.ok()) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_RESULT_INVALID);
  }

  cb->Return(reply);
  return Quit();
}

}  // namespace runtime_probe
