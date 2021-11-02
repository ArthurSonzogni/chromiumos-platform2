// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>

#include "vm_tools/common/pstore.h"
#include "vm_tools/concierge/arc_vm.h"
#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/shared_data.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {

namespace {

// Android data directory.
constexpr char kAndroidDataDir[] = "/run/arcvm/android-data";

// Android stub volume directory for MyFiles and removable media.
constexpr char kStubVolumeSharedDir[] = "/run/arcvm/media";

// Path to the VM guest kernel.
constexpr char kKernelPath[] = "/opt/google/vms/android/vmlinux";

// Path to the VM rootfs image file.
constexpr char kRootfsPath[] = "/opt/google/vms/android/system.raw.img";

// Path to the VM fstab file.
constexpr char kFstabPath[] = "/run/arcvm/host_generated/fstab";

// The CPU cgroup where all the ARCVM's crosvm processes should belong to.
constexpr char kArcvmVcpuCpuCgroup[] = "/sys/fs/cgroup/cpu/arcvm-vcpus";

}  // namespace

StartVmResponse Service::StartArcVm(StartArcVmRequest request,
                                    std::unique_ptr<dbus::MessageReader> reader,
                                    VmMemoryId vm_memory_id) {
  LOG(INFO) << "Received StartArcVm request";
  StartVmResponse response;
  response.set_status(VM_STATUS_FAILURE);

  VmInfo* vm_info = response.mutable_vm_info();
  vm_info->set_vm_type(VmInfo::ARC_VM);

  if (request.disks_size() > kMaxExtraDisks) {
    LOG(ERROR) << "Rejecting request with " << request.disks_size()
               << " extra disks";

    response.set_failure_reason("Too many extra disks");
    return response;
  }

  std::vector<Disk> disks;
  // The rootfs can be treated as a disk as well and needs to be added before
  // other disks.
  Disk::Config config{};
  config.o_direct = false;
  config.writable = request.rootfs_writable();
  const size_t rootfs_block_size = request.rootfs_block_size();
  if (rootfs_block_size) {
    config.block_size = rootfs_block_size;
  }
  disks.push_back(Disk(base::FilePath(kRootfsPath), config));
  for (const auto& disk : request.disks()) {
    if (!base::PathExists(base::FilePath(disk.path()))) {
      LOG(ERROR) << "Missing disk path: " << disk.path();
      response.set_failure_reason("One or more disk paths do not exist");
      return response;
    }
    config.writable = disk.writable();
    const size_t block_size = disk.block_size();
    if (block_size) {
      config.block_size = block_size;
    }
    disks.push_back(Disk(base::FilePath(disk.path()), config));
  }

  // Create the runtime directory.
  base::FilePath runtime_dir;
  if (!base::CreateTemporaryDirInDir(base::FilePath(kRuntimeDir), "vm.",
                                     &runtime_dir)) {
    PLOG(ERROR) << "Unable to create runtime directory for VM";

    response.set_failure_reason(
        "Internal error: unable to create runtime directory");
    return response;
  }

  // Allocate resources for the VM.
  uint32_t vsock_cid = vsock_cid_pool_.Allocate();
  if (vsock_cid == 0) {
    LOG(ERROR) << "Unable to allocate vsock context id";

    response.set_failure_reason("Unable to allocate vsock cid");
    return response;
  }
  vm_info->set_cid(vsock_cid);

  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New(bus_);
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";

    response.set_failure_reason("Unable to open network service client");
    return response;
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
    return response;
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

  if (request.has_balloon_policy()) {
    const auto& params = request.balloon_policy();
    features.balloon_policy_params = (LimitCacheBalloonPolicy::Params){
        .reclaim_target_cache = params.reclaim_target_cache(),
        .critical_target_cache = params.critical_target_cache(),
        .moderate_target_cache = params.moderate_target_cache()};
  }

  base::FilePath data_dir = base::FilePath(kAndroidDataDir);
  if (!base::PathExists(data_dir)) {
    LOG(WARNING) << "Android data directory does not exist";

    response.set_failure_reason("Android data directory does not exist");
    return response;
  }

  VmId vm_id(request.owner_id(), request.name());
  SendVmStartingUpSignal(vm_id, *vm_info);

  const std::vector<uid_t> privileged_quota_uids = {0};  // Root is privileged.
  std::string shared_data = CreateSharedDataParam(
      data_dir, "_data", true, false, true, privileged_quota_uids);
  std::string shared_data_media = CreateSharedDataParam(
      data_dir, "_data_media", false, true, true, privileged_quota_uids);

  const base::FilePath stub_dir(kStubVolumeSharedDir);
  std::string shared_stub = CreateSharedDataParam(stub_dir, "stub", false, true,
                                                  false, privileged_quota_uids);

  // TOOD(kansho): |non_rt_cpus_num|, |rt_cpus_num| and |affinity|
  // should be passed from chrome instead of |enable_rt_vcpu|.

  // By default we don't request any RT CPUs
  ArcVmCPUTopology topology(request.cpus(), 0);

  // We create only 1 RT VCPU for the time being
  if (request.enable_rt_vcpu())
    topology.SetNumRTCPUs(1);

  topology.CreateCPUAffinity();

  if (request.enable_rt_vcpu()) {
    params.emplace_back("isolcpus=" + topology.RTCPUMask());
    params.emplace_back("androidboot.rtcpus=" + topology.RTCPUMask());
    params.emplace_back("androidboot.non_rtcpus=" + topology.NonRTCPUMask());
  }

  params.emplace_back("ramoops.record_size=" +
                      std::to_string(kArcVmRamoopsRecordSize));
  params.emplace_back("ramoops.console_size=" +
                      std::to_string(kArcVmRamoopsConsoleSize));
  params.emplace_back("ramoops.ftrace_size=" +
                      std::to_string(kArcVmRamoopsFtraceSize));
  params.emplace_back("ramoops.pmsg_size=" +
                      std::to_string(kArcVmRamoopsPmsgSize));
  params.emplace_back("ramoops.dump_oops=1");

  VmBuilder vm_builder;
  vm_builder.AppendDisks(std::move(disks))
      .SetCpus(topology.NumCPUs())
      .AppendKernelParam(base::JoinString(params, " "))
      .AppendCustomParam("--vcpu-cgroup-path",
                         base::FilePath(kArcvmVcpuCpuCgroup).value())
      .AppendCustomParam("--android-fstab", kFstabPath)
      .AppendCustomParam(
          "--pstore", base::StringPrintf("path=%s,size=%" PRId64,
                                         kArcVmPstorePath, kArcVmRamoopsSize))
      .AppendSharedDir(shared_data)
      .AppendSharedDir(shared_data_media)
      .AppendSharedDir(shared_stub)
      .EnableSmt(false /* enable */)
      .EnablePerVmCoreScheduling(request.use_per_vm_core_scheduling())
      .SetWaylandSocket(request.vm().wayland_server());

  if (request.enable_rt_vcpu()) {
    vm_builder.AppendCustomParam("--rt-cpus", topology.RTCPUMask());
  }

  if (!topology.IsSymmetricCPU() && !topology.AffinityMask().empty()) {
    vm_builder.AppendCustomParam("--cpu-affinity", topology.AffinityMask());
  }

  if (!topology.IsSymmetricCPU() && !topology.CapacityMask().empty()) {
    vm_builder.AppendCustomParam("--cpu-capacity", topology.CapacityMask());
  }

  if (!topology.IsSymmetricCPU() && !topology.PackageMask().empty()) {
    for (auto& package : topology.PackageMask()) {
      vm_builder.AppendCustomParam("--cpu-cluster", package);
    }
  }

  if (request.use_hugepages()) {
    vm_builder.AppendCustomParam("--hugepages", "");
  }

  vm_builder.SetMemory(std::to_string(GetArcVmMemoryMiB(request)));

  /* Enable THP if the VM has at least 7G of memory */
  if (base::SysInfo::AmountOfPhysicalMemoryMB() >= 7 * 1024) {
    vm_builder.AppendCustomParam("--hugepages", "");
  }

  if (USE_CROSVM_SIBLINGS) {
    vm_builder.SetVmMemoryId(vm_memory_id);
  }

  auto vm = ArcVm::Create(base::FilePath(kKernelPath), vsock_cid,
                          std::move(network_client), std::move(server_proxy),
                          std::move(runtime_dir), vm_memory_id, features,
                          std::move(vm_builder));
  if (!vm) {
    LOG(ERROR) << "Unable to start VM";

    response.set_failure_reason("Unable to start VM");
    return response;
  }

  // ARCVM is ready.
  LOG(INFO) << "Started VM with pid " << vm->pid();

  response.set_success(true);
  response.set_status(VM_STATUS_RUNNING);
  vm_info->set_ipv4_address(vm->IPv4Address());
  vm_info->set_pid(vm->pid());

  SendVmStartedSignal(vm_id, *vm_info, response.status());

  vms_[vm_id] = std::move(vm);

  return response;
}

int64_t Service::GetArcVmMemoryMiB(const StartArcVmRequest& request) {
  int64_t memory_mib = request.memory_mib();
  if (memory_mib <= 0) {
    memory_mib = ::vm_tools::concierge::GetVmMemoryMiB();
  }
  return memory_mib;
}

}  // namespace concierge
}  // namespace vm_tools
