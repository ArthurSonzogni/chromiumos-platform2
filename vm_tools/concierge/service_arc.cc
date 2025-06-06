// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/cpu.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <chromeos/constants/vm_tools.h>
#include <dbus/vm_concierge/dbus-constants.h>
#include <libcrossystem/crossystem.h>
#include <metrics/metrics_library.h>
#include <vboot/crossystem.h>

#include "vm_tools/common/naming.h"
#include "vm_tools/common/pstore.h"
#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/arc_vm.h"
#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/feature_util.h"
#include "vm_tools/concierge/metrics/duration_recorder.h"
#include "vm_tools/concierge/network/arc_network.h"
#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/service_arc_utils.h"
#include "vm_tools/concierge/service_common.h"
#include "vm_tools/concierge/service_start_vm_helper.h"
#include "vm_tools/concierge/vm_util.h"
#include "vm_tools/concierge/vmm_swap_low_disk_policy.h"
#include "vm_tools/concierge/vmm_swap_metrics.h"

namespace vm_tools::concierge {

namespace {

// Android data directory.
constexpr char kAndroidDataDir[] = "/run/arcvm/android-data";

// Android stub volume directory for MyFiles and removable media.
constexpr char kStubVolumeSharedDir[] = "/run/arcvm/media";

// Path to the VM guest kernel.
constexpr char kKernelPath[] = "/opt/google/vms/android/vmlinux";
//
// Path to the GKI guest kernel.
constexpr char kGkiPath[] = "/opt/google/vms/android/gki";

// Path to the VM rootfs image file.
constexpr char kRootfsPath[] = "/opt/google/vms/android/system.raw.img";

// Path to the VM ramdisk file.
constexpr char kRamdiskPath[] = "/opt/google/vms/android/ramdisk.img";

// Path to the VM fstab file.
constexpr char kFstabPath[] = "/run/arcvm/host_generated/fstab";

// Path to the properties resolved by arcvm
constexpr char kModifiedPropPath[] = "/run/arcvm/host_generated/modified.prop";

// A feature name for enabling jemalloc multi-arena settings
// in low memory devices.
constexpr char kArcVmLowMemJemallocArenasFeatureName[] =
    "CrOSLateBootArcVmLowMemJemallocArenas";

// A feature name for using low latency (5ms) AAudio MMAP
constexpr char kArcVmAAudioMMAPLowLatencyFeatureName[] =
    "CrOSLateBootArcVmAAudioMMAPLowLatency";

// The number of milliseconds ARCVM clients will wait before aborting a kill
// decision.
constexpr base::TimeDelta kVmMemoryManagementArcKillDecisionTimeout =
    base::Milliseconds(100);

// Needs to be const as libfeatures does pointers checking.
const VariationsFeature kArcVmLowMemJemallocArenasFeature{
    kArcVmLowMemJemallocArenasFeatureName, FEATURE_DISABLED_BY_DEFAULT};

const VariationsFeature kArcVmAAudioMMAPLowLatencyFeature{
    kArcVmAAudioMMAPLowLatencyFeatureName, FEATURE_DISABLED_BY_DEFAULT};

// If enabled, provides props to ARCVM to override the default PSI thresholds
// for LMKD.
constexpr char kOverrideLmkdPsiDefaultsFeatureName[] =
    "CrOSLateBootOverrideLmkdPsiDefaults";

// The PSI threshold in ms for partial stalls. A lower value will cause ARC to
// attempt to kill low priority (cached) apps sooner.
constexpr char kLmkdPartialStallMsParam[] = "PartialStallMs";
// By default use the same default value as LMKD.
constexpr int kLmkdPartialStallMsDefault = 70;

// The PSI threshold in ms for complete stalls. A lower value will cause ARC to
// attempt to kill apps of any priority (including perceptible) sooner.
constexpr char kLmkdCompleteStallMsParam[] = "CompleteStallMs";
// By default use the same default value as LMKD.
constexpr int kLmkdCompleteStallMsDefault = 700;

const VariationsFeature kOverrideLmkdPsiDefaultsFeature{
    kOverrideLmkdPsiDefaultsFeatureName, FEATURE_DISABLED_BY_DEFAULT};

// Returns |image_path| on production. Returns a canonicalized path of the image
// file when in dev mode.
base::FilePath GetImagePath(const base::FilePath& image_path,
                            bool is_dev_mode) {
  if (!is_dev_mode) {
    return image_path;
  }

  // When in dev mode, the Android images might be on the stateful partition and
  // |kRootfsPath| might be a symlink to the stateful partition image file. In
  // that case, we need to use the resolved path so that brillo::SafeFD calls
  // can handle the path without errors. The same is true for vendor.raw.image
  // too. On the other hand, when in production mode, we should NEVER do the
  // special handling. In production, the images files in /opt should NEVER ever
  // be a symlink.

  // We cannot use base::NormalizeFilePath because the function fails
  // if |path| points to a directory (for Windows compatibility.)
  char buf[PATH_MAX] = {};
  if (realpath(image_path.value().c_str(), buf)) {
    return base::FilePath(buf);
  }
  if (errno != ENOENT) {
    PLOG(WARNING) << "Failed to resolve " << image_path.value();
  }
  return image_path;
}

// Returns the period size to use for AAudio MMAP.
// - If low latency is enabled and CPU is supported, use 256 frames which has
//   lower latency but may cause audio glitches.
// - If not, use 480 frames.
int GetAAudioMMAPPeriodSize(bool is_low_latency_enabled) {
  // Support any CPU that is not Celeron or Pentium.
  const std::string cpu_model_name =
      base::ToLowerASCII(base::CPU().cpu_brand());
  const bool supported_cpu =
      (cpu_model_name.find("celeron") == std::string::npos &&
       cpu_model_name.find("pentium") == std::string::npos);
  return is_low_latency_enabled && supported_cpu ? 256 : 480;
}

bool CreateMetadataImageIfNotExist(const base::FilePath& disk_path) {
  if (disk_path.value() == kEmptyDiskPath || base::PathExists(disk_path)) {
    return true;
  }
  base::ScopedFD fd(open(disk_path.value().c_str(), O_CREAT | O_WRONLY, 0600));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open /metadata disk at " << disk_path.value();
    return false;
  }
  if (fallocate(fd.get(), 0, 0, kMetadataDiskSize) != 0) {
    PLOG(ERROR) << "Failed to create /metadata disk at " << disk_path.value();
    unlink(disk_path.value().c_str());
    return false;
  }
  LOG(INFO) << "Successfully created /metadata disk at " << disk_path.value();
  return true;
}

base::ScopedFD CreateRuntimeSystemPropertiesDisk(
    const base::FilePath& disk_path) {
  base::ScopedFD fd(open(disk_path.value().c_str(),
                         O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open disk for runtime system properties at "
                << disk_path.value();
    return base::ScopedFD();
  }
  LOG(INFO) << "Successfully created runtime system properties disk at "
            << disk_path.value();
  return fd;
}

bool WriteRuntimeSystemPropertiesToDisk(const base::FilePath& disk_path,
                                        base::ScopedFD disk_fd,
                                        const std::string& runtime_properties) {
  size_t num_blocks =
      (runtime_properties.size() + kDefaultBlockSize - 1) / kDefaultBlockSize;

  if (!disk_fd.is_valid()) {
    PLOG(ERROR) << "Unable to open system property disk for writing.";
    return false;
  }

  // Allocate the disk so that it is aligned to kDefaultBlockSize.
  if (fallocate(disk_fd.get(), 0, 0, num_blocks * kDefaultBlockSize) != 0) {
    PLOG(ERROR) << "Failed to allocate disk for runtime system properties at "
                << disk_path.value();
    unlink(disk_path.value().c_str());
    return false;
  }

  if (!base::WriteFileDescriptor(disk_fd.get(), runtime_properties)) {
    PLOG(ERROR) << "Failed to write runtime system properties to disk";
    unlink(disk_path.value().c_str());
    return false;
  }

  return true;
}

// This function boosts the arcvm and arcvm-vcpus cgroups, by applying the
// cpu.uclamp.min boost for all the vcpus and crosvm services and enabling the
// latency_sensitive attribute.
// Appropriate boost is required for the little.BIG architecture, to reduce
// latency and improve general ARCVM experience. b/217825939
bool BoostArcVmCgroups(double boost_value) {
  bool ret = true;
  const base::FilePath arcvm_cgroup = base::FilePath(kArcvmCpuCgroup);
  const base::FilePath arcvm_vcpu_cgroup = base::FilePath(kArcvmVcpuCpuCgroup);

  if (!UpdateCpuLatencySensitive(arcvm_cgroup, true)) {
    ret = false;
  }

  if (!UpdateCpuLatencySensitive(arcvm_vcpu_cgroup, true)) {
    ret = false;
  }

  if (!UpdateCpuUclampMin(arcvm_cgroup, boost_value)) {
    ret = false;
  }

  if (!UpdateCpuUclampMin(arcvm_vcpu_cgroup, boost_value)) {
    ret = false;
  }

  return ret;
}

}  // namespace

void Service::StartArcVm(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        vm_tools::concierge::StartVmResponse>> response_cb,
    const vm_tools::concierge::StartArcVmRequest& request) {
  ASYNC_SERVICE_METHOD();

  StartVmResponse response;
  // We change to a success status later if necessary.
  response.set_status(VM_STATUS_FAILURE);

  if (!CheckStartVmPreconditions(request, &response)) {
    response_cb->Return(response);
    return;
  }

  StartArcVmInternal(request, response);
  response_cb->Return(response);
}

StartVmResponse Service::StartArcVmInternal(StartArcVmRequest request,
                                            StartVmResponse& response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Log how long it takes to start the VM.
  metrics::DurationRecorder duration_recorder(
      raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get()),
      apps::VmType::ARCVM, metrics::DurationRecorder::Event::kVmStart);

