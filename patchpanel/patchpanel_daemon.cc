// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/patchpanel_daemon.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <algorithm>
#include <set>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/key_value_store.h>
#include <metrics/metrics_library.h>
#include <shill/net/process_manager.h>

#include "patchpanel/ipc.h"
#include "patchpanel/metrics.h"
#include "patchpanel/net_util.h"
#include "patchpanel/proto_utils.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {
namespace {
// Passes |method_call| to |handler| and passes the response to
// |response_sender|. If |handler| returns nullptr, an empty response is
// created and sent.
void HandleSynchronousDBusMethodCall(
    base::RepeatingCallback<std::unique_ptr<dbus::Response>(dbus::MethodCall*)>
        handler,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  std::unique_ptr<dbus::Response> response = handler.Run(method_call);
  if (!response)
    response = dbus::Response::FromMethodCall(method_call);
  std::move(response_sender).Run(std::move(response));
}

void RecordDbusEvent(std::unique_ptr<MetricsLibraryInterface>& metrics,
                     DbusUmaEvent event) {
  metrics->SendEnumToUMA(kDbusUmaEventMetrics, event);
}

}  // namespace

PatchpanelDaemon::PatchpanelDaemon(const base::FilePath& cmd_path)
    : cmd_path_(cmd_path),
      system_(std::make_unique<System>()),
      process_manager_(shill::ProcessManager::GetInstance()),
      metrics_(std::make_unique<MetricsLibrary>()) {}

std::map<const std::string, bool> PatchpanelDaemon::cached_feature_enabled_ =
    {};

bool PatchpanelDaemon::ShouldEnableFeature(
    int min_android_sdk_version,
    int min_chrome_milestone,
    const std::vector<std::string>& supported_boards,
    const std::string& feature_name) {
  static const char kLsbReleasePath[] = "/etc/lsb-release";

  const auto& cached_result = cached_feature_enabled_.find(feature_name);
  if (cached_result != cached_feature_enabled_.end())
    return cached_result->second;

  auto check = [min_android_sdk_version, min_chrome_milestone,
                &supported_boards, &feature_name]() {
    brillo::KeyValueStore store;
    if (!store.Load(base::FilePath(kLsbReleasePath))) {
      LOG(ERROR) << "Could not read lsb-release";
      return false;
    }

    std::string value;
    if (!store.GetString("CHROMEOS_ARC_ANDROID_SDK_VERSION", &value)) {
      LOG(ERROR) << feature_name
                 << " disabled - cannot determine Android SDK version";
      return false;
    }
    int ver = 0;
    if (!base::StringToInt(value.c_str(), &ver)) {
      LOG(ERROR) << feature_name << " disabled - invalid Android SDK version";
      return false;
    }
    if (ver < min_android_sdk_version) {
      LOG(INFO) << feature_name << " disabled for Android SDK " << value;
      return false;
    }

    if (!store.GetString("CHROMEOS_RELEASE_CHROME_MILESTONE", &value)) {
      LOG(ERROR) << feature_name
                 << " disabled - cannot determine ChromeOS milestone";
      return false;
    }
    if (!base::StringToInt(value.c_str(), &ver)) {
      LOG(ERROR) << feature_name << " disabled - invalid ChromeOS milestone";
      return false;
    }
    if (ver < min_chrome_milestone) {
      LOG(INFO) << feature_name << " disabled for ChromeOS milestone " << value;
      return false;
    }

    if (!store.GetString("CHROMEOS_RELEASE_BOARD", &value)) {
      LOG(ERROR) << feature_name << " disabled - cannot determine board";
      return false;
    }
    if (!supported_boards.empty() &&
        std::find(supported_boards.begin(), supported_boards.end(), value) ==
            supported_boards.end()) {
      LOG(INFO) << feature_name << " disabled for board " << value;
      return false;
    }
    return true;
  };

  bool result = check();
  cached_feature_enabled_.emplace(feature_name, result);
  return result;
}

