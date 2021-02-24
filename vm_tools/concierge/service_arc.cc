// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_util.h>

#include "vm_tools/common/pstore.h"
#include "vm_tools/concierge/arc_vm.h"
#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/shared_data.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {

namespace {

// Android data directory.
constexpr const char kAndroidDataDir[] = "/run/arcvm/android-data";

}  // namespace

std::unique_ptr<dbus::Response> Service::StartArcVm(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Received StartArcVm request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());
  StartArcVmRequest request;
  StartVmResponse response;
  auto helper_result = StartVmHelper<StartArcVmRequest>(
      method_call, &reader, &writer, true /* allow_zero_cpus */);
  if (!helper_result) {
    return dbus_response;
  }
  std::tie(request, response) = *helper_result;

  VmInfo* vm_info = response.mutable_vm_info();
  vm_info->set_vm_type(VmInfo::ARC_VM);

  if (request.disks_size() > kMaxExtraDisks) {
    LOG(ERROR) << "Rejecting request with " << request.disks_size()
               << " extra disks";

    response.set_failure_reason("Too many extra disks");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const base::FilePath kernel(request.vm().kernel());
  const base::FilePath rootfs(request.vm().rootfs());
  const base::FilePath fstab(request.fstab());

  if (!base::PathExists(kernel)) {
    LOG(ERROR) << "Missing VM kernel path: " << kernel.value();

    response.set_failure_reason("Kernel path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!base::PathExists(rootfs)) {
    LOG(ERROR) << "Missing VM rootfs path: " << rootfs.value();

    response.set_failure_reason("Rootfs path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!base::PathExists(fstab)) {
    LOG(ERROR) << "Missing VM fstab path: " << fstab.value();

    response.set_failure_reason("Fstab path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::vector<Disk> disks;
  // The rootfs can be treated as a disk as well and needs to be added before
  // other disks.
  disks.push_back(Disk(std::move(rootfs), request.rootfs_writable()));
  for (const auto& disk : request.disks()) {
    if (!base::PathExists(base::FilePath(disk.path()))) {
      LOG(ERROR) << "Missing disk path: " << disk.path();
      response.set_failure_reason("One or more disk paths do not exist");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
    disks.push_back(Disk(base::FilePath(disk.path()), disk.writable()));
  }

  // Create the runtime directory.
  base::FilePath runtime_dir;
  if (!base::CreateTemporaryDirInDir(base::FilePath(kRuntimeDir), "vm.",
                                     &runtime_dir)) {
    PLOG(ERROR) << "Unable to create runtime directory for VM";

    response.set_failure_reason(
        "Internal error: unable to create runtime directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Allocate resources for the VM.
  uint32_t vsock_cid = vsock_cid_pool_.Allocate();
  if (vsock_cid == 0) {
    LOG(ERROR) << "Unable to allocate vsock context id";

    response.set_failure_reason("Unable to allocate vsock cid");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  vm_info->set_cid(vsock_cid);

  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New(bus_);
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";

    response.set_failure_reason("Unable to open network service client");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Map the chronos user (1000) and the chronos-access group (1001) to the
  // AID_EXTERNAL_STORAGE user and group (1077).
  uint32_t seneschal_server_port = next_seneschal_server_port_++;
  std::unique_ptr<SeneschalServerProxy> server_proxy =
      SeneschalServerProxy::CreateVsockProxy(bus_, seneschal_service_proxy_,
                                             seneschal_server_port, vsock_cid,
                                             {{1000, 1077}}, {{1001, 1077}});
  if (!server_proxy) {
    LOG(ERROR) << "Unable to start shared directory server";

    response.set_failure_reason("Unable to start shared directory server");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  uint32_t seneschal_server_handle = server_proxy->handle();
  vm_info->set_seneschal_server_handle(seneschal_server_handle);

  // Build the plugin params.
  std::vector<std::string> params(
      std::make_move_iterator(request.mutable_params()->begin()),
      std::make_move_iterator(request.mutable_params()->end()));
  params.emplace_back(base::StringPrintf("androidboot.seneschal_server_port=%d",
                                         seneschal_server_port));

  // Start the VM and build the response.
  ArcVmFeatures features;
  features.rootfs_writable = request.rootfs_writable();
  features.use_dev_conf = !request.ignore_dev_conf();

  base::FilePath data_dir = base::FilePath(kAndroidDataDir);
  if (!base::PathExists(data_dir)) {
    LOG(WARNING) << "Android data directory does not exist";

    response.set_failure_reason("Android data directory does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VmId vm_id(request.owner_id(), request.name());
  SendVmStartingUpSignal(vm_id, *vm_info);

  std::string shared_data =
      CreateSharedDataParam(data_dir, "_data", true, false);
  std::string shared_data_media =
      CreateSharedDataParam(data_dir, "_data_media", false, true);

  // TOOD(kansho): |non_rt_cpus_num|, |rt_cpus_num| and |rt_vcpu_affinity|
  // should be passed from chrome instead of |enable_rt_vcpu|.
  uint32_t non_rt_cpus_num;
  uint32_t rt_cpus_num;
  bool rt_vcpu_affinity;
  if (request.enable_rt_vcpu()) {
    // RT-vcpu will not work when only one logical core is online.
    if (request.cpus() < 2) {
      LOG(ERROR)
          << "RT-vcpu doesn't support device with only one logical core online";

      response.set_failure_reason(
          "RT-vcpu doesn't support device with only one logical core online");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    rt_cpus_num = 1;
    if (request.cpus() == 2) {
      // When two logical cores are online, use 2 non rt-vcpus and 1 rt-vcpu
      // without pinning.
      non_rt_cpus_num = request.cpus();
      rt_vcpu_affinity = false;
    } else {  // online logical cores > 2
      // When 3+ logical cores are online, use #cpu-1 non rt-vcpus and
      // 1 rt-vcpu with pinning. As a result, the number of vcpus is equal to
      // the number of online logical core which is passed from chrome.
      non_rt_cpus_num = request.cpus() - rt_cpus_num;
      rt_vcpu_affinity = true;
    }
  } else {  // rt-vcpu disabled
    non_rt_cpus_num = request.cpus();
    rt_cpus_num = 0;
  }

  // Add RT-vcpus following non RT-vcpus.
  const uint32_t cpus = non_rt_cpus_num + rt_cpus_num;
  std::string rt_vcpus_comma_separated;
  std::string cpu_affinity;
  if (rt_cpus_num > 0) {
    std::vector<std::string> rt_vcpus;
    for (uint32_t i = non_rt_cpus_num; i < cpus; i++) {
      rt_vcpus.emplace_back(std::to_string(i));
    }
    rt_vcpus_comma_separated = base::JoinString(rt_vcpus, ",");
    params.emplace_back("isolcpus=" + rt_vcpus_comma_separated);
    params.emplace_back("androidboot.rtcpus=" + rt_vcpus_comma_separated);

    // Isolate non RT-vcpus and RT-vcpus.
    // Guarantee that any non RT-vcpus and any RT-vcpus are never assigned to
    // a same pCPU to avoid lock-holder preemption problem.
    if (rt_vcpu_affinity) {
      const uint32_t pcpu_num_for_rt_vcpus = 1;
      std::vector<std::string> cpu_affinities;
      for (uint32_t i = 0; i < non_rt_cpus_num; i++) {
        cpu_affinities.emplace_back(base::StringPrintf(
            "%d=%d-%d", i, 0, cpus - pcpu_num_for_rt_vcpus - 1));
      }
      for (uint32_t i = non_rt_cpus_num; i < cpus; i++) {
        cpu_affinities.emplace_back(base::StringPrintf(
            "%d=%d-%d", i, cpus - pcpu_num_for_rt_vcpus, cpus - 1));
      }
      cpu_affinity = base::JoinString(cpu_affinities, ":");
    }
  }

  VmBuilder vm_builder;
  vm_builder.AppendDisks(std::move(disks))
      .SetCpus(cpus)
      .AppendKernelParam(base::JoinString(params, " "))
      .AppendCustomParam("--android-fstab", fstab.value())
      .AppendCustomParam("--pstore",
                         base::StringPrintf("path=%s,size=%d", kArcVmPstorePath,
                                            kArcVmPstoreSize))
      .AppendSharedDir(shared_data)
      .AppendSharedDir(shared_data_media)
      .EnableSmt(false /* enable */);

  if (rt_cpus_num > 0) {
    vm_builder.AppendCustomParam("--rt-cpus", rt_vcpus_comma_separated);
    if (rt_vcpu_affinity)
      vm_builder.AppendCustomParam("--cpu-affinity", cpu_affinity);
  }

  auto vm =
      ArcVm::Create(std::move(kernel), vsock_cid, std::move(network_client),
                    std::move(server_proxy), std::move(runtime_dir), features,
                    std::move(vm_builder));
  if (!vm) {
    LOG(ERROR) << "Unable to start VM";

    response.set_failure_reason("Unable to start VM");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // ARCVM is ready.
  LOG(INFO) << "Started VM with pid " << vm->pid();

  response.set_success(true);
  response.set_status(VM_STATUS_RUNNING);
  vm_info->set_ipv4_address(vm->IPv4Address());
  vm_info->set_pid(vm->pid());
  writer.AppendProtoAsArrayOfBytes(response);

  SendVmStartedSignal(vm_id, *vm_info, response.status());

  vms_[vm_id] = std::move(vm);
  return dbus_response;
}

}  // namespace concierge
}  // namespace vm_tools