  if (request.disks_size() > kMaxExtraDisks) {
    LOG(ERROR) << "Rejecting request with " << request.disks_size()
               << " extra disks";

    response.set_failure_reason("Too many extra disks");
    return response;
  }

  // TODO(b/219677829): Move VM configuration logic from chrome to concierge and
  // remove this check.
  if (!ValidateStartArcVmRequest(request)) {
    response.set_failure_reason("Invalid request");
    return response;
  }

  // Allocate vsock cid for the VM.
  uint32_t vsock_cid = vsock_cid_pool_.Allocate();
  std::string syslog_tag = base::StringPrintf("ARCVM(%u)", vsock_cid);

  const std::vector<uid_t> privileged_quota_uids = {0};  // Root is privileged.

  std::optional<VhostUserFrontParam> shared_stub_vhost_user_front_param =
      InvokeVhostUserFsBackend(
          SharedDirParam{.data_dir = base::FilePath(kStubVolumeSharedDir),
                         .tag = "stub",
                         .uid_map = kStubUidMap,
                         .gid_map = kStubGidMap,
                         .enable_caches = SharedDirParam::Cache::kAuto,
                         .ascii_casefold = true,
                         .posix_acl = false,
                         .max_dynamic_perm = 2,
                         .max_dynamic_xattr = 2,
                         .privileged_quota_uids = privileged_quota_uids},
          syslog_tag);
  if (!shared_stub_vhost_user_front_param.has_value()) {
    response.set_failure_reason(
        "Fail to create stub device vhost user parameters");
    return response;
  }