int PatchpanelDaemon::OnInit() {
  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

  // Initialize |process_manager_| before creating subprocesses.
  process_manager_->Init();

  // Run after Daemon::OnInit().
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&PatchpanelDaemon::InitialSetup,
                                weak_factory_.GetWeakPtr()));

  return brillo::DBusDaemon::OnInit();
}

void PatchpanelDaemon::InitialSetup() {
  auto shill_client = std::make_unique<ShillClient>(bus_, system_.get());
  manager_ =
      std::make_unique<Manager>(cmd_path_, system_.get(), process_manager_,
                                metrics_.get(), this, std::move(shill_client));

  LOG(INFO) << "Setting up DBus service interface";
  dbus_svc_path_ = bus_->GetExportedObject(
      dbus::ObjectPath(patchpanel::kPatchPanelServicePath));
  if (!dbus_svc_path_) {
    LOG(FATAL) << "Failed to export " << patchpanel::kPatchPanelServicePath
               << " object";
  }

  using ServiceMethod =
      std::unique_ptr<dbus::Response> (PatchpanelDaemon::*)(dbus::MethodCall*);
  const std::map<const char*, ServiceMethod> kServiceMethods = {
      {patchpanel::kArcShutdownMethod, &PatchpanelDaemon::OnArcShutdown},
      {patchpanel::kArcStartupMethod, &PatchpanelDaemon::OnArcStartup},
      {patchpanel::kArcVmShutdownMethod, &PatchpanelDaemon::OnArcVmShutdown},
      {patchpanel::kArcVmStartupMethod, &PatchpanelDaemon::OnArcVmStartup},
      {patchpanel::kConnectNamespaceMethod,
       &PatchpanelDaemon::OnConnectNamespace},
      {patchpanel::kCreateLocalOnlyNetworkMethod,
       &PatchpanelDaemon::OnCreateLocalOnlyNetwork},
      {patchpanel::kCreateTetheredNetworkMethod,
       &PatchpanelDaemon::OnCreateTetheredNetwork},
      {patchpanel::kDownstreamNetworkInfoMethod,
       &PatchpanelDaemon::OnDownstreamNetworkInfo},
      {patchpanel::kGetDevicesMethod, &PatchpanelDaemon::OnGetDevices},
      {patchpanel::kGetTrafficCountersMethod,
       &PatchpanelDaemon::OnGetTrafficCounters},
      {patchpanel::kModifyPortRuleMethod, &PatchpanelDaemon::OnModifyPortRule},
      {patchpanel::kPluginVmShutdownMethod,
       &PatchpanelDaemon::OnPluginVmShutdown},
      {patchpanel::kPluginVmStartupMethod,
       &PatchpanelDaemon::OnPluginVmStartup},
      {patchpanel::kSetDnsRedirectionRuleMethod,
       &PatchpanelDaemon::OnSetDnsRedirectionRule},
      {patchpanel::kSetVpnIntentMethod, &PatchpanelDaemon::OnSetVpnIntent},
      {patchpanel::kSetVpnLockdown, &PatchpanelDaemon::OnSetVpnLockdown},
      {patchpanel::kTerminaVmShutdownMethod,
       &PatchpanelDaemon::OnTerminaVmShutdown},
      {patchpanel::kTerminaVmStartupMethod,
       &PatchpanelDaemon::OnTerminaVmStartup},
  };

  for (const auto& kv : kServiceMethods) {
    if (!dbus_svc_path_->ExportMethodAndBlock(
            patchpanel::kPatchPanelInterface, kv.first,
            base::BindRepeating(
                &HandleSynchronousDBusMethodCall,
                base::BindRepeating(kv.second, base::Unretained(this))))) {
      LOG(FATAL) << "Failed to export method " << kv.first;
    }
  }

  if (!bus_->RequestOwnershipAndBlock(patchpanel::kPatchPanelServiceName,
                                      dbus::Bus::REQUIRE_PRIMARY)) {
    LOG(FATAL) << "Failed to take ownership of "
               << patchpanel::kPatchPanelServiceName;
  }
  LOG(INFO) << "DBus service interface ready";
}

