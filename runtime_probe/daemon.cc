// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/json/json_writer.h>
#include <base/memory/ptr_util.h>
#include <chromeos/dbus/service_constants.h>

#include <google/protobuf/util/json_util.h>

#include "runtime_probe/daemon.h"
#include "runtime_probe/probe_config.h"
#include "runtime_probe/probe_config_loader_impl.h"

namespace runtime_probe {

const char kErrorMsgFailedToPackProtobuf[] = "Failed to serialize the protobuf";

namespace {

void DumpProtocolBuffer(const google::protobuf::Message& protobuf,
                        std::string message_name) {
  VLOG(3) << "---> Protobuf dump of " << message_name;
  VLOG(3) << "       DebugString():\n\n" << protobuf.DebugString();
  std::string json_string;
  google::protobuf::util::JsonPrintOptions options;
  MessageToJsonString(protobuf, &json_string, options);
  VLOG(3) << "       JSON output:\n\n" << json_string << "\n";
  VLOG(3) << "<--- Finished Protobuf dump\n";
}

}  // namespace

Daemon::Daemon() {}

Daemon::~Daemon() {}

int Daemon::OnInit() {
  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  InitDBus();
  return 0;
}

void Daemon::InitDBus() {
  LOG(INFO) << "Init DBus for Runtime Probe";
  // Get or create the ExportedObject for the Runtime Probe service.
  auto const runtime_probe_exported_object =
      bus_->GetExportedObject(dbus::ObjectPath(kRuntimeProbeServicePath));
  CHECK(runtime_probe_exported_object);

  // Register a handler of the ProbeCategories method.
  CHECK(runtime_probe_exported_object->ExportMethodAndBlock(
      kRuntimeProbeInterfaceName, kProbeCategoriesMethod,
      base::Bind(&Daemon::ProbeCategories, weak_ptr_factory_.GetWeakPtr())));

  // Register a handler of the GetKnownComponents method.
  CHECK(runtime_probe_exported_object->ExportMethodAndBlock(
      kRuntimeProbeInterfaceName, kGetKnownComponentsMethod,
      base::Bind(&Daemon::GetKnownComponents, weak_ptr_factory_.GetWeakPtr())));

  // Take ownership of the RuntimeProbe service.
  CHECK(bus_->RequestOwnershipAndBlock(kRuntimeProbeServiceName,
                                       dbus::Bus::REQUIRE_PRIMARY));
  LOG(INFO) << kRuntimeProbeServicePath << " DBus initialized.";
}

void Daemon::PostQuitTask() {
  bus_->GetOriginTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&Daemon::QuitDaemonInternal, base::Unretained(this)));
}

void Daemon::QuitDaemonInternal() {
  bus_->ShutdownAndBlock();
  Quit();
}

void Daemon::SendMessage(const google::protobuf::Message& reply,
                         dbus::MethodCall* method_call,
                         dbus::ExportedObject::ResponseSender response_sender) {
  DumpProtocolBuffer(reply, "ProbeResult");

  std::unique_ptr<dbus::Response> message(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageWriter writer(message.get());
  if (!writer.AppendProtoAsArrayOfBytes(reply)) {
    LOG(ERROR) << kErrorMsgFailedToPackProtobuf;
    std::move(response_sender)
        .Run(dbus::ErrorResponse::FromMethodCall(
            method_call, DBUS_ERROR_INVALID_ARGS,
            kErrorMsgFailedToPackProtobuf));
  } else {
    // TODO(itspeter): b/119939408, PII filter before return.
    std::move(response_sender).Run(std::move(message));
  }
  PostQuitTask();
}

void Daemon::ProbeCategories(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  std::unique_ptr<dbus::Response> message(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(message.get());
  ProbeRequest request;
  ProbeResult reply;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_REQUEST_INVALID);
    return SendMessage(reply, method_call, std::move(response_sender));
  }

  DumpProtocolBuffer(request, "ProbeRequest");

  const auto probe_config_loader =
      std::make_unique<runtime_probe::ProbeConfigLoaderImpl>();
  const auto probe_config_data = probe_config_loader->LoadDefault();
  if (!probe_config_data) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
    return SendMessage(reply, method_call, std::move(response_sender));
  }
  LOG(INFO) << "Load probe config from: " << probe_config_data->path
            << " (checksum: " << probe_config_data->sha1_hash << ")";

  reply.set_probe_config_checksum(probe_config_data->sha1_hash);

  const auto probe_config =
      runtime_probe::ProbeConfig::FromValue(probe_config_data->config);
  if (!probe_config) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INCOMPLETE_PROBE_FUNCTION);
    return SendMessage(reply, method_call, std::move(response_sender));
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

  return SendMessage(reply, method_call, std::move(response_sender));
}

void Daemon::GetKnownComponents(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  std::unique_ptr<dbus::Response> message(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(message.get());
  GetKnownComponentsRequest request;
  GetKnownComponentsResult reply;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_REQUEST_INVALID);
    return SendMessage(reply, method_call, std::move(response_sender));
  }

  const auto probe_config_loader =
      std::make_unique<runtime_probe::ProbeConfigLoaderImpl>();
  const auto probe_config_data = probe_config_loader->LoadDefault();
  if (!probe_config_data) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
    return SendMessage(reply, method_call, std::move(response_sender));
  }

  const auto probe_config =
      runtime_probe::ProbeConfig::FromValue(probe_config_data->config);
  if (!probe_config) {
    reply.set_error(RUNTIME_PROBE_ERROR_PROBE_CONFIG_INCOMPLETE_PROBE_FUNCTION);
    return SendMessage(reply, method_call, std::move(response_sender));
  }

  std::string category_name =
      ProbeRequest_SupportCategory_Name(request.category());
  if (auto category = probe_config->GetComponentCategory(category_name);
      category != nullptr) {
    for (const auto& name : category->GetComponentNames()) {
      reply.add_component_names(name);
    }
  }

  return SendMessage(reply, method_call, std::move(response_sender));
}

}  // namespace runtime_probe