  base::FilePath data_dir = base::FilePath(kAndroidDataDir);
  if (!base::PathExists(data_dir)) {
    LOG(WARNING) << "Android data directory does not exist";

    response.set_failure_reason("Android data directory does not exist");
    return response;
  }

  SharedDirParam shared_data{.data_dir = data_dir,
                             .tag = "_data",
                             .uid_map = kAndroidUidMap,
                             .gid_map = kAndroidGidMap,
                             .enable_caches = SharedDirParam::Cache::kAlways,
                             .ascii_casefold = false,
                             .posix_acl = true,
                             .privileged_quota_uids = privileged_quota_uids};
  SharedDirParam shared_data_media{
      .data_dir = data_dir,
      .tag = "_data_media",
      .uid_map = kAndroidUidMap,
      .gid_map = kAndroidGidMap,
      .enable_caches = SharedDirParam::Cache::kAlways,
      .ascii_casefold = true,
      .posix_acl = true,
      .privileged_quota_uids = privileged_quota_uids};

  // Create the /metadata disk if it is requested but does not yet exist.
  // (go/arcvm-metadata)
  if (request.disks().size() > kMetadataDiskIndex) {
    const base::FilePath disk_path(request.disks()[kMetadataDiskIndex].path());
    if (!CreateMetadataImageIfNotExist(disk_path)) {
      response.set_failure_reason("Failed to create /metadata disk");
      return response;
    }
  }