void PatchpanelDaemon::OnShutdown(int* exit_code) {
  LOG(INFO) << "Shutting down and cleaning up";
  manager_.reset();

  if (bus_) {
    bus_->ShutdownAndBlock();
  }

  process_manager_->Stop();
  brillo::DBusDaemon::OnShutdown(exit_code);
}

void PatchpanelDaemon::OnNetworkDeviceChanged(const Device& virtual_device,
                                              Device::ChangeEvent event) {
  NetworkDeviceChangedSignal proto;
  proto.set_event(event == Device::ChangeEvent::kAdded
                      ? NetworkDeviceChangedSignal::DEVICE_ADDED
                      : NetworkDeviceChangedSignal::DEVICE_REMOVED);
  auto* dev = proto.mutable_device();
  FillDeviceProto(virtual_device, dev);
  if (const auto* subnet = virtual_device.config().ipv4_subnet()) {
    FillSubnetProto(*subnet, dev->mutable_ipv4_subnet());
  }

  dbus::Signal signal(kPatchPanelInterface, kNetworkDeviceChangedSignal);
  dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(proto);
  dbus_svc_path_->SendSignal(&signal);
}

void PatchpanelDaemon::OnNetworkConfigurationChanged() {
  dbus::Signal signal(kPatchPanelInterface, kNetworkConfigurationChangedSignal);
  dbus_svc_path_->SendSignal(&signal);
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnGetDevices(
    dbus::MethodCall* method_call) {
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::GetDevicesRequest request;
  patchpanel::GetDevicesResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  response = manager_->GetDevices();
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnArcStartup(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "ARC++ starting up";
  RecordDbusEvent(metrics_, DbusUmaEvent::kArcStartup);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ArcStartupRequest request;
  patchpanel::ArcStartupResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!manager_->ArcStartup(request.pid()))
    LOG(ERROR) << "Failed to start ARC++ network service";

  RecordDbusEvent(metrics_, DbusUmaEvent::kArcStartupSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnArcShutdown(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "ARC++ shutting down";
  RecordDbusEvent(metrics_, DbusUmaEvent::kArcShutdown);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ArcShutdownRequest request;
  patchpanel::ArcShutdownResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  manager_->ArcShutdown();

  RecordDbusEvent(metrics_, DbusUmaEvent::kArcShutdownSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnArcVmStartup(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "ARCVM starting up";
  RecordDbusEvent(metrics_, DbusUmaEvent::kArcVmStartup);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ArcVmStartupRequest request;
  patchpanel::ArcVmStartupResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto device_configs = manager_->ArcVmStartup(request.cid());
  if (!device_configs) {
    LOG(ERROR) << "Failed to start ARCVM network service";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Populate the response with the interface configurations of the known ARC
  // Devices
  for (const auto* config : *device_configs) {
    if (config->tap_ifname().empty())
      continue;

    // TODO(hugobenichi) Use FillDeviceProto.
    auto* dev = response.add_devices();
    dev->set_ifname(config->tap_ifname());
    dev->set_ipv4_addr(config->guest_ipv4_addr());
    dev->set_guest_type(NetworkDevice::ARCVM);
  }

  RecordDbusEvent(metrics_, DbusUmaEvent::kArcVmStartupSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnArcVmShutdown(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "ARCVM shutting down";
  RecordDbusEvent(metrics_, DbusUmaEvent::kArcVmShutdown);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ArcVmShutdownRequest request;
  patchpanel::ArcVmShutdownResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  manager_->ArcVmShutdown(request.cid());

  RecordDbusEvent(metrics_, DbusUmaEvent::kArcVmShutdownSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnTerminaVmStartup(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Termina VM starting up";
  RecordDbusEvent(metrics_, DbusUmaEvent::kTerminaVmStartup);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::TerminaVmStartupRequest request;
  patchpanel::TerminaVmStartupResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const uint32_t cid = request.cid();
  const auto* const guest_device = manager_->TerminaVmStartup(cid);
  if (!guest_device) {
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto* termina_subnet = guest_device->config().ipv4_subnet();
  if (!termina_subnet) {
    LOG(DFATAL) << "Missing required Termina IPv4 subnet for {cid: " << cid
                << "}";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  const auto* lxd_subnet = guest_device->config().lxd_ipv4_subnet();
  if (!lxd_subnet) {
    LOG(DFATAL) << "Missing required lxd container IPv4 subnet for {cid: "
                << cid << "}";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto* dev = response.mutable_device();
  FillDeviceProto(*guest_device, dev);
  FillSubnetProto(*termina_subnet, dev->mutable_ipv4_subnet());
  FillSubnetProto(*lxd_subnet, response.mutable_container_subnet());

  RecordDbusEvent(metrics_, DbusUmaEvent::kTerminaVmStartupSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnTerminaVmShutdown(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Termina VM shutting down";
  RecordDbusEvent(metrics_, DbusUmaEvent::kTerminaVmShutdown);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::TerminaVmShutdownRequest request;
  patchpanel::TerminaVmShutdownResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  manager_->TerminaVmShutdown(request.cid());

  RecordDbusEvent(metrics_, DbusUmaEvent::kTerminaVmShutdownSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnPluginVmStartup(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Plugin VM starting up";
  RecordDbusEvent(metrics_, DbusUmaEvent::kPluginVmStartup);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::PluginVmStartupRequest request;
  patchpanel::PluginVmStartupResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.subnet_index() < 0) {
    LOG(ERROR) << "Invalid subnet index: " << request.subnet_index();
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  const uint32_t subnet_index = static_cast<uint32_t>(request.subnet_index());

  const uint64_t vm_id = request.id();
  const auto* const guest_device =
      manager_->PluginVmStartup(vm_id, subnet_index);
  if (!guest_device) {
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto* subnet = guest_device->config().ipv4_subnet();
  if (!subnet) {
    LOG(DFATAL) << "Missing required subnet for {cid: " << vm_id << "}";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto* dev = response.mutable_device();
  FillDeviceProto(*guest_device, dev);
  FillSubnetProto(*subnet, dev->mutable_ipv4_subnet());

  RecordDbusEvent(metrics_, DbusUmaEvent::kPluginVmStartupSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnPluginVmShutdown(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Plugin VM shutting down";
  RecordDbusEvent(metrics_, DbusUmaEvent::kPluginVmShutdown);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::PluginVmShutdownRequest request;
  patchpanel::PluginVmShutdownResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  manager_->PluginVmShutdown(request.id());

  RecordDbusEvent(metrics_, DbusUmaEvent::kPluginVmShutdownSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnSetVpnIntent(
    dbus::MethodCall* method_call) {
  RecordDbusEvent(metrics_, DbusUmaEvent::kSetVpnIntent);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::SetVpnIntentRequest request;
  patchpanel::SetVpnIntentResponse response;

  bool success = true;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse SetVpnIntentRequest";
    // Do not return yet to make sure we close the received fd and
    // validate other arguments.
    success = false;
  }

  base::ScopedFD client_socket;
  reader.PopFileDescriptor(&client_socket);
  if (!client_socket.is_valid()) {
    LOG(ERROR) << "Unable to parse SetVpnIntentRequest";
    success = false;
  }

  if (!success) {
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  success = manager_->SetVpnIntent(request.policy(), std::move(client_socket));
  if (!success) {
    LOG(ERROR) << "Failed to set VpnIntent: " << request.policy();
  } else {
    response.set_success(true);
    RecordDbusEvent(metrics_, DbusUmaEvent::kSetVpnIntentSuccess);
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnConnectNamespace(
    dbus::MethodCall* method_call) {
  RecordDbusEvent(metrics_, DbusUmaEvent::kConnectNamespace);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ConnectNamespaceRequest request;
  patchpanel::ConnectNamespaceResponse response;

  bool success = true;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ConnectNamespaceRequest";
    // Do not return yet to make sure we close the received fd and
    // validate other arguments.
    success = false;
  }

  base::ScopedFD client_fd;
  reader.PopFileDescriptor(&client_fd);
  if (!client_fd.is_valid()) {
    LOG(ERROR) << "Invalid file descriptor";
    success = false;
  }

  if (!success) {
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  response = manager_->ConnectNamespace(request, std::move(client_fd));
  if (!response.netns_name().empty()) {
    RecordDbusEvent(metrics_, DbusUmaEvent::kConnectNamespaceSuccess);
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnGetTrafficCounters(
    dbus::MethodCall* method_call) {
  RecordDbusEvent(metrics_, DbusUmaEvent::kGetTrafficCounters);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::TrafficCountersRequest request;
  patchpanel::TrafficCountersResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse TrafficCountersRequest";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const std::set<std::string> shill_devices{request.devices().begin(),
                                            request.devices().end()};
  const auto counters = manager_->GetTrafficCounters(shill_devices);
  for (const auto& kv : counters) {
    auto* traffic_counter = response.add_counters();
    const auto& key = kv.first;
    const auto& counter = kv.second;
    traffic_counter->set_source(key.source);
    traffic_counter->set_device(key.ifname);
    traffic_counter->set_ip_family(key.ip_family);
    traffic_counter->set_rx_bytes(counter.rx_bytes);
    traffic_counter->set_rx_packets(counter.rx_packets);
    traffic_counter->set_tx_bytes(counter.tx_bytes);
    traffic_counter->set_tx_packets(counter.tx_packets);
  }

  RecordDbusEvent(metrics_, DbusUmaEvent::kGetTrafficCountersSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnModifyPortRule(
    dbus::MethodCall* method_call) {
  RecordDbusEvent(metrics_, DbusUmaEvent::kModifyPortRule);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ModifyPortRuleRequest request;
  patchpanel::ModifyPortRuleResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ModifyPortRequest";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  bool success = manager_->ModifyPortRule(request);
  response.set_success(success);
  if (success) {
    RecordDbusEvent(metrics_, DbusUmaEvent::kModifyPortRuleSuccess);
  }
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnSetVpnLockdown(
    dbus::MethodCall* method_call) {
  RecordDbusEvent(metrics_, DbusUmaEvent::kSetVpnLockdown);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::SetVpnLockdownRequest request;
  patchpanel::SetVpnLockdownResponse response;

  if (reader.PopArrayOfBytesAsProto(&request)) {
    manager_->SetVpnLockdown(request.enable_vpn_lockdown());
  } else {
    LOG(ERROR) << "Unable to parse SetVpnLockdownRequest";
  }

  RecordDbusEvent(metrics_, DbusUmaEvent::kSetVpnLockdownSuccess);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnSetDnsRedirectionRule(
    dbus::MethodCall* method_call) {
  RecordDbusEvent(metrics_, DbusUmaEvent::kSetDnsRedirectionRule);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::SetDnsRedirectionRuleRequest request;
  patchpanel::SetDnsRedirectionRuleResponse response;

  bool success = true;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse SetDnsRedirectionRuleRequest";
    // Do not return yet to make sure we close the received fd and
    // validate other arguments.
    success = false;
  }

  base::ScopedFD client_fd;
  reader.PopFileDescriptor(&client_fd);
  if (!client_fd.is_valid()) {
    LOG(ERROR) << "Invalid file descriptor";
    success = false;
  }

  if (!success) {
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  success = manager_->SetDnsRedirectionRule(request, std::move(client_fd));
  response.set_success(success);
  if (success) {
    RecordDbusEvent(metrics_, DbusUmaEvent::kSetDnsRedirectionRuleSuccess);
  }
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnCreateTetheredNetwork(
    dbus::MethodCall* method_call) {
  RecordDbusEvent(metrics_, DbusUmaEvent::kCreateTetheredNetwork);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  TetheredNetworkRequest request;
  TetheredNetworkResponse response;

  bool success = true;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    response.set_response_code(DownstreamNetworkResult::INVALID_ARGUMENT);
    // Do not return yet to make sure we close the received fd and
    // validate other arguments.
    success = false;
  }

  base::ScopedFD client_fd;
  reader.PopFileDescriptor(&client_fd);
  if (!client_fd.is_valid()) {
    LOG(ERROR) << __func__ << ": Invalid client file descriptor";
    response.set_response_code(DownstreamNetworkResult::INVALID_ARGUMENT);
    success = false;
  }

  if (!success) {
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto response_code =
      manager_->CreateTetheredNetwork(request, std::move(client_fd));
  if (response_code == patchpanel::DownstreamNetworkResult::SUCCESS) {
    RecordDbusEvent(metrics_, DbusUmaEvent::kCreateTetheredNetworkSuccess);
  }

  response.set_response_code(response_code);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnCreateLocalOnlyNetwork(
    dbus::MethodCall* method_call) {
  RecordDbusEvent(metrics_, DbusUmaEvent::kCreateLocalOnlyNetwork);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  LocalOnlyNetworkRequest request;
  LocalOnlyNetworkResponse response;

  bool success = true;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    response.set_response_code(DownstreamNetworkResult::INVALID_ARGUMENT);
    // Do not return yet to make sure we close the received fd and
    // validate other arguments.
    success = false;
  }

  base::ScopedFD client_fd;
  reader.PopFileDescriptor(&client_fd);
  if (!client_fd.is_valid()) {
    LOG(ERROR) << __func__ << ": Invalid client file descriptor";
    response.set_response_code(DownstreamNetworkResult::INVALID_ARGUMENT);
    success = false;
  }

  if (!success) {
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto response_code =
      manager_->CreateLocalOnlyNetwork(request, std::move(client_fd));
  if (response_code == patchpanel::DownstreamNetworkResult::SUCCESS) {
    RecordDbusEvent(metrics_, DbusUmaEvent::kCreateLocalOnlyNetworkSuccess);
  }

  response.set_response_code(response_code);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> PatchpanelDaemon::OnDownstreamNetworkInfo(
    dbus::MethodCall* method_call) {
  RecordDbusEvent(metrics_, DbusUmaEvent::kDownstreamNetworkInfo);

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::DownstreamNetworkInfoRequest request;
  patchpanel::DownstreamNetworkInfoResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << kDownstreamNetworkInfoMethod
               << ": Unable to parse DownstreamNetworkInfoRequest";
    response.set_success(false);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto& downstream_ifname = request.downstream_ifname();
  const auto downstream_network =
      manager_->GetDownstreamNetworkInfo(downstream_ifname);
  if (!downstream_network) {
    LOG(ERROR) << kDownstreamNetworkInfoMethod
               << ": no DownstreamNetwork for interface " << downstream_ifname;
    response.set_success(false);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // TODO(b/239559602) Get and copy clients' information into |output|.
  FillDownstreamNetworkProto(*downstream_network,
                             response.mutable_downstream_network());
  RecordDbusEvent(metrics_, DbusUmaEvent::kDownstreamNetworkInfoSuccess);
  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

void PatchpanelDaemon::OnNeighborReachabilityEvent(
    int ifindex,
    const shill::IPAddress& ip_addr,
    NeighborLinkMonitor::NeighborRole role,
    NeighborReachabilityEventSignal::EventType event_type) {
  using SignalProto = NeighborReachabilityEventSignal;
  SignalProto proto;
  proto.set_ifindex(ifindex);
  proto.set_ip_addr(ip_addr.ToString());
  proto.set_type(event_type);
  switch (role) {
    case NeighborLinkMonitor::NeighborRole::kGateway:
      proto.set_role(SignalProto::GATEWAY);
      break;
    case NeighborLinkMonitor::NeighborRole::kDNSServer:
      proto.set_role(SignalProto::DNS_SERVER);
      break;
    case NeighborLinkMonitor::NeighborRole::kGatewayAndDNSServer:
      proto.set_role(SignalProto::GATEWAY_AND_DNS_SERVER);
      break;
    default:
      NOTREACHED();
  }

  dbus::Signal signal(kPatchPanelInterface, kNeighborReachabilityEventSignal);
  dbus::MessageWriter writer(&signal);
  if (!writer.AppendProtoAsArrayOfBytes(proto)) {
    LOG(ERROR) << "Failed to encode proto NeighborReachabilityEventSignal";
    return;
  }

  dbus_svc_path_->SendSignal(&signal);
}

}  // namespace patchpanel