  // Create the disk to hold system properties generated before boot.
  base::FilePath sysprop_disk_path;
  base::ScopedFD sysprop_disk_fd;
  if (request.disks().size() > kPropertiesDiskIndex) {
    sysprop_disk_path =
        base::FilePath::FromASCII(request.disks()[kPropertiesDiskIndex].path());
    sysprop_disk_fd = CreateRuntimeSystemPropertiesDisk(sysprop_disk_path);
    if (!sysprop_disk_fd.is_valid()) {
      LOG(ERROR) << "Failed to create disk to hold runtime system properties";
      response.set_failure_reason(
          "Failed to create runtime system properties disk");
      return response;
    }
  } else {
    LOG(ERROR)
        << "No disk requested to share runtime system properties with ARCVM";
    response.set_failure_reason(
        "Request missing runtime system properties disk");
    return response;
  }

  VmBuilder vm_builder;
  // Exists just to keep FDs around for crosvm to inherit
  std::vector<brillo::SafeFD> owned_fds;
  auto root_fd = brillo::SafeFD::Root();

  if (brillo::SafeFD::IsError(root_fd.second)) {
    LOG(ERROR) << "Could not open root directory: "
               << static_cast<int>(root_fd.second);
    response.set_failure_reason("Could not open root directory");
    return response;
  }

  // The rootfs can be treated as a disk as well and needs to be added before
  // other disks.
  VmBuilder::Disk rootdisk{
      .writable = request.rootfs_writable(),
      .o_direct = request.rootfs_o_direct(),
      .multiple_workers = request.rootfs_multiple_workers()};
  const size_t rootfs_block_size = request.rootfs_block_size();
  if (rootfs_block_size) {
    rootdisk.block_size = rootfs_block_size;
  }
  const bool is_dev_mode = (VbGetSystemPropertyInt("cros_debug") == 1);
  auto rootfsPath = GetImagePath(base::FilePath(kRootfsPath), is_dev_mode);
  auto failure_reason =
      ConvertToFdBasedPath(root_fd.first, &rootfsPath,
                           rootdisk.writable ? O_RDWR : O_RDONLY, owned_fds);
  if (!failure_reason.empty()) {
    LOG(ERROR) << "Could not open rootfs image" << rootfsPath;
    response.set_failure_reason("Rootfs path does not exist");
    return response;
  }
  rootdisk.path = rootfsPath;
  vm_builder.AppendDisk(rootdisk);

  for (const auto& d : request.disks()) {
    VmBuilder::Disk disk{
        .path = GetImagePath(base::FilePath(d.path()), is_dev_mode),
        .writable = d.writable(),
        .o_direct = d.o_direct(),
        .multiple_workers = d.multiple_workers()};
    if (!base::PathExists(disk.path)) {
      LOG(ERROR) << "Missing disk path: " << disk.path;
      response.set_failure_reason("One or more disk paths do not exist");
      return response;
    }
    const size_t block_size = d.block_size();
    if (block_size) {
      disk.block_size = block_size;
    }
    failure_reason =
        ConvertToFdBasedPath(root_fd.first, &disk.path,
                             disk.writable ? O_RDWR : O_RDONLY, owned_fds);

    if (!failure_reason.empty()) {
      LOG(ERROR) << "Could not open disk file";
      response.set_failure_reason(failure_reason);
      return response;
    }

    vm_builder.AppendDisk(disk);
  }

  base::FilePath data_disk_path;
  if (request.disks().size() > kDataDiskIndex) {
    const std::string disk_path = request.disks()[kDataDiskIndex].path();
    if (IsValidDataImagePath(base::FilePath(disk_path))) {
      data_disk_path = base::FilePath(disk_path);
    }
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
  std::unique_ptr<ArcNetwork> network = ArcNetwork::Create(bus_, vsock_cid);
  if (!network) {
    LOG(ERROR) << "Unable to open networking service";

    response.set_failure_reason("Unable to open network service");
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

  crossystem::Crossystem cros_system;
  std::vector<std::string> params =
      ArcVm::GetKernelParams(cros_system, request, seneschal_server_port);

  // Start the VM and build the response.
  ArcVmFeatures features;
  features.rootfs_writable = request.rootfs_writable();
  features.use_dev_conf = !request.ignore_dev_conf();
  features.low_mem_jemalloc_arenas_enabled =
      feature::PlatformFeatures::Get()->IsEnabledBlocking(
          kArcVmLowMemJemallocArenasFeature);

  params.emplace_back(base::StringPrintf(
      "androidboot.lmkd.vm_memory_management_kill_"
      "decision_timeout_ms=%" PRId64,
      kVmMemoryManagementArcKillDecisionTimeout.InMilliseconds()));
  params.emplace_back(base::StringPrintf(
      "androidboot.lmkd.vm_memory_management_reclaim_port=%d",
      kVmMemoryManagementReclaimServerPort));
  params.emplace_back(
      base::StringPrintf("androidboot.lmkd.vm_memory_management_kills_port=%d",
                         kVmMemoryManagementKillsServerPort));

  // For GKI, the MGLRU admin interface moved to a module and has a different
  // path.
  // TODO(b:342318950) When GKI is enabled by default, update the default
  // MGLRU admin path in the arc lmkd hooks project and remove this override.
  if (request.use_gki()) {
    params.emplace_back("androidboot.lmkd.mglru_module=true");
  }

  const feature::PlatformFeatures::ParamsResult result =
      feature::PlatformFeatures::Get()->GetParamsAndEnabledBlocking(
          {&kOverrideLmkdPsiDefaultsFeature});

  const auto result_iter = result.find(kOverrideLmkdPsiDefaultsFeatureName);
  if (result_iter != result.end() && result_iter->second.enabled) {
    const auto& entry = result_iter->second;

    int partial_stall_ms = FindIntValue(entry.params, kLmkdPartialStallMsParam)
                               .value_or(kLmkdPartialStallMsDefault);
    int complete_stall_ms =
        FindIntValue(entry.params, kLmkdCompleteStallMsParam)
            .value_or(kLmkdCompleteStallMsDefault);

    LOG(INFO)
        << "Overriding lmkd default PSI thresholds. psi_partial_stall_ms: "
        << partial_stall_ms << " psi_complete_stall_ms: " << complete_stall_ms;

    params.emplace_back(base::StringPrintf(
        "androidboot.lmkd.psi_partial_stall_ms=%d", partial_stall_ms));
    params.emplace_back(base::StringPrintf(
        "androidboot.lmkd.psi_complete_stall_ms=%d", complete_stall_ms));
  }

  params.emplace_back("androidboot.audio.aaudio_mmap_enabled=1");
  bool aaudio_low_latency_enabled =
      feature::PlatformFeatures::Get()->IsEnabledBlocking(
          kArcVmAAudioMMAPLowLatencyFeature);
  params.emplace_back(
      base::StringPrintf("androidboot.audio.aaudio_mmap_period_size=%d",
                         GetAAudioMMAPPeriodSize(aaudio_low_latency_enabled)));

  // Workaround for slow vm-host IPC when recording video.
  params.emplace_back("androidboot.camera.async_process_capture_request=true");

  const auto pstore_path = GetPstoreDest(request.owner_id());

  VmId vm_id(request.owner_id(), request.name());
  SendVmStartingUpSignal(vm_id, apps::VmType::ARCVM, vsock_cid);

  // TOOD(kansho): |non_rt_cpus_num|, |rt_cpus_num| and |affinity|
  // should be passed from chrome instead of |enable_rt_vcpu|.

  // By default we don't request any RT CPUs
  ArcVmCPUTopology topology(request.cpus(), 0);

  // We create only 1 RT VCPU for the time being
  if (request.enable_rt_vcpu()) {
    topology.SetNumRTCPUs(1);
  }

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

  // Customize cache size of squashfs metadata for faster guest OS
  // boot/provisioning.
  params.emplace_back("squashfs.cached_blks=20");

  if (request.has_virtual_swap_config() &&
      request.virtual_swap_config().size_mib() != 0) {
    vm_builder.AppendPmemDevice(VmBuilder::PmemDevice{
        .path = "arcvm_virtual_swap",
        .writable = true,
        .vma_size = MiB(request.virtual_swap_config().size_mib()),
        .swap_interval_ms = request.virtual_swap_config().swap_interval_ms()});
    params.emplace_back("androidboot.arc.swap_device=/dev/block/pmem0");
  }

  vm_builder.SetCpus(topology.NumCPUs())
      .AppendCustomParam("--vcpu-cgroup-path",
                         base::FilePath(kArcvmVcpuCpuCgroup).value())
      .AppendCustomParam(
          "--pstore",
          base::StringPrintf("path=%s,size=%" PRId64,
                             pstore_path.value().c_str(), kArcVmRamoopsSize))
      .AppendSharedDir(shared_data)
      .AppendSharedDir(shared_data_media)
      .AppendVhostUserFrontend(
          std::move(shared_stub_vhost_user_front_param.value()))
      .EnableSmt(false /* enable */)
      .EnablePerVmCoreScheduling(request.use_per_vm_core_scheduling())
      .SetWaylandSocket(request.vm().wayland_server())
      .SetSyslogTag(syslog_tag);

  base::FilePath kernel_path;
  if (request.use_gki()) {
    kernel_path = base::FilePath(kGkiPath);
    vm_builder.AppendCustomParam("--initrd", kRamdiskPath);
    // This is set to 0 by the GKI kernel so we set back to the default.
    params.emplace_back("8250.nr_uarts=4");
    // TODO(b/331748554): The GKI doesn't have the pvclock driver.
    vm_builder.EnablePvClock(false /* disable */);
  } else {
    kernel_path = base::FilePath(kKernelPath);
    vm_builder.AppendCustomParam("--android-fstab", kFstabPath);
    if (ALLOW_VIRTIO_PVCLOCK) {
      vm_builder.EnablePvClock(request.enable_pvclock() /* enable */);
    }
  }

  if (request.enable_rt_vcpu()) {
    vm_builder.AppendCustomParam("--rt-cpus", topology.RTCPUMask());
  }

  if (!topology.IsSymmetricCPU() && !topology.AffinityMask().empty()) {
    vm_builder.AppendCustomParam("--cpu-affinity", topology.AffinityMask());
  }

  if (!topology.IsSymmetricCPU() && !topology.CapacityMask().empty()) {
    vm_builder.AppendCustomParam("--cpu-capacity", topology.CapacityMask());
    // Rise the uclamp_min value of the top-app in the ARCVM. This is a
    // performance tuning for games on big.LITTLE platform and Capacity
    // Aware Scheduler (CAS) on Linux.
    params.emplace_back(base::StringPrintf(
        "androidboot.arc_top_app_uclamp_min=%d", topology.TopAppUclampMin()));
  }

  if (!topology.IsSymmetricCPU() && !topology.PackageMask().empty()) {
    for (auto& package : topology.PackageMask()) {
      vm_builder.AppendCustomParam("--cpu-cluster", package);
    }
  }

  if (request.lock_guest_memory()) {
    vm_builder.AppendCustomParam("--lock-guest-memory", "");
  }

  if (request.use_hugepages()) {
    vm_builder.AppendCustomParam("--hugepages", "");
  }

  if (USE_CROSVM_LIMIT_ARMV8PMU_COUNTERS) {
    params.emplace_back("armv8pmu_max_events=20");
  }

  const int64_t memory_mib =
      request.memory_mib() > 0 ? request.memory_mib() : GetVmMemoryMiB();
  vm_builder.SetMemory(std::to_string(memory_mib));

  /* Enable THP if the VM has at least 7G of memory */
  if (base::SysInfo::AmountOfPhysicalMemoryMB() >= 7 * 1024) {
    vm_builder.AppendCustomParam("--hugepages", "");
  }

  base::FilePath swap_dir = GetCryptohomePath(request.owner_id());
  std::unique_ptr<VmmSwapLowDiskPolicy> vmm_swap_low_disk_policy =
      std::make_unique<VmmSwapLowDiskPolicy>(
          swap_dir,
          raw_ref<spaced::DiskUsageProxy>::from_ptr(disk_usage_proxy_.get()));
  base::FilePath vmm_swap_usage_path =
      GetVmmSwapUsageHistoryPath(request.owner_id());

  if (request.enable_vmm_swap()) {
    vm_builder.SetVmmSwapDir(swap_dir);
  }

  if (request.enable_s2idle()) {
    // Force PCI config access via MMIO when s2idle is enabled to avoid the
    // need for VM exits when reading the PCI config space. This substantially
    // reduces how long it takes to exit s2idle.
    vm_builder.AppendCustomParam("--break-linux-pci-config-io", "");
  }

  // Write runtime properties to the file backing the properties block device.
  // We do not use kModifiedPropPath to back the device directly because
  // concierge only has read access to that file, and here we need the ability
  // to append additional properties and resize the file to be block-aligned.
  static base::FilePath modified_prop_path(kModifiedPropPath);
  std::string props;
  if (!base::ReadFileToString(modified_prop_path, &props)) {
    PLOG(ERROR) << "Failed to read " << modified_prop_path << " to string";

    response.set_failure_reason("Unable to read modified.prop");
    return response;
  }

  if (!RelocateBootProps(&params, &props)) {
    response.set_failure_reason(
        "Unable to move androidboot command-line properties");
    return response;
  }

  if (!WriteRuntimeSystemPropertiesToDisk(sysprop_disk_path,
                                          std::move(sysprop_disk_fd), props)) {
    PLOG(ERROR) << "Failed to write runtime system properties to "
                << sysprop_disk_path.value();
    response.set_failure_reason(
        "Unable to write runtime system properties to disk");
    return response;
  }

  vm_builder.AppendKernelParam(base::JoinString(params, " "));

  base::RepeatingCallback<void(SwappingState)> vm_swapping_notify_callback =
      base::BindRepeating(&Service::NotifyVmSwapping,
                          weak_ptr_factory_.GetWeakPtr(), vm_id);

  auto vm = ArcVm::Create(ArcVm::Config{
      .kernel = kernel_path,
      .vsock_cid = vsock_cid,
      .network = std::move(network),
      .seneschal_server_proxy = std::move(server_proxy),
      .is_vmm_swap_enabled = request.enable_vmm_swap(),
      .vmm_swap_metrics = std::make_unique<VmmSwapMetrics>(
          apps::VmType::ARCVM,
          raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get())),
      .vmm_swap_low_disk_policy = std::move(vmm_swap_low_disk_policy),
      .vmm_swap_tbw_policy =
          raw_ref<VmmSwapTbwPolicy>::from_ptr(vmm_swap_tbw_policy_.get()),
      .vmm_swap_usage_path = vmm_swap_usage_path,
      .vm_swapping_notify_callback = std::move(vm_swapping_notify_callback),
      .virtio_blk_metrics = std::make_unique<VirtioBlkMetrics>(
          raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get())),
      .guest_memory_size = MiB(memory_mib),
      .runtime_dir = std::move(runtime_dir),
      .data_disk_path = std::move(data_disk_path),
      .features = features,
      .vm_builder = std::move(vm_builder)});
  if (!vm) {
    LOG(ERROR) << "Unable to start VM";

    response.set_failure_reason("Unable to start VM");
    return response;
  }

  // ARCVM is ready.
  LOG(INFO) << "Started VM with pid " << vm->pid();

  vms_[vm_id] = std::move(vm);

  HandleControlSocketReady(vm_id);

  double vm_boost = topology.GlobalVMBoost();
  if (vm_boost > 0.0) {
    if (!BoostArcVmCgroups(vm_boost)) {
      LOG(WARNING) << "Failed to boost the ARCVM to " << vm_boost;
    }
  }

  response.set_success(true);
  response.set_status(VM_STATUS_RUNNING);
  *response.mutable_vm_info() = ToVmInfo(vms_[vm_id]->GetInfo(), true);

  return response;
}

void Service::ArcVmCompleteBoot(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        ArcVmCompleteBootResponse>> response_cb,
    const ArcVmCompleteBootRequest& request) {
  ASYNC_SERVICE_METHOD();

  ArcVmCompleteBootResponse response;

  VmId vm_id(request.owner_id(), kArcVmName);
  if (!CheckVmNameAndOwner(request, response)) {
    response.set_result(ArcVmCompleteBootResult::BAD_REQUEST);
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Unable to locate ArcVm instance";
    response.set_result(ArcVmCompleteBootResult::ARCVM_NOT_FOUND);
    response_cb->Return(response);
    return;
  }

  ArcVm* vm = dynamic_cast<ArcVm*>(iter->second.get());
  vm->HandleUserlandReady();

  // Notify the VM guest userland ready
  SendVmGuestUserlandReadySignal(vm_id,
                                 GuestUserlandReady::ARC_BRIDGE_CONNECTED);

  if (vm_memory_management_service_) {
    vm_memory_management_service_->NotifyVmBootComplete(vm->GetInfo().cid);
  }

  response.set_result(ArcVmCompleteBootResult::SUCCESS);
  response_cb->Return(response);
}

}  // namespace vm_tools::concierge
