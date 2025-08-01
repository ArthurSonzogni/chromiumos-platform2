// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <net/route.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

// clang-format off
#include <sys/socket.h>
#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h
// clang-format on

#include <zstd.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <base/base64url.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/span.h>
#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_path_watcher.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/format_macros.h>
#include <base/functional/bind.h>
#include <base/functional/bind_internal.h>
#include <base/functional/callback.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/hash/md5.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <base/notreached.h>
#include <base/posix/eintr_wrapper.h>
#include <base/process/launch.h>
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/synchronization/waitable_event.h>
#include <base/system/sys_info.h>
#include <base/task/bind_post_task.h>
#include <base/task/single_thread_task_runner.h>
#include <base/task/thread_pool.h>
#include <base/time/time.h>
#include <base/uuid.h>
#include <base/version.h>
#include <blkid/blkid.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/files/safe_fd.h>
#include <brillo/osrelease_reader.h>
#include <brillo/process/process.h>
#include <chromeos/constants/vm_tools.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <dbus/error.h>
#include <dbus/object_proxy.h>
#include <dbus/shadercached/dbus-constants.h>
#include <dbus/vm_concierge/dbus-constants.h>
#include <google/protobuf/repeated_field.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_writer.h>
#include <spaced/dbus-proxies.h>
#include <spaced/disk_usage_proxy.h>
#include <vm_applications/apps.pb.h>
#include <vm_cicerone/cicerone_service.pb.h>
#include <vm_concierge/concierge_service.pb.h>
#include <vm_protos/proto_bindings/vm_guest.pb.h>
#include <vm_protos/proto_bindings/vm_host.pb.h>

#include "vm_tools/common/naming.h"
#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/arc_vm.h"
#include "vm_tools/concierge/baguette_version.h"
#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/dbus_adaptor.h"
#include "vm_tools/concierge/dbus_proxy_util.h"
#include "vm_tools/concierge/disk_image.h"
#include "vm_tools/concierge/dlc_helper.h"
#include "vm_tools/concierge/feature_util.h"
#include "vm_tools/concierge/metrics/duration_recorder.h"
#include "vm_tools/concierge/mm/resize_priority.h"
#include "vm_tools/concierge/network/baguette_network.h"
#include "vm_tools/concierge/network/borealis_network.h"
#include "vm_tools/concierge/network/bruschetta_network.h"
#include "vm_tools/concierge/network/guest_os_network.h"
#include "vm_tools/concierge/network/termina_network.h"
#include "vm_tools/concierge/plugin_vm.h"
#include "vm_tools/concierge/plugin_vm_helper.h"
#include "vm_tools/concierge/seneschal_server_proxy.h"
#include "vm_tools/concierge/service_common.h"
#include "vm_tools/concierge/service_start_vm_helper.h"
#include "vm_tools/concierge/shadercached_helper.h"
#include "vm_tools/concierge/ssh_keys.h"
#include "vm_tools/concierge/termina_vm.h"
#include "vm_tools/concierge/thread_utils.h"
#include "vm_tools/concierge/tracing.h"
#include "vm_tools/concierge/untrusted_vm_utils.h"
#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vm_builder.h"
#include "vm_tools/concierge/vm_permission_interface.h"
#include "vm_tools/concierge/vm_util.h"
#include "vm_tools/concierge/vm_wl_interface.h"
#include "vm_tools/concierge/vmplugin_dispatcher_interface.h"

namespace vm_tools::concierge {

namespace {

// How long we should wait for a VM to start up.
// While this timeout might be high, it's meant to be a final failure point, not
// the lower bound of how long it takes.  On a loaded system (like extracting
// large compressed files), it could take 10 seconds to boot.
constexpr base::TimeDelta kVmStartupDefaultTimeout = base::Seconds(60);
// Borealis has a longer default timeout, as it can take a long time to create
// its swap file on eMMC devices.
constexpr base::TimeDelta kBorealisVmStartupDefaultTimeout = base::Seconds(180);

// crosvm log directory name.
constexpr char kCrosvmLogDir[] = "log";

// Extension for crosvm log files
constexpr char kCrosvmLogFileExt[] = "log";

// Extension for vmlog_forwarder listener sockets.
constexpr char kCrosvmLogSocketExt[] = "lsock";

// crosvm gpu cache directory name.
constexpr char kCrosvmGpuCacheDir[] = "gpucache";

// Path to system boot_id file.
constexpr char kBootIdFile[] = "/proc/sys/kernel/random/boot_id";

// File extension for raw disk types
constexpr char kRawImageExtension[] = ".img";

// File extension for qcow2 disk types
constexpr char kQcowImageExtension[] = ".qcow2";

// File extension for Plugin VMs disk types
constexpr char kPluginVmImageExtension[] = ".pvm";

// Valid file extensions for disk images
constexpr const char* kDiskImageExtensions[] = {kRawImageExtension,
                                                kQcowImageExtension, nullptr};

// Valid file extensions for Plugin VM images
constexpr const char* kPluginVmImageExtensions[] = {kPluginVmImageExtension,
                                                    nullptr};

constexpr uint64_t kMinimumDiskSize = GiB(1);
constexpr uint64_t kDiskSizeMask = ~4095ll;  // Round to disk block size.

// vmlog_forwarder relies on creating a socket for crosvm to receive log
// messages. Socket paths may only be 108 character long. Further, while Linux
// actually allows for 108 non-null bytes to be used, the rust interface to bind
// only allows for 107, with the last byte always being null.
//
// We can abbreviate the directories in the path by opening the target directory
// and using /proc/self/fd/ to access it, but this still uses up
// 21 + (fd digits) characters on the prefix and file extension. This leaves us
// with 86 - (fd digits) characters for the base64 encoding of the VM
// name. Base64 always produces encoding that are a multiple of 4 digits long,
// so we can either allow for 63/84 characters before/after encoding, or
// 60/80. The first will break if our file descriptor numbers ever go above 99,
// which seems unlikely but not impossible. We can definitely be sure they won't
// go above 99,999, however.
constexpr int kMaxVmNameLength = 60;

constexpr uint64_t kDefaultIoLimit = MiB(1);

// How often we should broadcast state of a disk operation (import or export).
constexpr base::TimeDelta kDiskOpReportInterval = base::Seconds(1);

// Path to cpu information directories
constexpr char kCpuInfosPath[] = "/sys/devices/system/cpu/";

// Path of system timezone file.
constexpr char kLocaltimePath[] = "/etc/localtime";
// Path to zone info directory in host.
constexpr char kZoneInfoPath[] = "/usr/share/zoneinfo";

// Feature name of per-boot-vm-shader-cache
constexpr char kPerBootVmShaderCacheFeatureName[] =
    "CrOSLateBootVmPerBootShaderCache";

// Needs to be const as libfeatures does pointers checking.
const VariationsFeature kPerBootVmShaderCacheFeature{
    kPerBootVmShaderCacheFeatureName, FEATURE_DISABLED_BY_DEFAULT};

// Feature name of borealis-vcpu-tweaks
constexpr char kBorealisVcpuTweaksFeatureName[] =
    "CrOSLateBootBorealisVcpuTweaks";

// Feature name of borealis-provision.
constexpr char kBorealisProvisionFeature[] = "BorealisProvision";

// A feature name for throttling ARCVM's crosvm with cpu.cfs_quota_us.
constexpr char kArcVmInitialThrottleFeatureName[] =
    "CrOSLateBootArcVmInitialThrottle";
// A parameter name for |kArcVmInitialThrottleFeatureName|. Can be 1 to 100,
// or -1 (disabled).
constexpr char kArcVmInitialThrottleFeatureQuotaParam[] = "quota";

// Needs to be const as libfeatures does pointers checking.
const VariationsFeature kArcVmInitialThrottleFeature{
    kArcVmInitialThrottleFeatureName, FEATURE_DISABLED_BY_DEFAULT};

const VariationsFeature kBorealisVcpuTweaksFeature{
    kBorealisVcpuTweaksFeatureName, FEATURE_DISABLED_BY_DEFAULT};

// Rational for setting bytes-per-inode to 32KiB (rather than the default 16
// KiB) in go/borealis-inode.
const uint64_t kExt4BytesPerInode = 32768;

// Opts to be used when making an ext4 image. Note: these were specifically
// selected for Borealis, please take care when using outside of Borealis
// (especially the casefold feature).
const std::vector<std::string> kExtMkfsOpts = {
    "-Elazy_itable_init=0,lazy_journal_init=0,discard", "-Ocasefold",
    "-i" + std::to_string(kExt4BytesPerInode)};

// A TBW limit that is unlikely to impact disk health over the lifetime of a
// given 32GB device.
constexpr int64_t kTbwTargetForVmmSwapPerDay = 550ull * 1000 * 1000;
// The reference disk size used to determine the base TBW target.
constexpr int64_t kTbwTargetForVmmSwapReferenceDiskSize =
    32ull * 1000 * 1000 * 1000;
// Maximum daily TBW budget for vmm-swap - if we're writing more than this,
// then the user is using ARCVM enough that we don't want to activate vmm-swap.
constexpr int64_t kTbwMaxForVmmSwapPerDay = 2ull * 1000 * 1000 * 1000;
// The path to the history file for VmmSwapTbwPolicy.
constexpr char kVmmSwapTbwHistoryFilePath[] =
    "/var/lib/vm_concierge/vmm_swap_policy/tbw_history2";

// Maximum size of logs to send through D-Bus. Must be less than the maximum
// D-Bus array length (64 MiB) and the configured maximum message size for the
// system bus (usually 32 MiB).
constexpr int64_t kMaxGetVmLogsSize = MiB(30);

static constexpr auto state_to_signal_state =
    base::MakeFixedFlatMap<vm_tools::VmInstallState_State,
                           vm_tools::concierge::VmInstallStateSignal_State>(
        {{VmInstallState_State_IN_PROGRESS,
          vm_tools::concierge::VmInstallStateSignal_State::
              VmInstallStateSignal_State_IN_PROGRESS},
         {VmInstallState_State_FAILED,
          vm_tools::concierge::VmInstallStateSignal_State::
              VmInstallStateSignal_State_FAILED},
         {VmInstallState_State_SUCCEEDED,
          vm_tools::concierge::VmInstallStateSignal_State::
              VmInstallStateSignal_State_SUCCEEDED},
         {VmInstallState_State_UNKNOWN,
          vm_tools::concierge::VmInstallStateSignal_State::
              VmInstallStateSignal_State_UNKNOWN}});

static constexpr auto state_to_signal_step =
    base::MakeFixedFlatMap<vm_tools::VmInstallState_Step,
                           vm_tools::concierge::VmInstallStateSignal_Step>(
        {{VmInstallState_Step_launcher_start,
          vm_tools::concierge::VmInstallStateSignal_Step::
              VmInstallStateSignal_Step_launcher_start},
         {VmInstallState_Step_core_start,
          vm_tools::concierge::VmInstallStateSignal_Step::
              VmInstallStateSignal_Step_core_start},
         {VmInstallState_Step_install_fetch_image,
          vm_tools::concierge::VmInstallStateSignal_Step::
              VmInstallStateSignal_Step_install_fetch_image},
         {VmInstallState_Step_install_configure,
          vm_tools::concierge::VmInstallStateSignal_Step::
              VmInstallStateSignal_Step_install_configure},
         {VmInstallState_Step_install_done,
          vm_tools::concierge::VmInstallStateSignal_Step::
              VmInstallStateSignal_Step_install_done},
         {VmInstallState_Step_install_success,
          vm_tools::concierge::VmInstallStateSignal_Step::
              VmInstallStateSignal_Step_install_success},
         {VmInstallState_Step_install_failure,
          vm_tools::concierge::VmInstallStateSignal_Step::
              VmInstallStateSignal_Step_install_failure},
         {VmInstallState_Step_unknown,
          vm_tools::concierge::VmInstallStateSignal_Step::
              VmInstallStateSignal_Step_unknown}});

std::string ConvertToFdBasedPaths(brillo::SafeFD& root_fd,
                                  bool is_rootfs_writable,
                                  VMImageSpec& image_spec,
                                  std::vector<brillo::SafeFD>& owned_fds) {
  std::string failure_reason;
  if (image_spec.kernel.empty() && image_spec.bios.empty()) {
    LOG(ERROR) << "neither a kernel nor a BIOS were provided";
    failure_reason = "neither a kernel nor a BIOS were provided";
    return failure_reason;
  }

  if (!image_spec.kernel.empty()) {
    failure_reason =
        ConvertToFdBasedPath(root_fd, &image_spec.kernel, O_RDONLY, owned_fds);

    if (!failure_reason.empty()) {
      LOG(ERROR) << "Missing VM kernel path: " << image_spec.kernel.value();
      failure_reason = "Kernel path does not exist";
      return failure_reason;
    }
  }

  if (!image_spec.bios.empty()) {
    failure_reason =
        ConvertToFdBasedPath(root_fd, &image_spec.bios, O_RDONLY, owned_fds);

    if (!failure_reason.empty()) {
      LOG(ERROR) << "Missing VM BIOS path: " << image_spec.bios.value();
      failure_reason = "BIOS path does not exist";
      return failure_reason;
    }
  }

  if (!image_spec.pflash.empty()) {
    failure_reason =
        ConvertToFdBasedPath(root_fd, &image_spec.pflash, O_RDONLY, owned_fds);

    if (!failure_reason.empty()) {
      LOG(ERROR) << "Missing VM pflash path: " << image_spec.pflash.value();
      failure_reason = "pflash path does not exist";
      return failure_reason;
    }
  }

  if (!image_spec.initrd.empty()) {
    failure_reason =
        ConvertToFdBasedPath(root_fd, &image_spec.initrd, O_RDONLY, owned_fds);

    if (!failure_reason.empty()) {
      LOG(ERROR) << "Missing VM initrd path: " << image_spec.initrd.value();
      failure_reason = "Initrd path does not exist";
      return failure_reason;
    }
  }

  if (!image_spec.rootfs.empty()) {
    failure_reason =
        ConvertToFdBasedPath(root_fd, &image_spec.rootfs,
                             is_rootfs_writable ? O_RDWR : O_RDONLY, owned_fds);

    if (!failure_reason.empty()) {
      LOG(ERROR) << "Missing VM rootfs path: " << image_spec.rootfs.value();
      failure_reason = "Rootfs path does not exist";
      return failure_reason;
    }
  }

  return failure_reason;
}

// Posted to a grpc thread to startup a listener service. Puts a copy of
// the pointer to the grpc server in |server_copy| and then signals |event|.
// It will listen on the address specified in |listener_address|.
void RunListenerService(grpc::Service* listener,
                        const std::string& listener_address,
                        base::WaitableEvent* event,
                        std::shared_ptr<grpc::Server>* server_copy) {
  // Build the grpc server.
  grpc::ServerBuilder builder;
  builder.AddListeningPort(listener_address, grpc::InsecureServerCredentials());
  builder.RegisterService(listener);

  std::shared_ptr<grpc::Server> server(builder.BuildAndStart().release());

  *server_copy = server;
  event->Signal();

  if (server) {
    server->Wait();
  }
}

// Sets up a gRPC listener service by starting the |grpc_thread| and posting
// the main task to run for the thread. |listener_address| should be the
// address the gRPC server is listening on. A copy of the pointer to the
// server is put in |server_copy|. Returns true if setup & started
// successfully, false otherwise.
bool SetupListenerService(grpc::Service* listener_impl,
                          const std::string& listener_address,
                          std::shared_ptr<grpc::Server>* server_copy) {
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool ret = base::ThreadPool::PostTask(
      FROM_HERE, base::BindOnce(&RunListenerService, listener_impl,
                                listener_address, &event, server_copy));
  if (!ret) {
    LOG(ERROR) << "Failed to post server startup task to grpc thread";
    return false;
  }

  // Wait for the VM grpc server to start.
  event.Wait();

  if (!server_copy) {
    LOG(ERROR) << "grpc server failed to start";
    return false;
  }

  return true;
}

// Gets the path to a VM disk given the name, user id, and location.
bool GetDiskPathFromName(
    const VmId& vm_id,
    StorageLocation storage_location,
    base::FilePath* path_out,
    enum DiskImageType preferred_image_type = DiskImageType::DISK_IMAGE_AUTO) {
  switch (storage_location) {
    case STORAGE_CRYPTOHOME_ROOT: {
      const auto qcow2_path =
          GetFilePathFromName(vm_id, storage_location, kQcowImageExtension);
      if (!qcow2_path) {
        return false;
      }
      const auto raw_path =
          GetFilePathFromName(vm_id, storage_location, kRawImageExtension);
      if (!raw_path) {
        return false;
      }

      const bool qcow2_exists = base::PathExists(*qcow2_path);
      const bool raw_exists = base::PathExists(*raw_path);

      // This scenario (both <name>.img and <name>.qcow2 exist) should never
      // happen. It is prevented by the later checks in this function.
      // However, in case it does happen somehow (e.g. user manually created
      // files in dev mode), bail out, since we can't tell which one the user
      // wants.
      if (qcow2_exists && raw_exists) {
        LOG(ERROR) << "Both qcow2 and raw variants of " << vm_id.name()
                   << " already exist.";
        return false;
      }

      // Return the path to an existing image of any type, if one exists.
      // If not, generate a path based on the preferred image type.
      if (qcow2_exists) {
        *path_out = *qcow2_path;
      } else if (raw_exists) {
        *path_out = *raw_path;
      } else if (preferred_image_type == DISK_IMAGE_QCOW2) {
        *path_out = *qcow2_path;
      } else if (preferred_image_type == DISK_IMAGE_RAW ||
                 preferred_image_type == DISK_IMAGE_AUTO) {
        *path_out = *raw_path;
      } else {
        LOG(ERROR) << "Unknown image type " << preferred_image_type;
        return false;
      }
      return true;
    }
    case STORAGE_CRYPTOHOME_PLUGINVM: {
      const auto plugin_path =
          GetFilePathFromName(vm_id, storage_location, kPluginVmImageExtension);
      if (!plugin_path) {
        return false;
      }
      *path_out = *plugin_path;
      return true;
    }
    default:
      LOG(ERROR) << "Unknown storage location type";
      return false;
  }
}

// Given a VM's stateful disk, stored at |disk_location|, returns the filesystem
// which that stateful disk is formatted with. Returns "" if:
//  - The disk hasn't been formatted (yet)
//  - Some error occurs while checking
std::string GetFilesystem(const base::FilePath& disk_location) {
  std::string output;
  blkid_cache blkid_cache;
  // No cache file is used as it should always query information from
  // the device, i.e. setting cache file to /dev/null.
  if (blkid_get_cache(&blkid_cache, "/dev/null") != 0) {
    LOG(ERROR) << "Failed to initialize blkid cache handler";
    return output;
  }
  blkid_dev dev = blkid_get_dev(blkid_cache, disk_location.value().c_str(),
                                BLKID_DEV_NORMAL);
  if (!dev) {
    LOG(ERROR) << "Failed to get device for '" << disk_location.value().c_str()
               << "'";
    blkid_put_cache(blkid_cache);
    return output;
  }

  char* filesystem_type =
      blkid_get_tag_value(blkid_cache, "TYPE", disk_location.value().c_str());
  if (filesystem_type) {
    output = filesystem_type;
  }
  blkid_put_cache(blkid_cache);
  return output;
}

bool CheckVmExists(const VmId& vm_id,
                   base::FilePath* out_path = nullptr,
                   StorageLocation* storage_location = nullptr) {
  for (int l = StorageLocation_MIN; l <= StorageLocation_MAX; l++) {
    StorageLocation location = static_cast<StorageLocation>(l);
    base::FilePath disk_path;
    if (GetDiskPathFromName(vm_id, location, &disk_path) &&
        base::PathExists(disk_path)) {
      if (out_path) {
        *out_path = disk_path;
      }
      if (storage_location) {
        *storage_location = location;
      }
      return true;
    }
  }

  return false;
}

// Returns the desired size of VM disks, which is 90% of the available space
// (excluding the space already taken up by the disk). If storage ballooning
// is being used, we instead return 95% of the total disk space.
uint64_t CalculateDesiredDiskSize(base::FilePath disk_location,
                                  uint64_t current_usage,
                                  bool storage_ballooning = false) {
  if (storage_ballooning) {
    auto total_space =
        base::SysInfo::AmountOfTotalDiskSpace(disk_location.DirName());
    return ((total_space * 95) / 100) & kDiskSizeMask;
  }
  uint64_t free_space =
      base::SysInfo::AmountOfFreeDiskSpace(disk_location.DirName());
  free_space += current_usage;
  uint64_t disk_size = ((free_space * 9) / 10) & kDiskSizeMask;

  return std::max(disk_size, kMinimumDiskSize);
}

// Returns true if a disk image has a set xattr matching the desired vm_type.
std::optional<vm_tools::apps::VmType> GetDiskImageVmType(
    const std::string& disk_path) {
  static_assert(
      vm_tools::apps::VmType_MAX < 100,
      "VmType enum has more than two digits, update xattr buffer size");
  static const int xattr_max_size = 2;
  std::string xattr_vm_type;
  xattr_vm_type.resize(xattr_max_size);

  ssize_t bytes_read = getxattr(disk_path.c_str(), kDiskImageVmTypeXattr,
                                xattr_vm_type.data(), sizeof(xattr_vm_type));
  if (bytes_read < 0) {
    PLOG(WARNING) << "Unable to obtain xattr " << kDiskImageVmTypeXattr
                  << " for file " << disk_path;
    return {};
  }
  if (bytes_read <= xattr_max_size) {
    xattr_vm_type.resize(bytes_read);
  }

  int vm_type_int;
  if (!base::StringToInt(xattr_vm_type, &vm_type_int)) {
    LOG(ERROR) << "VM type xattr of " << xattr_vm_type
               << " was not a valid int.";
    return {};
  }
  if (!vm_tools::apps::VmType_IsValid(vm_type_int)) {
    LOG(ERROR) << "VM type of " << vm_type_int << " was not valid.";
    return {};
  }
  vm_tools::apps::VmType disk_image_vm_type =
      static_cast<vm_tools::apps::VmType>(vm_type_int);

  return disk_image_vm_type;
}

bool SetDiskImageVmType(const base::ScopedFD& fd,
                        vm_tools::apps::VmType vm_type) {
  std::string vm_type_str = base::NumberToString(static_cast<int>(vm_type));
  return fsetxattr(fd.get(), kDiskImageVmTypeXattr, vm_type_str.c_str(),
                   vm_type_str.size(), 0) == 0;
}

// Returns true if the disk should not be automatically resized because it is
// not sparse and its size was specified by the user.
bool IsDiskPreallocatedWithUserChosenSize(const std::string& disk_path) {
  return getxattr(disk_path.c_str(),
                  kDiskImagePreallocatedWithUserChosenSizeXattr, nullptr,
                  0) >= 0;
}

// Mark a non-sparse disk with an xattr indicating its size has been chosen by
// the user.
bool SetPreallocatedWithUserChosenSizeAttr(const base::ScopedFD& fd) {
  // The xattr value doesn't matter, only its existence.
  // Store something human-readable for debugging.
  static constexpr char val[] = "1";
  return fsetxattr(fd.get(), kDiskImagePreallocatedWithUserChosenSizeXattr, val,
                   sizeof(val), 0) == 0;
}

void FormatDiskImageStatus(const DiskImageOperation* op,
                           DiskImageStatusResponse* status) {
  status->set_status(op->status());
  status->set_command_uuid(op->uuid());
  status->set_failure_reason(op->failure_reason());
  status->set_progress(op->GetProgress());
}

uint64_t GetFileUsage(const base::FilePath& path) {
  struct stat st;
  if (stat(path.value().c_str(), &st) == 0) {
    // Use the st_blocks value to get the space usage (as in 'du') of the
    // file. st_blocks is always in units of 512 bytes, regardless of the
    // underlying filesystem and block device block size.
    return st.st_blocks * 512;
  }
  return 0;
}

// vm_id.name should always be less than kMaxVmNameLength characters long.
base::FilePath GetVmLogPath(const VmId& vm_id, const std::string& extension) {
  std::string encoded_vm_name = GetEncodedName(vm_id.name());

  base::FilePath path = base::FilePath(kCryptohomeRoot)
                            .Append(kCrosvmDir)
                            .Append(vm_id.owner_id())
                            .Append(kCrosvmLogDir)
                            .Append(encoded_vm_name)
                            .AddExtension(extension);

  return path;
}

// Returns a hash string that is safe to use as a filename.
std::string GetMd5HashForFilename(const std::string& str) {
  std::string result;
  base::MD5Digest digest;
  base::MD5Sum(base::as_byte_span(str), &digest);
  std::string_view hash_piece(reinterpret_cast<char*>(&digest.a[0]),
                              sizeof(digest.a));
  // Note, we can not have '=' symbols in this path or it will break crosvm's
  // commandline argument parsing, so we use OMIT_PADDING.
  base::Base64UrlEncode(hash_piece, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &result);
  return result;
}

// Reclaims memory of the crosvm process with |pid| by writing "shmem" to
// /proc/<pid>/reclaim. Since this function may block 10 seconds or more, do
// not call on the main thread.
ReclaimVmMemoryResponse ReclaimVmMemoryInternal(pid_t pid, int32_t page_limit) {
  ReclaimVmMemoryResponse response;

  if (page_limit < 0) {
    LOG(ERROR) << "Invalid negative page_limit " << page_limit;
    response.set_failure_reason("Negative page_limit");
    return response;
  }

  const std::string path = base::StringPrintf("/proc/%d/reclaim", pid);
  base::ScopedFD fd(
      HANDLE_EINTR(open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW)));
  if (!fd.is_valid()) {
    LOG(ERROR) << "Failed to open " << path;
    response.set_failure_reason("Failed to open /proc filesystem");
    return response;
  }

  const std::string reclaim = "shmem";
  std::list commands = {reclaim};
  if (page_limit != 0) {
    LOG(INFO) << "per-process reclaim active: [" << page_limit << "] pages";
    commands.push_front(reclaim + " " + base::NumberToString(page_limit));
  }
  ssize_t bytes_written = 0;
  int attempts = 0;
  bool write_ok = false;
  for (const auto& v : commands) {
    ++attempts;
    // We want to open the file only once, and write two times to it,
    // different values.  WriteFile() and its variants would
    // open/close/write,  which would cause an unnecessary open/close
    // cycle, so we use write() directly.
    bytes_written = HANDLE_EINTR(write(fd.get(), v.c_str(), v.size()));
    write_ok = (bytes_written == v.size());
    if (write_ok || (errno != EINVAL)) {
      break;
    }
  }

  if (!write_ok) {
    PLOG(ERROR) << "Failed to write to " << path
                << " bytes_written: " << bytes_written
                << " attempts: " << attempts;
    response.set_failure_reason("Failed to write to /proc filesystem");
    return response;
  }

  LOG(INFO) << "Successfully reclaimed VM memory. PID=" << pid;
  response.set_success(true);
  return response;
}

vm_tools::concierge::VmInstallStateSignal StateToSignal(VmInstallState state) {
  vm_tools::concierge::VmInstallStateSignal signal;
  if (auto it = state_to_signal_state.find(state.state());
      it != state_to_signal_state.end()) {
    signal.set_state(it->second);
  } else {
    signal.set_state(vm_tools::concierge::VmInstallStateSignal_State_UNKNOWN);
  }

  if (auto it = state_to_signal_step.find(state.in_progress_step());
      it != state_to_signal_step.end()) {
    signal.set_in_progress_step(it->second);
  } else {
    signal.set_in_progress_step(
        vm_tools::concierge::VmInstallStateSignal_Step_unknown);
  }
  return signal;
}

// Scoped ZSTD DCtx pointer to ensure proper deletion.
struct ZSTD_DCtxDeleter {
  void operator()(ZSTD_DCtx* dctx) const { ZSTD_freeDCtx(dctx); }
};

typedef std::unique_ptr<ZSTD_DCtx, ZSTD_DCtxDeleter> ScopedZSTD_DCtxPtr;

}  // namespace

namespace internal {
std::optional<internal::VmStartImageFds> GetVmStartImageFds(
    const google::protobuf::RepeatedField<int>& fds,
    const std::vector<base::ScopedFD>& file_handles) {
  if (file_handles.size() != fds.size()) {
    return std::nullopt;
  }
  struct internal::VmStartImageFds result;
  size_t count = 0;
  for (const auto& fdType : fds) {
    std::optional<base::ScopedFD> fd(dup(file_handles[count++].get()));
    if (!fd) {
      LOG(ERROR) << "Failed to get VM start image file descriptor";
      return std::nullopt;
    }
    switch (fdType) {
      case StartVmRequest_FdType_KERNEL:
        result.kernel_fd = std::move(*fd);
        break;
      case StartVmRequest_FdType_ROOTFS:
        result.rootfs_fd = std::move(*fd);
        break;
      case StartVmRequest_FdType_INITRD:
        result.initrd_fd = std::move(*fd);
        break;
      case StartVmRequest_FdType_STORAGE:
        result.storage_fd = std::move(*fd);
        break;
      case StartVmRequest_FdType_BIOS:
        result.bios_fd = std::move(*fd);
        break;
      case StartVmRequest_FdType_PFLASH:
        result.pflash_fd = std::move(*fd);
        break;
      default:
        LOG(WARNING) << "received request with unknown FD type " << fdType
                     << ". Ignoring.";
    }
  }
  return result;
}
}  // namespace internal

void Service::VmInstallStateSignal(VmInstallState state) {
  if (concierge_adaptor_) {
    concierge_adaptor_->SendVmInstallStateSignalSignal(StateToSignal(state));
  }
}

base::FilePath Service::GetVmGpuCachePathInternal(const VmId& vm_id) {
  std::string vm_dir;
  // Note, we can not have '=' symbols in this path or it will break crosvm's
  // commandline argument parsing, so we use OMIT_PADDING.
  base::Base64UrlEncode(vm_id.name(), base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &vm_dir);

  std::string cache_id;
  std::string error;
  bool per_boot_cache = feature::PlatformFeatures::Get()->IsEnabledBlocking(
      kPerBootVmShaderCacheFeature);

  // if per-boot cache feature is enabled or we failed to read BUILD_ID from
  // /etc/os-release, set |cache_id| as boot-id.
  brillo::OsReleaseReader reader;
  reader.Load();
  if (per_boot_cache || !reader.GetString("BUILD_ID", &cache_id)) {
    CHECK(base::ReadFileToString(base::FilePath(kBootIdFile), &cache_id));
  }

  return base::FilePath(kCryptohomeRoot)
      .Append(kCrosvmDir)
      .Append(vm_id.owner_id())
      .Append(kCrosvmGpuCacheDir)
      .Append(GetMd5HashForFilename(cache_id))
      .Append(vm_dir);
}

std::optional<int64_t> Service::GetAvailableMemory() {
  dbus::MethodCall method_call(resource_manager::kResourceManagerInterface,
                               resource_manager::kGetAvailableMemoryKBMethod);
  auto dbus_response =
      CallDBusMethod(bus_, resource_manager_service_proxy_, &method_call,
                     dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to get available memory size from resourced";
    return std::nullopt;
  }
  dbus::MessageReader reader(dbus_response.get());
  uint64_t available_kb;
  if (!reader.PopUint64(&available_kb)) {
    LOG(ERROR)
        << "Failed to read available memory size from the D-Bus response";
    return std::nullopt;
  }
  return KiB(available_kb);
}

std::optional<int64_t> Service::GetForegroundAvailableMemory() {
  dbus::MethodCall method_call(
      resource_manager::kResourceManagerInterface,
      resource_manager::kGetForegroundAvailableMemoryKBMethod);
  auto dbus_response =
      CallDBusMethod(bus_, resource_manager_service_proxy_, &method_call,
                     dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR)
        << "Failed to get foreground available memory size from resourced";
    return std::nullopt;
  }
  dbus::MessageReader reader(dbus_response.get());
  uint64_t available_kb;
  if (!reader.PopUint64(&available_kb)) {
    LOG(ERROR) << "Failed to read foreground available memory size from the "
                  "D-Bus response";
    return std::nullopt;
  }
  return KiB(available_kb);
}

std::optional<uint64_t> Service::GetCriticalMemoryMargin() {
  dbus::MethodCall method_call(resource_manager::kResourceManagerInterface,
                               resource_manager::kGetMemoryMarginsKBMethod);
  auto dbus_response =
      CallDBusMethod(bus_, resource_manager_service_proxy_, &method_call,
                     dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to get critical margin size from resourced";
    return std::nullopt;
  }
  dbus::MessageReader reader(dbus_response.get());
  uint64_t critical_margin;
  if (!reader.PopUint64(&critical_margin)) {
    LOG(ERROR)
        << "Failed to read available critical margin from the D-Bus response";
    return std::nullopt;
  }

  critical_margin *= KiB(1);
  return critical_margin;
}

std::optional<resource_manager::GameMode> Service::GetGameMode() {
  dbus::MethodCall method_call(resource_manager::kResourceManagerInterface,
                               resource_manager::kGetGameModeMethod);
  auto dbus_response =
      CallDBusMethod(bus_, resource_manager_service_proxy_, &method_call,
                     dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to get geme mode from resourced";
    return std::nullopt;
  }
  dbus::MessageReader reader(dbus_response.get());
  uint8_t game_mode;
  if (!reader.PopByte(&game_mode)) {
    LOG(ERROR) << "Failed to read game mode from the D-Bus response";
    return std::nullopt;
  }
  return static_cast<resource_manager::GameMode>(game_mode);
}

static std::optional<std::string> GameModeToForegroundVmName(
    resource_manager::GameMode game_mode) {
  using resource_manager::GameMode;
  if (USE_BOREALIS_HOST && game_mode == GameMode::BOREALIS) {
    return "borealis";
  }
  if (game_mode == GameMode::OFF) {
    return std::nullopt;
  }
  LOG(ERROR) << "Unexpected game mode value " << static_cast<int>(game_mode);
  return std::nullopt;
}

// Runs balloon policy against each VM to balance memory.
// This will be called periodically by balloon_resizing_timer_.
void Service::RunBalloonPolicy() {
  VMT_TRACE(kCategory, "Service::RunBalloonPolicy");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(b/191946183): Design and migrate to a new D-Bus API
  // that is less chatty for implementing balloon logic.

  std::optional<uint64_t> critical_margin_opt = GetCriticalMemoryMargin();
  if (!critical_margin_opt) {
    LOG(ERROR) << "Failed to get ChromeOS memory margins";
    return;
  }
  uint64_t critical_margin = *critical_margin_opt;

  const auto available_memory = GetAvailableMemory();
  if (!available_memory.has_value()) {
    return;
  }
  const auto game_mode = GetGameMode();
  if (!game_mode.has_value()) {
    return;
  }
  std::optional<int64_t> foreground_available_memory;
  if (*game_mode != resource_manager::GameMode::OFF) {
    // foreground_available_memory is only used when the game mode is enabled.
    foreground_available_memory = GetForegroundAvailableMemory();
    if (!foreground_available_memory.has_value()) {
      return;
    }
  }

  const auto foreground_vm_name = GameModeToForegroundVmName(*game_mode);
  for (auto& vm_entry : vms_) {
    auto& vm = vm_entry.second;
    if (vm->IsSuspended()) {
      // Skip suspended VMs since there is no effect.
      continue;
    }

    const std::unique_ptr<BalloonPolicyInterface>& policy =
        vm->GetBalloonPolicy(critical_margin, vm_entry.first.name());
    if (!policy) {
      // Skip VMs that don't have a memory policy. It may just not be ready
      // yet.
      continue;
    }

    auto stats_opt = vm->GetBalloonStats(base::Milliseconds(100));
    if (!stats_opt) {
      // Stats not available. Skip running policies.
      continue;
    }
    BalloonStats stats = *stats_opt;

    // Switch available memory for this VM based on the current game mode.
    bool is_in_game_mode = foreground_vm_name.has_value() &&
                           vm_entry.first.name() == foreground_vm_name;
    const int64_t available_memory_for_vm =
        is_in_game_mode ? *foreground_available_memory : *available_memory;

    int64_t delta = policy->ComputeBalloonDelta(stats, available_memory_for_vm,
                                                vm_entry.first.name());

    uint64_t target =
        std::max(static_cast<int64_t>(0),
                 static_cast<int64_t>(stats.balloon_actual) + delta);
    if (target != stats.balloon_actual) {
      vm->SetBalloonSize(target);
    }
  }
}

std::optional<bool> Service::IsFeatureEnabled(const std::string& feature_name,
                                              std::string* error_out) {
  dbus::MethodCall method_call(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::kChromeFeaturesServiceIsFeatureEnabledMethod);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(feature_name);

  dbus::Error error;
  auto dbus_response = CallDBusMethodWithErrorResponse(
      bus_, chrome_features_service_proxy_, &method_call,
      dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, &error);
  if (error.IsValid()) {
    *error_out = error.message();
    return std::nullopt;
  }

  dbus::MessageReader reader(dbus_response.get());
  bool result;
  if (!reader.PopBool(&result)) {
    *error_out = "Failed to read bool from D-Bus response";
    return std::nullopt;
  }

  *error_out = "";
  return result;
}

bool Service::ListVmDisksInLocation(const std::string& cryptohome_id,
                                    StorageLocation location,
                                    const std::string& lookup_name,
                                    ListVmDisksResponse* response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::FilePath image_dir;
  base::FileEnumerator::FileType file_type = base::FileEnumerator::FILES;
  const char* const* allowed_ext = kDiskImageExtensions;
  switch (location) {
    case STORAGE_CRYPTOHOME_ROOT:
      image_dir = base::FilePath(kCryptohomeRoot)
                      .Append(kCrosvmDir)
                      .Append(cryptohome_id);
      break;

    case STORAGE_CRYPTOHOME_PLUGINVM:
      image_dir = base::FilePath(kCryptohomeRoot)
                      .Append(kPluginVmDir)
                      .Append(cryptohome_id);
      file_type = base::FileEnumerator::DIRECTORIES;
      allowed_ext = kPluginVmImageExtensions;
      break;

    default:
      response->set_failure_reason("Unsupported storage location for images");
      return false;
  }

  if (!base::DirectoryExists(image_dir)) {
    // No directory means no VMs, return the empty response.
    return true;
  }

  uint64_t total_size = 0;
  base::FileEnumerator dir_enum(image_dir, false, file_type);
  for (base::FilePath path = dir_enum.Next(); !path.empty();
       path = dir_enum.Next()) {
    std::string extension = path.BaseName().Extension();
    bool allowed = false;
    for (auto p = allowed_ext; *p; p++) {
      if (extension == *p) {
        allowed = true;
        break;
      }
    }
    if (!allowed) {
      continue;
    }

    base::FilePath bare_name = path.BaseName().RemoveExtension();
    if (bare_name.empty()) {
      continue;
    }
    std::string image_name = GetDecodedName(bare_name.value());
    if (image_name.empty()) {
      continue;
    }
    if (!lookup_name.empty() && lookup_name != image_name) {
      continue;
    }

    uint64_t size = dir_enum.GetInfo().IsDirectory()
                        ? ComputeDirectorySize(path)
                        : GetFileUsage(path);
    total_size += size;

    uint64_t min_size;
    uint64_t available_space;
    VmId vm_id(cryptohome_id, image_name);
    auto iter = FindVm(vm_id);
    // VM may not be running - in this case, we can't determine min_size or
    // available_space, so report 0 for unknown.
    min_size = 0;
    available_space = 0;
    if (iter != vms_.end()) {
      // GetMinDiskSize relies on btrfs specific functions.
      if (GetFilesystem(path) == "btrfs") {
        min_size = iter->second->GetMinDiskSize();
      }
      available_space = iter->second->GetAvailableDiskSpace();
    }

    enum DiskImageType image_type = DiskImageType::DISK_IMAGE_AUTO;
    if (extension == kRawImageExtension) {
      image_type = DiskImageType::DISK_IMAGE_RAW;
    } else if (extension == kQcowImageExtension) {
      image_type = DiskImageType::DISK_IMAGE_QCOW2;
    } else if (extension == kPluginVmImageExtension) {
      image_type = DiskImageType::DISK_IMAGE_PLUGINVM;
    }

    VmDiskInfo* image = response->add_images();
    image->set_name(std::move(image_name));
    image->set_storage_location(location);
    image->set_size(size);
    image->set_min_size(min_size);
    image->set_available_space(available_space);
    image->set_image_type(image_type);
    image->set_user_chosen_size(
        IsDiskPreallocatedWithUserChosenSize(path.value()));
    image->set_path(path.value());
    auto vm_type = GetDiskImageVmType(path.value());
    image->set_has_vm_type(vm_type.has_value());
    if (vm_type.has_value()) {
      image->set_vm_type(ToLegacyVmType(vm_type.value()));
    }
  }

  response->set_total_size(response->total_size() + total_size);
  return true;
}

// static
void Service::CreateAndHost(
    int signal_fd,
    base::OnceCallback<void(std::unique_ptr<Service>)> on_hosted) {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  opts.dbus_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(std::move(opts));

  dbus::Bus* bus_ptr = bus.get();
  bus->GetDBusTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&dbus::Bus::Connect, base::Unretained(bus_ptr)),
      base::BindOnce(
          [](scoped_refptr<dbus::Bus> bus, int signal_fd,
             base::OnceCallback<void(std::unique_ptr<Service>)> on_hosted,
             bool connected) {
            if (!connected) {
              LOG(ERROR) << "Failed to connect to system bus";
              std::move(on_hosted).Run(nullptr);
              return;
            }
            CreateAndHost(
                std::move(bus), signal_fd,
                base::BindOnce(
                    [](const raw_ref<MetricsLibraryInterface> metrics) {
                      return std::make_unique<mm::MmService>(metrics);
                    }),
                std::move(on_hosted));
          },
          std::move(bus), signal_fd, std::move(on_hosted)));
}

// static
void Service::CreateAndHost(
    scoped_refptr<dbus::Bus> bus,
    int signal_fd,
    MmServiceFactory mm_service_factory,
    base::OnceCallback<void(std::unique_ptr<Service>)> on_hosted) {
  // Bus should be connected when using this API.
  CHECK(bus->IsConnected());
  auto service = base::WrapUnique(new Service(signal_fd, std::move(bus)));
  if (!service->Init(std::move(mm_service_factory))) {
    std::move(on_hosted).Run(nullptr);
    return;
  }
  Service* service_ptr = service.get();
  DbusAdaptor::Create(
      service_ptr->bus_, service_ptr,
      base::BindOnce(
          [](std::unique_ptr<Service> owned_service,
             base::OnceCallback<void(std::unique_ptr<Service>)> on_hosted,
             std::unique_ptr<DbusAdaptor> adaptor) {
            if (!adaptor) {
              std::move(on_hosted).Run(nullptr);
              return;
            }
            owned_service->concierge_adaptor_ = std::move(adaptor);
            std::move(on_hosted).Run(std::move(owned_service));
          },
          std::move(service), std::move(on_hosted)));
}

Service::Service(int signal_fd, scoped_refptr<dbus::Bus> bus)
    : signal_fd_(signal_fd),
      bus_(std::move(bus)),
      next_seneschal_server_port_(kFirstSeneschalServerPort),
      weak_ptr_factory_(this) {
  // The service should run on the thread that *created* the bus, not the
  // thread that de/serializes dbus messages.
  bus_->AssertOnOriginThread();
}

Service::~Service() {
  if (grpc_server_vm_) {
    grpc_server_vm_->Shutdown();
  }
}

bool Service::Init(MmServiceFactory mm_service_factory) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  VMT_TRACE_BEGIN(kCategory, "Service::Init");

  metrics_ = std::make_unique<MetricsLibrary>(
      base::MakeRefCounted<AsynchronousMetricsWriter>(
          base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})));

  vmm_swap_tbw_policy_ = std::make_unique<VmmSwapTbwPolicy>(
      raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get()),
      base::FilePath(kVmmSwapTbwHistoryFilePath));

  dlcservice_client_ = std::make_unique<DlcHelper>(bus_);

  // Set up the D-Bus client for shill.
  shill_client_ = std::make_unique<ShillClient>(bus_);
  shill_client_->RegisterResolvConfigChangedHandler(base::BindRepeating(
      &Service::OnResolvConfigChanged, weak_ptr_factory_.GetWeakPtr()));
  shill_client_->RegisterDefaultServiceChangedHandler(
      base::BindRepeating(&Service::OnDefaultNetworkServiceChanged,
                          weak_ptr_factory_.GetWeakPtr()));

  // Set up the D-Bus client for powerd and register suspend/resume handlers.
  power_manager_client_ = std::make_unique<PowerManagerClient>(bus_);
  power_manager_client_->RegisterSuspendDelay(
      base::BindRepeating(&Service::HandleSuspendImminent,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&Service::HandleSuspendDone,
                          weak_ptr_factory_.GetWeakPtr()));

  // Set up the D-Bus client for vhost_user_starter daemon.
  vhost_user_starter_client_ = std::make_unique<VhostUserStarterClient>(bus_);

  // Setup D-Bus proxy for spaced.
  disk_usage_proxy_ = std::make_unique<spaced::DiskUsageProxy>(
      std::make_unique<org::chromium::SpacedProxy>(bus_));
  disk_usage_proxy_->AddObserver(this);
  disk_usage_proxy_->StartMonitoring();

  // Get the D-Bus proxy for communicating with cicerone.
  cicerone_service_proxy_ =
      std::make_unique<org::chromium::VmCiceroneProxy>(bus_);
  cicerone_service_proxy_->RegisterTremplinStartedSignalHandler(
      base::BindRepeating(&Service::OnTremplinStartedSignal,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&Service::OnSignalConnected,
                     weak_ptr_factory_.GetWeakPtr()));

  // Get the D-Bus proxy for communicating with seneschal.
  seneschal_service_proxy_ = bus_->GetObjectProxy(
      vm_tools::seneschal::kSeneschalServiceName,
      dbus::ObjectPath(vm_tools::seneschal::kSeneschalServicePath));

  // Get the D-Bus proxy for communicating with Plugin VM dispatcher.
  vm_permission_service_proxy_ = vm_permission::GetServiceProxy(bus_);

  // Get the D-Bus proxy for communicating with Plugin VM dispatcher.
  vmplugin_service_proxy_ = pvm::dispatcher::GetServiceProxy(bus_);
  pvm::dispatcher::RegisterVmToolsChangedCallbacks(
      vmplugin_service_proxy_,
      base::BindRepeating(&Service::OnVmToolsStateChangedSignal,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&Service::OnSignalConnected,
                     weak_ptr_factory_.GetWeakPtr()));

  // Get the D-Bus proxy for communicating with resource manager.
  resource_manager_service_proxy_ = bus_->GetObjectProxy(
      resource_manager::kResourceManagerServiceName,
      dbus::ObjectPath(resource_manager::kResourceManagerServicePath));

  // Get the D-Bus proxy for communicating with Chrome Features Service.
  chrome_features_service_proxy_ = bus_->GetObjectProxy(
      chromeos::kChromeFeaturesServiceName,
      dbus::ObjectPath(chromeos::kChromeFeaturesServicePath));

  shadercached_proxy_ = bus_->GetObjectProxy(
      shadercached::kShaderCacheServiceName,
      dbus::ObjectPath(shadercached::kShaderCacheServicePath));

  CHECK(feature::PlatformFeatures::Initialize(bus_));
  VMT_TRACE_END(kCategory);

  // Setup & start the gRPC listener services.
  startup_listener_.SetInstallStateCallback(base::BindRepeating(
      &Service::VmInstallStateSignal, weak_ptr_factory_.GetWeakPtr()));
  if (!SetupListenerService(
          &startup_listener_,
          base::StringPrintf("vsock:%u:%u", VMADDR_CID_ANY,
                             vm_tools::kDefaultStartupListenerPort),
          &grpc_server_vm_)) {
    LOG(ERROR) << "Failed to setup/startup the VM grpc server";
    return false;
  }

  if (!localtime_watcher_.Watch(
          base::FilePath(kLocaltimePath),
          base::FilePathWatcher::Type::kNonRecursive,
          base::BindRepeating(&Service::OnLocaltimeFileChanged,
                              weak_ptr_factory_.GetWeakPtr()))) {
    LOG(WARNING) << "Failed to initialize file watcher for timezone change";
  }

  int64_t root_device_size = PostTaskAndWaitForResult(
      bus_->GetDBusTaskRunner(), base::BindOnce(
                                     [](spaced::DiskUsageProxy* proxy) {
                                       return proxy->GetRootDeviceSize();
                                     },
                                     disk_usage_proxy_.get()));
  if (root_device_size < 0) {
    LOG(WARNING) << "Failed to determine disk size, defaulting to minimum 16GB";
    root_device_size = 16ull * 1000 * 1000 * 1000;
  }

  double device_size_multiplier = static_cast<double>(root_device_size) /
                                  kTbwTargetForVmmSwapReferenceDiskSize;
  int64_t tbw_target = std::min(
      static_cast<int64_t>(device_size_multiplier * kTbwTargetForVmmSwapPerDay),
      kTbwMaxForVmmSwapPerDay);

  vmm_swap_tbw_policy_->SetTargetTbwPerDay(tbw_target);
  base::FilePath tbw_history_file_path(kVmmSwapTbwHistoryFilePath);
  // VmmSwapTbwPolicy repopulate pessimistic history if it fails to init. This
  // is safe to continue using regardless of the result.
  vmm_swap_tbw_policy_->Init();

  // Initialize the VM Memory Management service which handles incoming
  // connections from VMs and resourced.
  if (!InitVmMemoryManagementService(std::move(mm_service_factory))) {
    return false;
  }

  return true;
}

bool Service::InitVmMemoryManagementService(
    MmServiceFactory mm_service_factory) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // The VM Memory Management Service has a dependency on VSOCK Loopback and
  // cannot be enabled on kernels older than 5.4
  auto kernel_version = UntrustedVMUtils::GetKernelVersion();
  if (kernel_version.first < 5 ||
      (kernel_version.first == 5 && kernel_version.second < 4)) {
    LOG(INFO) << "VmMemoryManagementService not supported by kernel";
    return false;
  }

  vm_memory_management_service_ =
      std::move(mm_service_factory)
          .Run(raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get()));

  if (!vm_memory_management_service_->Start()) {
    vm_memory_management_service_.reset();
    LOG(ERROR) << "Failed to initialize VmMemoryManagementService.";
    return false;
  }

  LOG(INFO) << "Enabling VmMemoryManagementService";
  return true;
}

void Service::ChildExited() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // We can't just rely on the information in the siginfo structure because
  // more than one child may have exited but only one SIGCHLD will be
  // generated.
  while (true) {
    int status;
    pid_t pid = waitpid(-1, &status, WNOHANG);
    if (pid <= 0) {
      if (pid == -1 && errno != ECHILD) {
        PLOG(ERROR) << "Unable to reap child processes";
      }
      break;
    }

    if (WIFEXITED(status)) {
      if (WEXITSTATUS(status) != 0) {
        LOG(INFO) << "Process " << pid << " exited with status "
                  << WEXITSTATUS(status);
      }
    } else if (WIFSIGNALED(status)) {
      LOG(INFO) << "Process " << pid << " killed by signal " << WTERMSIG(status)
                << (WCOREDUMP(status) ? " (core dumped)" : "");
    } else {
      LOG(WARNING) << "Unknown exit status " << status << " for process "
                   << pid;
    }

    // See if this is a process we launched.
    auto iter = std::find_if(vms_.begin(), vms_.end(), [=](auto& pair) {
      VmBaseImpl::Info info = pair.second->GetInfo();
      return pid == info.pid;
    });

    if (iter != vms_.end()) {
      // Notify that the VM has exited.
      NotifyVmStopped(iter->first, iter->second->GetInfo().cid, VM_EXITED);

      // Now remove it from the vm list.
      vms_.erase(iter);
    }
  }

  // By this point if a VM exited, the VM instance is guaranteed to have been
  // removed from vms_. HandleChildExit() is run regardless of the exit type
  // (graceful, crash, etc.) so this is the best place to check if the balloon
  // policy timer should be stopped.
  if (balloon_resizing_timer_.IsRunning() && !BalloonTimerShouldRun()) {
    LOG(INFO) << "Balloon timer no longer needed. Stopping the timer.";
    balloon_resizing_timer_.Stop();
  }
}

void Service::Stop(base::OnceClosure on_stopped) {
  LOG(INFO) << "Shutting down due to SIGTERM";

  StopAllVmsImpl(SERVICE_SHUTDOWN);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, std::move(on_stopped));
}

void Service::StartVm(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<StartVmResponse>>
        response_cb,
    const StartVmRequest& request,
    const std::vector<base::ScopedFD>& file_handles) {
  ASYNC_SERVICE_METHOD();

  StartVmResponse response;
  // We change to a success status later if necessary.
  response.set_status(VM_STATUS_FAILURE);

  if (!CheckStartVmPreconditions(request, &response)) {
    response_cb->Return(response);
    return;
  }

  std::optional<internal::VmStartImageFds> vm_start_image_fds =
      internal::GetVmStartImageFds(request.fds(), file_handles);
  if (!vm_start_image_fds) {
    response.set_failure_reason("failed to get a VmStartImage fd");
    response_cb->Return(response);
    return;
  }

  response = StartVmInternal(request, std::move(*vm_start_image_fds));
  response_cb->Return(response);
  return;
}

StartVmResponse Service::StartVmInternal(
    StartVmRequest request, internal::VmStartImageFds vm_start_image_fds) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StartVmResponse response;
  response.set_status(VM_STATUS_FAILURE);

  VmId vm_id(request.owner_id(), request.name());
  VmBuilder vm_builder;

  apps::VmType classification = internal::ClassifyVm(request);

  // Log how long it takes to start the VM.
  metrics::DurationRecorder duration_recorder(
      raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get()),
      classification, metrics::DurationRecorder::Event::kVmStart);

  std::string failure_reason;
  std::optional<base::FilePath> biosDlcPath, vmDlcPath, toolsDlcPath;

  if (!untrusted_vm_utils_.SafeToRunVirtualMachines(&failure_reason)) {
    LOG(ERROR) << failure_reason;
    response.set_failure_reason(failure_reason);
    return response;
  }

  if (!vm_start_image_fds.bios_fd.has_value() &&
      !request.vm().bios_dlc_id().empty() &&
      request.vm().bios_dlc_id() == kBruschettaBiosDlcId) {
    biosDlcPath = GetVmImagePath(kBruschettaBiosDlcId, failure_reason);
    if (!failure_reason.empty() || !biosDlcPath.has_value()) {
      response.set_failure_reason(failure_reason);
      return response;
    }
  }

  if (!request.vm().dlc_id().empty()) {
    vmDlcPath = GetVmImagePath(request.vm().dlc_id(), failure_reason);
    if (!failure_reason.empty() || !vmDlcPath.has_value()) {
      response.set_failure_reason(failure_reason);
      return response;
    }
  }

  if (!request.vm().tools_dlc_id().empty()) {
    toolsDlcPath = GetVmImagePath(request.vm().tools_dlc_id(), failure_reason);
    if (!failure_reason.empty() || !toolsDlcPath.has_value()) {
      response.set_failure_reason(failure_reason);
      return response;
    }
  }

  // Make sure we have our signal connected if starting a Termina VM.
  if (classification == apps::VmType::TERMINA &&
      !is_tremplin_started_signal_connected_) {
    LOG(ERROR) << "Can't start Termina VM without TremplinStartedSignal";
    response.set_failure_reason("TremplinStartedSignal not connected");
    return response;
  }

  if (request.disks_size() > kMaxExtraDisks) {
    LOG(ERROR) << "Rejecting request with " << request.disks_size()
               << " extra disks";
    response.set_failure_reason("Too many extra disks");
    return response;
  }

  // Exists just to keep FDs around for crosvm to inherit
  std::vector<brillo::SafeFD> owned_fds;
  auto root_fd_result = brillo::SafeFD::Root();

  if (brillo::SafeFD::IsError(root_fd_result.second)) {
    LOG(ERROR) << "Could not open root directory: "
               << static_cast<int>(root_fd_result.second);
    response.set_failure_reason("Could not open root directory");
    return response;
  }
  auto root_fd = std::move(root_fd_result.first);

  VMImageSpec image_spec = internal::GetImageSpec(
      vm_start_image_fds.kernel_fd, vm_start_image_fds.rootfs_fd,
      vm_start_image_fds.initrd_fd, vm_start_image_fds.bios_fd,
      vm_start_image_fds.pflash_fd, biosDlcPath, vmDlcPath, toolsDlcPath,
      failure_reason);
  if (!failure_reason.empty()) {
    LOG(ERROR) << "Failed to get image paths: " << failure_reason;
    response.set_failure_reason("Failed to get image paths: " + failure_reason);
    return response;
  }

  std::string convert_fd_based_path_result = ConvertToFdBasedPaths(
      root_fd, request.writable_rootfs(), image_spec, owned_fds);
  if (!convert_fd_based_path_result.empty()) {
    response.set_failure_reason(convert_fd_based_path_result);
    return response;
  }

  std::optional<base::FilePath> pflash_result =
      GetInstalledOrRequestPflashPath(vm_id, image_spec.pflash);
  if (!pflash_result) {
    LOG(ERROR) << "Failed to get pflash path";
    response.set_failure_reason("Failed to get pflash path");
    return response;
  }
  // The path can be empty if no pflash file is installed or nothing sent by the
  // user.
  base::FilePath pflash = pflash_result.value();

  // Track the next available virtio-blk device name.
  // Assume that the rootfs filesystem was assigned /dev/pmem0 if
  // pmem is used, /dev/vda otherwise.
  // Assume every subsequent image was assigned a letter in alphabetical order
  // starting from 'b'.
  // Borealis has some hard-coded assumptions and expects /dev/pmem0.
  // Other guest types can handle booting from virtio-blk.
  bool use_pmem = USE_BOREALIS_HOST && classification == apps::VmType::BOREALIS;
  std::string rootfs_device = use_pmem ? "/dev/pmem0" : "/dev/vda";
  unsigned char disk_letter = use_pmem ? 'a' : 'b';

  // In newer components, the /opt/google/cros-containers directory
  // is split into its own disk image(vm_tools.img).  Detect whether it exists
  // to keep compatibility with older components with only vm_rootfs.img.
  std::string tools_device;
  if (base::PathExists(image_spec.tools_disk)) {
    failure_reason = ConvertToFdBasedPath(root_fd, &image_spec.tools_disk,
                                          O_RDONLY, owned_fds);
    if (!failure_reason.empty()) {
      LOG(ERROR) << "Could not open tools_disk file";
      response.set_failure_reason(failure_reason);
      return response;
    }
    vm_builder.AppendDisk(VmBuilder::Disk{
        .path = std::move(image_spec.tools_disk), .writable = false});
    tools_device = base::StringPrintf("/dev/vd%c", disk_letter++);
  }

  if (request.disks().size() == 0) {
    LOG(ERROR) << "Missing required stateful disk";
    response.set_failure_reason("Missing required stateful disk");
    return response;
  }

  // Assume the stateful device is the first disk in the request.
  std::string stateful_device = base::StringPrintf("/dev/vd%c", disk_letter);

  auto stateful_path = base::FilePath(request.disks()[0].path());
  std::optional<int64_t> stateful_size = base::GetFileSize(stateful_path);
  if (!stateful_size.has_value()) {
    LOG(ERROR) << "Could not determine stateful disk size";
    response.set_failure_reason(
        "Internal error: unable to determine stateful disk size");
    return response;
  }

  bool storage_ballooning = false;
  // Storage ballooning enabled for Borealis (for ext4 setups in order
  // to not interfere with the storage management solutions of legacy
  // setups) and Bruschetta VMs.
  if (USE_BOREALIS_HOST && classification == apps::VmType::BOREALIS &&
      GetFilesystem(stateful_path) == "ext4") {
    storage_ballooning = request.storage_ballooning();
  } else if (classification == apps::VmType::BRUSCHETTA) {
    storage_ballooning = true;
  }

  // TODO(b/288998343): remove when bug is fixed and interrupted discards are
  // not lost.
  if (storage_ballooning) {
    TrimUserFilesystem();
  }

  for (const auto& d : request.disks()) {
    VmBuilder::Disk disk{
        .path = base::FilePath(d.path()),
        .writable = d.writable(),
        .sparse = !IsDiskPreallocatedWithUserChosenSize(d.path())};

    failure_reason = ConvertToFdBasedPath(
        root_fd, &disk.path, disk.writable ? O_RDWR : O_RDONLY, owned_fds);

    if (!failure_reason.empty()) {
      LOG(ERROR) << "Could not open disk file";
      response.set_failure_reason(failure_reason);
      return response;
    }

    vm_builder.AppendDisk(disk);
  }

  // Check if an opened storage image was passed over D-BUS.
  if (vm_start_image_fds.storage_fd.has_value()) {
    const base::ScopedFD& fd = vm_start_image_fds.storage_fd.value();
    std::string failure_reason = internal::RemoveCloseOnExec(fd);
    if (!failure_reason.empty()) {
      LOG(ERROR) << "failed to remove close-on-exec flag: " << failure_reason;
      response.set_failure_reason(
          "failed to get a path for extra storage disk: " + failure_reason);
      return response;
    }

    bool writable = false;
    int mode = fcntl(fd.get(), F_GETFL);
    if ((mode & O_ACCMODE) == O_RDWR || (mode & O_ACCMODE) == O_WRONLY) {
      writable = true;
    }

    vm_builder.AppendDisk(
        VmBuilder::Disk{.path = base::FilePath(kProcFileDescriptorsPath)
                                    .Append(base::NumberToString(fd.get())),
                        .writable = writable,
                        .block_id = "cr-extra-disk"});
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

  if (request.name().size() > kMaxVmNameLength) {
    LOG(ERROR) << "VM name is too long";

    response.set_failure_reason("VM name is too long");
    return response;
  }

  base::FilePath log_path = GetVmLogPath(vm_id, kCrosvmLogSocketExt);
  base::FilePath log_dir = log_path.DirName();
  base::File::Error dir_error;
  if (!base::CreateDirectoryAndGetError(log_dir, &dir_error)) {
    LOG(ERROR) << "Failed to create crosvm log directory " << log_dir << ": "
               << base::File::ErrorToString(dir_error);
    response.set_failure_reason("Failed to create crosvm log directory");
    return response;
  }

  if (request.enable_big_gl() && !request.enable_gpu()) {
    LOG(ERROR) << "Big GL enabled without GPU";
    response.set_failure_reason("Big GL enabled without GPU");
    return response;
  }

  if (request.enable_virtgpu_native_context() && !request.enable_gpu()) {
    LOG(ERROR) << "Virtgpu native context enabled without GPU";
    response.set_failure_reason("Virtgpu native context enabled without GPU");
    return response;
  }

  // Enable the render server for Vulkan.
  const bool enable_render_server = request.enable_gpu() && USE_CROSVM_VULKAN;
  // Enable foz db list (dynamic un/loading for RO mesa shader cache) only for
  // Borealis, for now.
  const bool enable_foz_db_list =
      USE_BOREALIS_HOST && classification == apps::VmType::BOREALIS;

  VMGpuCacheSpec gpu_cache_spec;
  if (request.enable_gpu()) {
    gpu_cache_spec =
        PrepareVmGpuCachePaths(vm_id, enable_render_server, enable_foz_db_list);
  }

  // Allocate resources for the VM.
  uint32_t vsock_cid = vsock_cid_pool_.Allocate();

  std::unique_ptr<GuestOsNetwork> network;
  if (classification == apps::BRUSCHETTA) {
    network = BruschettaNetwork::Create(bus_, vsock_cid);
  } else if (USE_BOREALIS_HOST && classification == apps::BOREALIS) {
    network = BorealisNetwork::Create(bus_, vsock_cid);
  } else if (classification == apps::BAGUETTE) {
    network = BaguetteNetwork::Create(bus_, vsock_cid);
  } else {
    network = TerminaNetwork::Create(bus_, vsock_cid);
  }
  if (!network) {
    LOG(ERROR) << "Unable to get network resources";

    response.set_failure_reason("Unable to get network resources");
    return response;
  }

  uint32_t seneschal_server_port = next_seneschal_server_port_++;
  std::unique_ptr<SeneschalServerProxy> server_proxy =
      SeneschalServerProxy::CreateVsockProxy(bus_, seneschal_service_proxy_,
                                             seneschal_server_port, vsock_cid,
                                             {}, {});
  if (!server_proxy) {
    LOG(ERROR) << "Unable to start shared directory server";

    response.set_failure_reason("Unable to start shared directory server");
    return response;
  }

  // Set up a "checker" that will wait until the VM is ready or a signal is
  // received while waiting for the VM to start or we timeout.
  std::unique_ptr<VmStartChecker> vm_start_checker =
      VmStartChecker::Create(signal_fd_);
  if (!vm_start_checker) {
    LOG(ERROR) << "Failed to create VM start checker";
    response.set_failure_reason("Failed to create VM start checker");
    return response;
  }
  // This will signal the event fd passed in when the VM is ready.
  startup_listener_.AddPendingVm(vsock_cid, vm_start_checker->GetEventFd());

  // Start the VM and build the response.
  VmFeatures features{
      .gpu = request.enable_gpu(),
      .dgpu_passthrough = request.enable_dgpu_passthrough(),
      .big_gl = request.enable_big_gl(),
      .virtgpu_native_context = request.enable_virtgpu_native_context(),
      .render_server = enable_render_server,
      .vtpm_proxy = request.vtpm_proxy(),
      .audio_capture = request.enable_audio_capture(),
  };

  std::vector<std::string> params(
      std::make_move_iterator(request.mutable_kernel_params()->begin()),
      std::make_move_iterator(request.mutable_kernel_params()->end()));
  features.kernel_params = std::move(params);

  if (classification == apps::BAGUETTE) {
    stateful_device = "/dev/vdb";
    features.kernel_params.push_back(
        "root=/dev/vdb rw net.ifnames=0 systemd.log_color=0");
  }

  std::vector<std::string> oem_strings(
      std::make_move_iterator(request.mutable_oem_strings()->begin()),
      std::make_move_iterator(request.mutable_oem_strings()->end()));
  features.oem_strings = std::move(oem_strings);

  // We use _SC_NPROCESSORS_ONLN here rather than
  // base::SysInfo::NumberOfProcessors() so that offline CPUs are not counted.
  // Also, |untrusted_vm_utils_| may disable SMT leading to cores being
  // disabled. Hence, only allocate the lower of (available cores, cpus
  // allocated by the user).
  const int32_t cpus =
      request.cpus() == 0
          ? sysconf(_SC_NPROCESSORS_ONLN)
          : std::min(static_cast<int32_t>(sysconf(_SC_NPROCESSORS_ONLN)),
                     static_cast<int32_t>(request.cpus()));

  // Notify VmLogForwarder that a vm is starting up.
  SendVmStartingUpSignal(vm_id, classification, vsock_cid);

  vm_builder.SetKernel(std::move(image_spec.kernel))
      .SetBios(std::move(image_spec.bios))
      .SetPflash(std::move(pflash))
      .SetInitrd(std::move(image_spec.initrd))
      .SetCpus(cpus)
      .AppendSharedDir(CreateFontsSharedDirParam())
      .EnableSmt(false /* enable */)
      .SetGpuCachePath(std::move(gpu_cache_spec.device))
      .AppendCustomParam("--vcpu-cgroup-path",
                         base::FilePath(kTerminaVcpuCpuCgroup).value())
      .SetRenderServerCachePath(std::move(gpu_cache_spec.render_server));
  if (enable_foz_db_list) {
    auto prepare_result = PrepareShaderCache(vm_id, bus_, shadercached_proxy_);
    if (prepare_result.has_value()) {
      auto precompiled_cache_path =
          base::FilePath(prepare_result.value().precompiled_cache_path());
      vm_builder.SetFozDbListPath(std::move(gpu_cache_spec.foz_db_list))
          .SetPrecompiledCachePath(precompiled_cache_path)
          .AppendSharedDir(CreateShaderSharedDirParam(precompiled_cache_path));
    } else {
      LOG(ERROR) << "Unable to initialize shader cache: "
                 << prepare_result.error();
    }
  }
  if (!image_spec.rootfs.empty()) {
    vm_builder.SetRootfs({.device = std::move(rootfs_device),
                          .path = std::move(image_spec.rootfs),
                          .writable = request.writable_rootfs()});
  }

  // Spoof baguette vm as termina to wayland server
  VmWlInterface::Result wl_result = VmWlInterface::CreateWaylandServer(
      bus_, vm_id,
      classification == apps::BAGUETTE ? apps::TERMINA : classification);
  if (!wl_result.has_value()) {
    response.set_failure_reason("Unable to start a wayland server: " +
                                wl_result.error());
    LOG(ERROR) << response.failure_reason();
    return response;
  }
  std::unique_ptr<ScopedWlSocket> socket = std::move(wl_result).value();
  vm_builder.SetWaylandSocket(socket->GetPath().value());

  // Group the CPUs by their physical package ID to determine CPU cluster
  // layout.
  VmBuilder::VmCpuArgs vm_cpu_args =
      internal::GetVmCpuArgs(cpus, base::FilePath(kCpuInfosPath));
  vm_builder.SetVmCpuArgs(vm_cpu_args);

  /* Enable hugepages on devices with > 7 GB memory */
  if (base::SysInfo::AmountOfPhysicalMemoryMB() >= 7 * 1024) {
    vm_builder.AppendCustomParam("--hugepages", "");
  }

  if (USE_BOREALIS_HOST && classification == apps::BOREALIS) {
    bool vcpu_tweaks = feature::PlatformFeatures::Get()->IsEnabledBlocking(
        kBorealisVcpuTweaksFeature);

    if (vcpu_tweaks) {
      // Enable the vCPU tweaks here
      vm_builder.SetCpus(GetBorealisCpuCountOverride(cpus));
    }
  }

  // TODO(b/288361720): This is temporary while we test the 'provision'
  // mount option. Once we're satisfied things are stable, we'll make this
  // the default and remove this feature check.
  if (USE_BOREALIS_HOST && classification == apps::BOREALIS) {
    std::string error;
    std::optional<bool> provision =
        IsFeatureEnabled(kBorealisProvisionFeature, &error);
    if (!provision.has_value()) {
      LOG(WARNING) << "Failed to check borealis provision feature: " << error;
    } else if (provision.value()) {
      vm_builder.AppendKernelParam("maitred.provision_stateful");
    }
  }

  auto vm = TerminaVm::Create(TerminaVm::Config{
      .vsock_cid = vsock_cid,
      .network = std::move(network),
      .seneschal_server_proxy = std::move(server_proxy),
      .runtime_dir = std::move(runtime_dir),
      .log_path = std::move(log_path),
      .stateful_device = std::move(stateful_device),
      .stateful_size = static_cast<uint64_t>(std::move(stateful_size.value())),
      .features = features,
      .vm_permission_service_proxy = vm_permission_service_proxy_,
      .bus = bus_,
      .id = vm_id,
      .classification = classification,
      .storage_ballooning = storage_ballooning,
      .vm_builder = std::move(vm_builder),
      .socket = std::move(socket)});
  if (!vm) {
    LOG(ERROR) << "Unable to start VM";

    startup_listener_.RemovePendingVm(vsock_cid);
    response.set_failure_reason("Unable to start VM");
    return response;
  }

  // Wait for the VM to finish starting up and for maitre'd to signal that it's
  // ready.
  // TODO(b/338085116) Remove Borealis special case when we fix swap creation.
  base::TimeDelta timeout = (classification == apps::VmType::BOREALIS)
                                ? kBorealisVmStartupDefaultTimeout
                                : kVmStartupDefaultTimeout;
  if (request.timeout() != 0) {
    timeout = base::Seconds(request.timeout());
  }

  VmStartChecker::Status vm_start_checker_status =
      vm_start_checker->Wait(timeout);
  if (vm_start_checker_status != VmStartChecker::Status::READY) {
    LOG(ERROR) << "Error starting VM. VmStartCheckerStatus="
               << vm_start_checker_status;
    response.set_failure_reason("Error starting VM. VmStartCheckerStatus=" +
                                std::to_string(vm_start_checker_status));
    return response;
  }

  // maitre'd is ready.  Finish setting up the VM.
  if (!vm->ConfigureNetwork(nameservers_, search_domains_)) {
    LOG(ERROR) << "Failed to configure VM network";

    response.set_failure_reason("Failed to configure VM network");
    return response;
  }

  // Attempt to set the timezone of the VM correctly. Incorrect timezone does
  // not introduce issues to turnup process. Timezone can also be set during
  // runtime upon host's update.
  std::string error;
  if (!vm->SetTimezone(GetHostTimeZone(), &error)) {
    LOG(WARNING) << "Failed to set VM timezone: " << error;
  }

  // Do all the mounts.
  for (const auto& disk : request.disks()) {
    std::string src = base::StringPrintf("/dev/vd%c", disk_letter++);

    if (!disk.do_mount()) {
      continue;
    }

    uint64_t flags = disk.flags();
    if (!disk.writable()) {
      flags |= MS_RDONLY;
    }
    if (!vm->Mount(std::move(src), disk.mount_point(), disk.fstype(), flags,
                   disk.data())) {
      LOG(ERROR) << "Failed to mount " << disk.path() << " -> "
                 << disk.mount_point();

      response.set_failure_reason("Failed to mount extra disk");
      return response;
    }
  }

  // Mount the 9p server.
  if (!vm->Mount9P(seneschal_server_port, "/mnt/shared")) {
    LOG(ERROR) << "Failed to mount shared directory";

    response.set_failure_reason("Failed to mount shared directory");
    return response;
  }

  // Determine the VM token. Termina doesnt use a VM token because it has
  // per-container tokens.
  std::string vm_token = "";
  if (!request.start_termina()) {
    vm_token = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }

  // Notify cicerone that we have started a VM.
  // We must notify cicerone now before calling StartTermina, but we will only
  // send the VmStartedSignal on success.
  NotifyCiceroneOfVmStarted(vm_id, vm->cid(), vm->pid(), vm_token,
                            classification);

  if (request.start_termina()) {
    if (classification != apps::VmType::TERMINA) {
      // Should usually never be not TERMINA, but you can craft a request from
      // vmc.
      response.set_failure_reason("start_termina set on non-TERMINA");
      return response;
    }

    if (auto result = StartTermina(*vm, request.features());
        !result.has_value()) {
      response.set_failure_reason(std::move(result.error()));
      response.set_mount_result(StartVmResponse::UNKNOWN);
      return response;
    } else {
      auto [mount_result, free_bytes] = result.value();
      response.set_mount_result(
          static_cast<StartVmResponse::MountResult>(mount_result));
      if (free_bytes) {
        response.set_free_bytes(free_bytes.value());
        response.set_free_bytes_has_value(true);
      }
    }
  }

  if (!vm_token.empty() &&
      !vm->ConfigureContainerGuest(vm_token, request.vm_username(),
                                   &failure_reason)) {
    failure_reason =
        "Failed to configure the container guest: " + failure_reason;
    // TODO(b/162562622): This request is temporarily non-fatal. Once we are
    // satisfied that the maitred changes have been completed, we will make this
    // failure fatal.
    LOG(WARNING) << failure_reason;
  }

  LOG(INFO) << "Started VM with pid " << vm->pid();

  // Mount an extra disk in the VM. We mount them after calling StartTermina
  // because /mnt/external is set up there.
  if (vm_start_image_fds.storage_fd.has_value()) {
    const std::string external_disk_path =
        base::StringPrintf("/dev/vd%c", disk_letter++);

    // To support multiple extra disks in the future easily, we use integers for
    // names of mount points. Since we support only one extra disk for now,
    // |target_dir| is always "0".
    if (!vm->MountExternalDisk(std::move(external_disk_path),
                               /* target_dir= */ "0")) {
      LOG(ERROR) << "Failed to mount " << external_disk_path;

      response.set_failure_reason("Failed to mount extra disk");
      return response;
    }
  }

  vms_[vm_id] = std::move(vm);

  // While VmStartedSignal is delayed, the return of StartVM does not wait for
  // the control socket to avoid a delay in boot time. Ref: b:316491142.
  HandleControlSocketReady(vm_id);

  response.set_success(true);
  response.set_status(request.start_termina() ? VM_STATUS_STARTING
                                              : VM_STATUS_RUNNING);
  *response.mutable_vm_info() = ToVmInfo(vms_[vm_id]->GetInfo(), true);
  return response;
}

void Service::StopVm(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                         SuccessFailureResponse>> response_cb,
                     const StopVmRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  if (!StopVmInternal(vm_id, STOP_VM_REQUESTED)) {
    LOG(ERROR) << "Unable to shut down VM";
    response.set_failure_reason("Unable to shut down VM");
  } else {
    response.set_success(true);
  }
  response_cb->Return(response);
  return;
}

void Service::StopVmWithoutOwnerId(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        SuccessFailureResponse>> response_cb,
    const StopVmRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  if (request.name().empty()) {
    response_cb->Return(response);
    return;
  }

  std::vector<VmId> vms_to_stop;
  for (const auto& [vm_id, _] : vms_) {
    if (vm_id.name() == request.name()) {
      vms_to_stop.push_back(vm_id);
    }
  }

  for (const auto& vm_to_stop : vms_to_stop) {
    if (!StopVmInternal(vm_to_stop, STOP_VM_REQUESTED)) {
      LOG(ERROR) << "Unable to shut down VM";
      response.set_failure_reason("Unable to shut down VM");
      response_cb->Return(response);
      return;
    }
  }

  response.set_success(true);
  response_cb->Return(response);
  return;
}

bool Service::StopVmInternal(const VmId& vm_id, VmStopReason reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    // This is not an error to Chrome
    return true;
  }
  std::unique_ptr<VmBaseImpl>& vm = iter->second;
  VmBaseImpl::Info info = vm->GetInfo();

  // Notify that we are about to stop a VM.
  NotifyVmStopping(vm_id, info.cid);

  {
    metrics::DurationRecorder duration_recorder(
        raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get()), info.type,
        metrics::DurationRecorder::Event::kVmStop);
    if (!vm->Shutdown()) {
      return false;
    }
  }

  // Notify that we have stopped a VM.
  NotifyVmStopped(vm_id, info.cid, reason);

  vms_.erase(iter);
  return true;
}

void Service::StopVmInternalAsTask(VmId vm_id, VmStopReason reason) {
  StopVmInternal(vm_id, reason);
}

// Wrapper to destroy VM in another thread
class VMDelegate : public base::PlatformThread::Delegate {
 public:
  explicit VMDelegate(VmBaseImpl* vm = nullptr) : vm_(vm) {}
  ~VMDelegate() override = default;

  void ThreadMain() override { vm_->Shutdown(); }

 private:
  VmBaseImpl* vm_;
};

void Service::StopAllVms(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response_cb) {
  ASYNC_SERVICE_METHOD();
  StopAllVmsImpl(STOP_ALL_VMS_REQUESTED);
  response_cb->Return();
}

void Service::StopAllVmsImpl(VmStopReason reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  is_shutting_down_ = true;

  struct ThreadContext {
    base::PlatformThreadHandle handle;
    VMDelegate delegate;
  };
  std::vector<ThreadContext> ctxs(vms_.size());

  // Spawn a thread for each VM to shut it down.
  int i = 0;
  for (auto& vm : vms_) {
    ThreadContext& ctx = ctxs[i++];

    const VmId& id = vm.first;
    VmBaseImpl* vm_base_impl = vm.second.get();
    VmBaseImpl::Info info = vm_base_impl->GetInfo();

    // Notify that we are about to stop a VM.
    NotifyVmStopping(id, info.cid);

    // The VM will be destructred in the new thread, stopping it normally (and
    // then forcibly) it if it hasn't stopped yet.
    //
    // Would you just take a lambda function? Why do we need the Delegate?...
    // It's safe to pass a pointer to |metrics_| to another thread because
    // |metrics_| uses AsynchronousMetricsWriter, which is thread-safe.
    ctx.delegate = VMDelegate(vm_base_impl);
    base::PlatformThread::Create(0, &ctx.delegate, &ctx.handle);
  }

  i = 0;
  for (auto& vm : vms_) {
    ThreadContext& ctx = ctxs[i++];
    base::PlatformThread::Join(ctx.handle);

    const VmId& id = vm.first;
    VmBaseImpl* vm_base_impl = vm.second.get();
    VmBaseImpl::Info info = vm_base_impl->GetInfo();

    // Notify that we have stopped a VM.
    NotifyVmStopped(id, info.cid, reason);
  }

  vms_.clear();

  if (!ctxs.empty()) {
    LOG(INFO) << "Stopped all Vms";
  }
}

void Service::SuspendVm(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                            SuccessFailureResponse>> response_cb,
                        const SuspendVmRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << request.name() << " does not exist";
    // This is not an error to Chrome
    response.set_success(true);
    response_cb->Return(response);
    return;
  }

  auto& vm = iter->second;
  if (!vm->UsesExternalSuspendSignals()) {
    LOG(ERROR) << "Received D-Bus suspend request for " << iter->first
               << " but it does not use external suspend signals.";

    response.set_failure_reason(
        "VM does not support external suspend signals.");
    response_cb->Return(response);
    return;
  }

  vm->Suspend();

  response.set_success(true);
  response_cb->Return(response);
  return;
}

void Service::ResumeVm(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                           SuccessFailureResponse>> response_cb,
                       const ResumeVmRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    // This is not an error to Chrome
    response.set_success(true);
    response_cb->Return(response);
    return;
  }

  auto& vm = iter->second;
  if (!vm->UsesExternalSuspendSignals()) {
    LOG(ERROR) << "Received D-Bus resume request for " << iter->first
               << " but it does not use external suspend signals.";

    response.set_failure_reason(
        "VM does not support external suspend signals.");
    response_cb->Return(response);
    return;
  }

  vm->Resume();

  std::string failure_reason;
  if (vm->SetTime(&failure_reason)) {
    LOG(INFO) << "Successfully set VM clock in " << iter->first << ".";
  } else {
    LOG(ERROR) << "Failed to set VM clock in " << iter->first << ": "
               << failure_reason;
  }

  vm->SetResolvConfig(nameservers_, search_domains_);

  response.set_success(true);
  response_cb->Return(response);
  return;
}

void Service::GetVmInfo(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<GetVmInfoResponse>>
        response_cb,
    const GetVmInfoRequest& request) {
  ASYNC_SERVICE_METHOD();

  GetVmInfoResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";

    response_cb->Return(response);
    return;
  }

  *response.mutable_vm_info() = ToVmInfo(iter->second->GetInfo(), true);
  response.set_success(true);
  response_cb->Return(response);
  return;
}

void Service::GetVmEnterpriseReportingInfo(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        GetVmEnterpriseReportingInfoResponse>> response_cb,
    const GetVmEnterpriseReportingInfoRequest& request) {
  ASYNC_SERVICE_METHOD();

  GetVmEnterpriseReportingInfoResponse response;

  VmId vm_id(request.owner_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    response.set_failure_reason("Requested VM does not exist");
    response_cb->Return(response);
    return;
  }

  // failure_reason and success will be set by GetVmEnterpriseReportingInfo.
  if (!iter->second->GetVmEnterpriseReportingInfo(&response)) {
    LOG(ERROR) << "Failed to get VM enterprise reporting info";
  }
  response_cb->Return(response);
  return;
}

void Service::SetBalloonTimer(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        SuccessFailureResponse>> response_cb,
    const SetBalloonTimerRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  if (request.timer_interval_millis() == 0) {
    LOG(INFO) << "timer_interval_millis is 0. Stop the timer.";
    balloon_resizing_timer_.Stop();
  } else if (BalloonTimerShouldRun()) {
    LOG(INFO) << "Update balloon timer interval as "
              << request.timer_interval_millis() << "ms.";
    balloon_resizing_timer_.Start(
        FROM_HERE, base::Milliseconds(request.timer_interval_millis()), this,
        &Service::RunBalloonPolicy);
  } else {
    LOG(WARNING) << "SetBalloonTimer request received but the balloon timer "
                    "should not be "
                    "running. Defaulting to a disabled balloon timer.";
  }

  response.set_success(true);
  response_cb->Return(response);
  return;
}

void Service::AdjustVm(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                           SuccessFailureResponse>> response_cb,
                       const AdjustVmRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  StorageLocation location;
  if (!CheckVmExists(vm_id, nullptr, &location)) {
    response.set_failure_reason("Requested VM does not exist");
    response_cb->Return(response);
    return;
  }

  std::vector<std::string> params(request.params().begin(),
                                  request.params().end());

  std::string failure_reason;
  bool success = false;
  if (request.operation() == "pvm.shared-profile") {
    if (location != STORAGE_CRYPTOHOME_PLUGINVM) {
      failure_reason = "Operation is not supported for the VM";
    } else {
      success = pvm::helper::ToggleSharedProfile(
          bus_, vmplugin_service_proxy_,
          VmId(request.owner_id(), request.name()), std::move(params),
          &failure_reason);
    }
  } else if (request.operation() == "memsize") {
    if (params.size() != 1) {
      failure_reason = "Incorrect number of arguments for 'memsize' operation";
    } else if (location != STORAGE_CRYPTOHOME_PLUGINVM) {
      failure_reason = "Operation is not supported for the VM";
    } else {
      success =
          pvm::helper::SetMemorySize(bus_, vmplugin_service_proxy_,
                                     VmId(request.owner_id(), request.name()),
                                     std::move(params), &failure_reason);
    }
  } else if (request.operation() == "rename") {
    if (params.size() != 1) {
      failure_reason = "Incorrect number of arguments for 'rename' operation";
    } else if (params[0].empty()) {
      failure_reason = "New name can not be empty";
    } else {
      VmId new_id(request.owner_id(), params[0]);
      if (CheckVmExists(new_id)) {
        failure_reason = "VM with such name already exists";
      } else if (location != STORAGE_CRYPTOHOME_PLUGINVM) {
        failure_reason = "Operation is not supported for the VM";
      } else {
        success = RenamePluginVm(vm_id, new_id, &failure_reason);
      }
    }
  } else {
    failure_reason = "Unrecognized operation";
  }

  response.set_success(success);
  response.set_failure_reason(failure_reason);
  response_cb->Return(response);
  return;
}

void Service::SyncVmTimes(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        vm_tools::concierge::SyncVmTimesResponse>> response_cb) {
  ASYNC_SERVICE_METHOD();

  SyncVmTimesResponse response;
  int failures = 0;
  int requests = 0;
  for (auto& vm_entry : vms_) {
    requests++;
    std::string failure_reason;
    if (!vm_entry.second->SetTime(&failure_reason)) {
      failures++;
      response.add_failure_reason(std::move(failure_reason));
    }
  }
  response.set_requests(requests);
  response.set_failures(failures);

  response_cb->Return(response);
  return;
}

base::expected<std::pair<vm_tools::StartTerminaResponse::MountResult /*result*/,
                         std::optional<int64_t> /*free_bytes*/>,
               std::string>
Service::StartTermina(TerminaVm& vm,
                      const google::protobuf::RepeatedField<int>& features) {
  LOG(INFO) << "Starting Termina-specific services";
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  vm_tools::StartTerminaResponse response;
  std::optional<int64_t> free_bytes;
  if (std::string error; !vm.StartTermina(vm.ContainerCIDRAddress().ToString(),
                                          features, &error, &response)) {
    return base::unexpected(error);
  }

  if (response.mount_result() ==
      vm_tools::StartTerminaResponse::PARTIAL_DATA_LOSS) {
    LOG(ERROR) << "Possible data loss from filesystem corruption detected";
  }

  if (response.free_bytes_has_value()) {
    free_bytes = response.free_bytes();
  }

  return std::make_pair(response.mount_result(), free_bytes);
}

// Executes a command on the specified disk path. Returns false when
// `GetAppOutputWithExitCode()` fails (i.e., the command could not be launched
// or does not exit cleanly). Otherwise returns true and sets |exit_code|.
bool ExecuteCommandOnDisk(const base::FilePath& disk_path,
                          const std::string& executable_path,
                          const std::vector<std::string>& opts,
                          int* exit_code) {
  std::vector<std::string> args = {executable_path, disk_path.value()};
  args.insert(args.end(), opts.begin(), opts.end());
  std::string output;
  return base::GetAppOutputWithExitCode(base::CommandLine(args), &output,
                                        exit_code);
}

// Generates a file path that is a distinct sibling of the specified path and
// does not contain the equal sign '='.
base::FilePath GenerateTempFilePathWithNoEqualSign(const base::FilePath& path) {
  std::string temp_name;
  base::RemoveChars(path.BaseName().value(), "=", &temp_name);
  return path.DirName().Append(temp_name + ".tmp");
}

bool WriteSourceImageToDisk(const base::ScopedFD& source_fd,
                            const base::ScopedFD& disk_fd) {
  size_t in_size = ZSTD_DStreamInSize();
  size_t out_size = ZSTD_DStreamOutSize();
  std::vector<char> in_buffer(in_size);
  std::vector<char> out_buffer(out_size);

  ScopedZSTD_DCtxPtr dctx(ZSTD_createDCtx());
  CHECK(dctx != nullptr);

  ssize_t bytes_read;
  size_t bytes_written = 0;

  while ((bytes_read =
              HANDLE_EINTR(read(source_fd.get(), in_buffer.data(), in_size)))) {
    if (bytes_read < 0) {
      LOG(ERROR) << "Error reading from source image: " << bytes_read;
      return false;
    }

    ZSTD_inBuffer input = {in_buffer.data(), static_cast<size_t>(bytes_read),
                           0};
    while (input.pos < input.size) {
      ZSTD_outBuffer output = {out_buffer.data(), out_size, 0};
      size_t const ret = ZSTD_decompressStream(dctx.get(), &output, &input);

      if (ZSTD_isError(ret)) {
        LOG(ERROR) << "Unable to decompress: " << ZSTD_getErrorName(ret);
        return false;
      }

      ssize_t written =
          HANDLE_EINTR(write(disk_fd.get(), out_buffer.data(), output.pos));
      if (written < 0) {
        LOG(ERROR) << "Error writing to output file: " << written;
        return false;
      }
      bytes_written += written;
    }
  }

  if (bytes_written == 0) {
    LOG(ERROR) << "Provided source file was empty";
    return false;
  }

  return true;
}

// Creates a filesystem at the specified file/path.
bool CreateFilesystem(const base::FilePath& disk_location,
                      enum FilesystemType filesystem_type,
                      const std::vector<std::string>& mkfs_opts,
                      const std::vector<std::string>& tune2fs_opts) {
  std::string filesystem_string;
  switch (filesystem_type) {
    case FilesystemType::EXT4:
      filesystem_string = "ext4";
      break;
    case FilesystemType::UNSPECIFIED:
    default:
      LOG(ERROR) << "Filesystem was not specified";
      return false;
  }

  std::string existing_filesystem = GetFilesystem(disk_location);
  if (!existing_filesystem.empty() &&
      existing_filesystem != filesystem_string) {
    LOG(ERROR) << "Filesystem already exists but is the wrong type, expected:"
               << filesystem_string << ", got:" << existing_filesystem;
    return false;
  }

  if (existing_filesystem == filesystem_string) {
    return true;
  }

  LOG(INFO) << "Creating " << filesystem_string << " filesystem at "
            << disk_location;
  int exit_code = -1;
  ExecuteCommandOnDisk(disk_location, "/sbin/mkfs." + filesystem_string,
                       mkfs_opts, &exit_code);
  if (exit_code != 0) {
    LOG(ERROR) << "Can't format '" << disk_location << "' as "
               << filesystem_string << ", exit status: " << exit_code;
    return false;
  }

  if (tune2fs_opts.empty()) {
    return true;
  }

  LOG(INFO) << "Adjusting ext4 filesystem at " << disk_location
            << " with tune2fs";
  // Currently, tune2fs cannot handle paths containing '=' (b/267134417).
  // To avoid the issue, below we temporarily rename the disk image so that it
  // does not contain '=', apply tune2fs to the renamed path, and then rename
  // the disk image back to its original name.
  // TODO(b/267134417): Remove this workaround once tune2fs is fixed.
  const base::FilePath temp_disk_location =
      GenerateTempFilePathWithNoEqualSign(disk_location);

  if (!base::Move(disk_location, temp_disk_location)) {
    LOG(ERROR) << "Failed to move " << disk_location << " to "
               << temp_disk_location;
    unlink(temp_disk_location.value().c_str());
    return false;
  }

  exit_code = -1;
  ExecuteCommandOnDisk(temp_disk_location, "/sbin/tune2fs", tune2fs_opts,
                       &exit_code);

  // Move the disk image back to the original location before checking the exit
  // code. This is to make the behavior on tune2fs failures aligh with that on
  // mkfs failures (the disk image exists in the original location).
  // Note that the disk image is removed if the move (rename) operation fails,
  // but it should be much rarer than mkfs/tune2fs failures.
  if (!base::Move(temp_disk_location, disk_location)) {
    LOG(ERROR) << "Failed to move " << temp_disk_location << " back to "
               << disk_location;
    unlink(temp_disk_location.value().c_str());
    return false;
  }

  if (exit_code != 0) {
    LOG(ERROR) << "Can't adjust '" << disk_location
               << "' with tune2fs, exit status: " << exit_code;
    return false;
  }

  return true;
}

void Service::CreateDiskImage(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        CreateDiskImageResponse>> response_cb,
    const CreateDiskImageRequest& request,
    const std::vector<base::ScopedFD>& file_handles) {
  ASYNC_SERVICE_METHOD();

  CreateDiskImageResponse response;

  base::ScopedFD in_fd;
  if (request.storage_location() == STORAGE_CRYPTOHOME_PLUGINVM) {
    if (file_handles.size() == 0) {
      LOG(ERROR) << "CreateDiskImage: no fd found";
      response.set_failure_reason("no source fd found");

      response_cb->Return(response);
      return;
    }
    in_fd.reset(dup(file_handles[0].get()));
  }

  if (request.copy_baguette_image()) {
    if (file_handles.size() == 0) {
      LOG(ERROR) << "CreateDiskImage: no baguette source fd found";
      response.set_failure_reason("no baguette source fd found");

      response_cb->Return(response);
      return;
    }
    in_fd.reset(dup(file_handles[0].get()));
  }

  response_cb->Return(CreateDiskImageInternal(request, std::move(in_fd)));
  return;
}

CreateDiskImageResponse Service::CreateDiskImageInternal(
    CreateDiskImageRequest request, base::ScopedFD in_fd) {
  CreateDiskImageResponse response;

  VmId vm_id(request.cryptohome_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response.set_status(DISK_STATUS_FAILED);
    return response;
  }

  // Set up the disk image as a sparse file when
  //   1) |allocation_type| is DISK_ALLOCATION_TYPE_SPARSE, or
  //   2) |allocation_type| is DISK_ALLOCATION_TYPE_AUTO (the default value) and
  //      |disk_size| is 0.
  // The latter case exists to preserve the old behaviors for existing callers.
  if (request.allocation_type() ==
      DiskImageAllocationType::DISK_ALLOCATION_TYPE_AUTO) {
    LOG(WARNING) << "Disk allocation type is unspecified (or specified as "
                    "auto). Whether to create a sparse disk image will be "
                    "automatically determined using the requested disk size.";
  }
  bool is_sparse = request.allocation_type() ==
                       DiskImageAllocationType::DISK_ALLOCATION_TYPE_SPARSE ||
                   (request.allocation_type() ==
                        DiskImageAllocationType::DISK_ALLOCATION_TYPE_AUTO &&
                    request.disk_size() == 0);
  if (!is_sparse && request.disk_size() == 0) {
    response.set_failure_reason(
        "Request is invalid, disk size must be non-zero for non-sparse disks");
    return response;
  }
  if (!is_sparse && request.storage_ballooning()) {
    response.set_failure_reason(
        "Request is invalid, storage ballooning is only available for sparse "
        "disks");
    return response;
  }

  base::FilePath disk_path;
  StorageLocation disk_location;
  if (CheckVmExists(vm_id, &disk_path, &disk_location)) {
    if (disk_location != request.storage_location()) {
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason(
          "VM/disk with same name already exists in another storage location");
      return response;
    }

    if (disk_location == STORAGE_CRYPTOHOME_PLUGINVM) {
      // We do not support extending Plugin VM images.
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Plugin VM with such name already exists");
      return response;
    }

    LOG(INFO) << "Found existing disk at " << disk_path.value();

    response.set_status(DISK_STATUS_EXISTS);
    response.set_disk_path(disk_path.value());
    return response;
  }

  if (!GetDiskPathFromName(vm_id, request.storage_location(), &disk_path,
                           request.image_type())) {
    response.set_status(DISK_STATUS_FAILED);
    response.set_failure_reason("Failed to create vm image");

    return response;
  }

  if (request.storage_location() == STORAGE_CRYPTOHOME_PLUGINVM) {
    // Make sure we have the FD to fill with disk image data.
    if (!in_fd.is_valid()) {
      LOG(ERROR) << "CreateDiskImage: fd is not valid";
      response.set_failure_reason("fd is not valid");
    }

    // Get the name of directory for ISO images. Do not create it - it will be
    // created by the PluginVmCreateOperation code.
    base::FilePath iso_dir;
    if (!GetPluginIsoDirectory(vm_id, false /* create */, &iso_dir)) {
      LOG(ERROR) << "Unable to determine directory for ISOs";

      response.set_failure_reason("Unable to determine ISO directory");
      return response;
    }

    std::vector<std::string> params(
        std::make_move_iterator(request.mutable_params()->begin()),
        std::make_move_iterator(request.mutable_params()->end()));

    std::unique_ptr<PluginVmCreateOperation> op =
        PluginVmCreateOperation::Create(
            std::move(in_fd), iso_dir, request.source_size(),
            VmId(request.cryptohome_id(), request.vm_name()),
            std::move(params));

    response.set_disk_path(disk_path.value());
    response.set_status(op->status());
    response.set_command_uuid(op->uuid());
    response.set_failure_reason(op->failure_reason());

    if (op->status() == DISK_STATUS_IN_PROGRESS) {
      std::string uuid = op->uuid();
      disk_image_ops_.emplace_back(std::move(op));
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&Service::RunDiskImageOperation,
                         weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
    }

    return response;
  }

  uint64_t disk_size = request.disk_size()
                           ? request.disk_size()
                           : CalculateDesiredDiskSize(
                                 disk_path, 0, request.storage_ballooning());

  if (request.image_type() == DISK_IMAGE_QCOW2) {
    LOG(ERROR) << "Creating qcow2 disk images is unsupported";
    response.set_status(DISK_STATUS_FAILED);
    response.set_failure_reason("Creating qcow2 disk images is unsupported");

    return response;
  }

  if (request.image_type() == DISK_IMAGE_RAW ||
      request.image_type() == DISK_IMAGE_AUTO) {
    LOG(INFO) << "Creating raw disk at: " << disk_path.value() << " size "
              << disk_size;
    base::ScopedFD fd(
        open(disk_path.value().c_str(), O_CREAT | O_NONBLOCK | O_WRONLY, 0600));
    if (!fd.is_valid()) {
      PLOG(ERROR) << "Failed to create raw disk";
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Failed to create raw disk file");

      return response;
    }

    if (request.copy_baguette_image()) {
      if (!in_fd.is_valid()) {
        PLOG(ERROR) << "CreateDiskImage: fd is not valid";
        unlink(disk_path.value().c_str());
        response.set_status(DISK_STATUS_FAILED);
        response.set_failure_reason("fd is not valid");
        return response;
      }

      if (!WriteSourceImageToDisk(in_fd, fd)) {
        PLOG(ERROR) << "Failed to create disk from provided disk image";
        unlink(disk_path.value().c_str());
        response.set_status(DISK_STATUS_FAILED);
        response.set_failure_reason("unable to write source image to disk");
        return response;
      }
      LOG(INFO) << "Disk image created from compressed image";
      response.set_status(DISK_STATUS_CREATED);
      response.set_disk_path(disk_path.value());

      if (!SetDiskImageVmType(fd, vm_tools::apps::VmType::BAGUETTE)) {
        LOG(WARNING) << "Unable to set xattr for disk image's VmType";
      } else {
        LOG(INFO) << "Set xattr for disk image.";
      }
    }

    if (!is_sparse) {
      LOG(INFO) << "Creating user-chosen-size raw disk image";
      if (!SetPreallocatedWithUserChosenSizeAttr(fd)) {
        PLOG(ERROR) << "Failed to set user_chosen_size xattr";
        unlink(disk_path.value().c_str());
        response.set_status(DISK_STATUS_FAILED);
        response.set_failure_reason("Failed to set user_chosen_size xattr");

        return response;
      }

      LOG(INFO) << "Preallocating user-chosen-size raw disk image";
      if (fallocate(fd.get(), 0, 0, disk_size) != 0) {
        PLOG(ERROR) << "Failed to allocate raw disk";
        unlink(disk_path.value().c_str());
        response.set_status(DISK_STATUS_FAILED);
        response.set_failure_reason("Failed to allocate raw disk file");

        return response;
      }

      LOG(INFO) << "Disk image preallocated";
      response.set_status(DISK_STATUS_CREATED);
      response.set_disk_path(disk_path.value());

    } else {
      LOG(INFO) << "Creating sparse raw disk image";
      int ret = ftruncate(fd.get(), disk_size);
      if (ret != 0) {
        PLOG(ERROR) << "Failed to truncate raw disk";
        unlink(disk_path.value().c_str());
        response.set_status(DISK_STATUS_FAILED);
        response.set_failure_reason("Failed to truncate raw disk file");

        return response;
      }

      LOG(INFO) << "Sparse raw disk image created";
      response.set_status(DISK_STATUS_CREATED);
      response.set_disk_path(disk_path.value());
    }

    if (request.filesystem_type() == FilesystemType::UNSPECIFIED) {
      // Skip creating a filesystem when no filesystem type is specified.
      return response;
    }

    // Create a filesystem on the disk to make it usable for the VM.
    std::vector<std::string> mkfs_opts(
        std::make_move_iterator(request.mutable_mkfs_opts()->begin()),
        std::make_move_iterator(request.mutable_mkfs_opts()->end()));
    if (mkfs_opts.empty()) {
      // Set the default options.
      mkfs_opts = kExtMkfsOpts;
    }
    // -q is added to silence the output.
    mkfs_opts.push_back("-q");

    const std::vector<std::string> tune2fs_opts(
        std::make_move_iterator(request.mutable_tune2fs_opts()->begin()),
        std::make_move_iterator(request.mutable_tune2fs_opts()->end()));

    if (!CreateFilesystem(disk_path, request.filesystem_type(), mkfs_opts,
                          tune2fs_opts)) {
      PLOG(ERROR) << "Failed to create filesystem";
      unlink(disk_path.value().c_str());
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Failed to create filesystem");
    }

    return response;
  }

  LOG(ERROR) << "Unknown image_type in CreateDiskImage: "
             << request.image_type();
  response.set_status(DISK_STATUS_FAILED);
  response.set_failure_reason("Unknown image_type");
  return response;
}

void Service::DestroyDiskImage(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        DestroyDiskImageResponse>> response_cb,
    const DestroyDiskImageRequest& request) {
  ASYNC_SERVICE_METHOD();

  DestroyDiskImageResponse response;

  VmId vm_id(request.cryptohome_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response.set_status(DISK_STATUS_FAILED);
    response_cb->Return(response);
    return;
  }

  // Stop the associated VM if it is still running.
  auto iter = FindVm(vm_id);
  if (iter != vms_.end()) {
    LOG(INFO) << "Shutting down VM " << request.vm_name();

    if (!StopVmInternal(vm_id, DESTROY_DISK_IMAGE_REQUESTED)) {
      LOG(ERROR) << "Unable to shut down VM";

      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Unable to shut down VM");
      response_cb->Return(response);
      return;
    }
  }

  // Delete shader cache best-effort. Shadercached is only distributed to boards
  // if borealis enabled. There is no way to check VM type easily unless we turn
  // it up.
  // TODO(endlesspring): Deal with errors once we distribute to all boards.
  auto _ = PurgeShaderCache(vm_id, bus_, shadercached_proxy_);

  base::FilePath disk_path;
  StorageLocation location;
  if (!CheckVmExists(vm_id, &disk_path, &location)) {
    response.set_status(DISK_STATUS_DOES_NOT_EXIST);
    response.set_failure_reason("No such image");
    response_cb->Return(response);
    return;
  }

  if (!EraseGuestSshKeys(vm_id)) {
    // Don't return a failure here, just log an error because this is only a
    // side effect and not what the real request is about.
    LOG(ERROR) << "Failed removing guest SSH keys for VM " << request.vm_name();
  }

  if (location == STORAGE_CRYPTOHOME_PLUGINVM) {
    base::FilePath iso_dir;
    if (GetPluginIsoDirectory(vm_id, false /* create */, &iso_dir) &&
        base::PathExists(iso_dir) && !base::DeletePathRecursively(iso_dir)) {
      LOG(ERROR) << "Unable to remove ISO directory for " << vm_id.name();

      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Unable to remove ISO directory");

      response_cb->Return(response);
      return;
    }

    // Delete GPU shader disk cache.
    base::FilePath gpu_cache_path = GetVmGpuCachePathInternal(vm_id);
    if (!base::DeletePathRecursively(gpu_cache_path)) {
      LOG(ERROR) << "Failed to remove GPU cache for VM: " << gpu_cache_path;
    }
  }

  bool delete_result = (location == STORAGE_CRYPTOHOME_PLUGINVM)
                           ? base::DeletePathRecursively(disk_path)
                           : base::DeleteFile(disk_path);
  if (!delete_result) {
    response.set_status(DISK_STATUS_FAILED);
    response.set_failure_reason("Disk removal failed");

    response_cb->Return(response);
    return;
  }

  // Pflash may not be present for all VMs. We should only report error if it
  // exists and we failed to delete it. The |DeleteFile| API handles the
  // non-existing case as a success.
  std::optional<PflashMetadata> pflash_metadata = GetPflashMetadata(vm_id);
  if (pflash_metadata && pflash_metadata->is_installed) {
    if (!base::DeleteFile(pflash_metadata->path)) {
      response.set_status(DISK_STATUS_FAILED);
      response.set_failure_reason("Pflash removal failed");
      response_cb->Return(response);
      return;
    }
  }

  response.set_status(DISK_STATUS_DESTROYED);
  response_cb->Return(response);
  return;
}

void Service::ResizeDiskImage(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        ResizeDiskImageResponse>> response_cb,
    const ResizeDiskImageRequest& request) {
  ASYNC_SERVICE_METHOD();

  ResizeDiskImageResponse response;

  VmId vm_id(request.cryptohome_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response.set_status(DISK_STATUS_FAILED);
    response_cb->Return(response);
    return;
  }

  base::FilePath disk_path;
  StorageLocation location;
  if (!CheckVmExists(vm_id, &disk_path, &location)) {
    response.set_status(DISK_STATUS_DOES_NOT_EXIST);
    response.set_failure_reason("Resize image doesn't exist");
    response_cb->Return(response);
    return;
  }

  auto size = request.disk_size() & kDiskSizeMask;
  if (size != request.disk_size()) {
    LOG(INFO) << "Rounded requested disk size from " << request.disk_size()
              << " to " << size;
  }

  auto op = VmResizeOperation::Create(
      vm_id, location, disk_path, size,
      base::BindOnce(&Service::ResizeDisk, weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&Service::ProcessResize,
                          weak_ptr_factory_.GetWeakPtr()));

  response.set_status(op->status());
  response.set_command_uuid(op->uuid());
  response.set_failure_reason(op->failure_reason());

  if (op->status() == DISK_STATUS_IN_PROGRESS) {
    std::string uuid = op->uuid();
    disk_image_ops_.emplace_back(std::move(op));
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Service::RunDiskImageOperation,
                       weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
  } else if (op->status() == DISK_STATUS_RESIZED) {
    DiskImageStatusEnum status = DISK_STATUS_RESIZED;
    std::string failure_reason;
    FinishResize(vm_id, location, &status, &failure_reason);
    if (status != DISK_STATUS_RESIZED) {
      response.set_status(status);
      response.set_failure_reason(failure_reason);
    }
  }

  response_cb->Return(response);
  return;
}

void Service::ResizeDisk(const VmId& vm_id,
                         StorageLocation location,
                         uint64_t new_size,
                         DiskImageStatusEnum* status,
                         std::string* failure_reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Unable to find VM " << vm_id.name();
    *failure_reason = "No such image";
    *status = DISK_STATUS_DOES_NOT_EXIST;
    return;
  }

  *status = iter->second->ResizeDisk(new_size, failure_reason);
}

void Service::ProcessResize(const VmId& vm_id,
                            StorageLocation location,
                            uint64_t target_size,
                            DiskImageStatusEnum* status,
                            std::string* failure_reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Unable to find VM " << vm_id.name();
    *failure_reason = "No such image";
    *status = DISK_STATUS_DOES_NOT_EXIST;
    return;
  }

  *status = iter->second->GetDiskResizeStatus(failure_reason);

  if (*status == DISK_STATUS_RESIZED) {
    FinishResize(vm_id, location, status, failure_reason);
  }
}

void Service::FinishResize(const VmId& vm_id,
                           StorageLocation location,
                           DiskImageStatusEnum* status,
                           std::string* failure_reason) {
  base::FilePath disk_path;
  if (!GetDiskPathFromName(vm_id, location, &disk_path)) {
    LOG(ERROR) << "Failed to get disk path after resize";
    *failure_reason = "Failed to get disk path after resize";
    *status = DISK_STATUS_FAILED;
    return;
  }

  base::ScopedFD fd(
      open(disk_path.value().c_str(), O_CREAT | O_NONBLOCK | O_WRONLY, 0600));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open disk image";
    *failure_reason = "Failed to open disk image";
    *status = DISK_STATUS_FAILED;
    return;
  }

  // This disk now has a user-chosen size by virtue of being resized.
  if (!SetPreallocatedWithUserChosenSizeAttr(fd)) {
    LOG(ERROR) << "Failed to set user-chosen size xattr";
    *failure_reason = "Failed to set user-chosen size xattr";
    *status = DISK_STATUS_FAILED;
    return;
  }
}

void Service::ExportDiskImage(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        ExportDiskImageResponse>> response_cb,
    const ExportDiskImageRequest& request,
    const std::vector<base::ScopedFD>& fds) {
  ASYNC_SERVICE_METHOD();

  ExportDiskImageResponse response;
  response.set_status(DISK_STATUS_FAILED);

  if (fds.size() == 0) {
    LOG(ERROR) << "Need 1 or 2 fds";
    response.set_failure_reason("Need 1 or 2 fds");
    response_cb->Return(response);
    return;
  }

  // Get the FD to fill with disk image data.
  base::ScopedFD storage_fd{dup(fds[0].get())};

  base::ScopedFD digest_fd;
  if (request.generate_sha256_digest()) {
    if (fds.size() != 2) {
      LOG(ERROR) << "export: no digest fd found";
      response.set_failure_reason("export: no digest fd found");
      response_cb->Return(response);
      return;
    }
    digest_fd.reset(dup(fds[1].get()));
  }

  response_cb->Return(ExportDiskImageInternal(
      std::move(request), std::move(storage_fd), std::move(digest_fd)));
  return;
}

ExportDiskImageResponse Service::ExportDiskImageInternal(
    ExportDiskImageRequest request,
    base::ScopedFD storage_fd,
    base::ScopedFD digest_fd) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ExportDiskImageResponse response;
  response.set_status(DISK_STATUS_FAILED);

  VmId vm_id(request.cryptohome_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response.set_status(DISK_STATUS_FAILED);
    return response;
  }

  base::FilePath disk_path;
  StorageLocation location;
  if (!CheckVmExists(vm_id, &disk_path, &location)) {
    response.set_status(DISK_STATUS_DOES_NOT_EXIST);
    response.set_failure_reason("Export image doesn't exist");
    return response;
  }

  if (!request.force()) {
    // Ensure the VM is not currently running. This is sufficient to ensure
    // a consistent on-disk state.
    if (FindVm(vm_id) != vms_.end()) {
      LOG(ERROR) << "VM " << request.vm_name() << " is currently running";
      response.set_failure_reason("VM is currently running");
      return response;
    }
  }

  // Non-plugin VMs will only be exported in zstd compression
  // Non-plugin VMs previously exported to zip can still be imported
  std::unique_ptr<DiskImageOperation> op;

  if (location == STORAGE_CRYPTOHOME_PLUGINVM) {
    op = PluginVmExportOperation::Create(
        vm_id, disk_path, std::move(storage_fd), std::move(digest_fd));
  } else {
    op = TerminaVmExportOperation::Create(
        vm_id, disk_path, std::move(storage_fd), std::move(digest_fd));
  }

  response.set_status(op->status());
  response.set_command_uuid(op->uuid());
  response.set_failure_reason(op->failure_reason());

  if (op->status() == DISK_STATUS_IN_PROGRESS) {
    std::string uuid = op->uuid();
    disk_image_ops_.emplace_back(std::move(op));
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Service::RunDiskImageOperation,
                       weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
  }

  return response;
}

void Service::ImportDiskImage(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        ImportDiskImageResponse>> response_cb,
    const ImportDiskImageRequest& request,
    const base::ScopedFD& in_fd) {
  ASYNC_SERVICE_METHOD();

  ImportDiskImageResponse response;
  response.set_status(DISK_STATUS_FAILED);

  VmId vm_id(request.cryptohome_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  base::FilePath disk_path;
  if (!GetDiskPathFromName(vm_id, request.storage_location(), &disk_path)) {
    response.set_failure_reason("Failed to set up vm image name");
    response_cb->Return(response);
    return;
  }

  base::ScopedFD source_file(dup(in_fd.get()));
  std::unique_ptr<DiskImageOperation> op;

  switch (request.storage_location()) {
    case STORAGE_CRYPTOHOME_ROOT:
      // Allow TerminaVm import to replace an existing VM, but only if stopped.
      if (FindVm(vm_id) != vms_.end()) {
        response.set_status(DISK_STATUS_EXISTS);
        response.set_failure_reason("VM is currently running");
        response_cb->Return(response);
        return;
      }

      op = TerminaVmImportOperation::Create(std::move(source_file), disk_path,
                                            request.source_size(), vm_id);
      break;

    case STORAGE_CRYPTOHOME_PLUGINVM:
      // Don't allow PluginVm import to replace an existing VM.
      if (CheckVmExists(vm_id)) {
        response.set_status(DISK_STATUS_EXISTS);
        response.set_failure_reason("VM/disk with such name already exists");
        response_cb->Return(response);
        return;
      }

      op = PluginVmImportOperation::Create(std::move(source_file), disk_path,
                                           request.source_size(), vm_id, bus_,
                                           vmplugin_service_proxy_);
      break;

    default:
      LOG(ERROR) << "Unsupported location for disk image import";
      response.set_failure_reason("Unsupported location for disk image import");
      response_cb->Return(response);
      return;
  }

  response.set_status(op->status());
  response.set_command_uuid(op->uuid());
  response.set_failure_reason(op->failure_reason());

  if (op->status() == DISK_STATUS_IN_PROGRESS) {
    std::string uuid = op->uuid();
    disk_image_ops_.emplace_back(std::move(op));
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Service::RunDiskImageOperation,
                       weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
  }

  response_cb->Return(response);
  return;
}

void Service::RunDiskImageOperation(std::string uuid) {
  auto iter =
      std::find_if(disk_image_ops_.begin(), disk_image_ops_.end(),
                   [&uuid](auto& info) { return info.op->uuid() == uuid; });

  if (iter == disk_image_ops_.end()) {
    LOG(ERROR) << "RunDiskImageOperation called with unknown uuid";
    return;
  }

  if (iter->canceled) {
    // Operation was cancelled. Now that our posted task is running we can
    // remove it from the list and not reschedule ourselves.
    disk_image_ops_.erase(iter);
    return;
  }

  auto op = iter->op.get();
  op->Run(kDefaultIoLimit);
  if (!iter->last_report_time.has_value() ||
      base::TimeTicks::Now() - iter->last_report_time.value() >
          kDiskOpReportInterval ||
      op->status() != DISK_STATUS_IN_PROGRESS) {
    LOG(INFO) << "Disk Image Operation: UUID=" << uuid
              << " progress: " << op->GetProgress()
              << " status: " << op->status();

    // Send the D-Bus signal out updating progress of the operation.
    DiskImageStatusResponse status;
    FormatDiskImageStatus(op, &status);
    concierge_adaptor_->SendDiskImageProgressSignal(status);

    // Note the time we sent out the notification.
    iter->last_report_time = base::TimeTicks::Now();
  }

  if (op->status() == DISK_STATUS_IN_PROGRESS) {
    // Reschedule ourselves so we can execute next chunk of work.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Service::RunDiskImageOperation,
                       weak_ptr_factory_.GetWeakPtr(), std::move(uuid)));
  }
}

void Service::DiskImageStatus(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        DiskImageStatusResponse>> response_cb,
    const DiskImageStatusRequest& request) {
  ASYNC_SERVICE_METHOD();

  DiskImageStatusResponse response;
  response.set_status(DISK_STATUS_FAILED);

  // Locate the pending command in the list.
  auto iter = std::find_if(disk_image_ops_.begin(), disk_image_ops_.end(),
                           [&request](auto& info) {
                             return info.op->uuid() == request.command_uuid();
                           });

  if (iter == disk_image_ops_.end() || iter->canceled) {
    LOG(ERROR) << "Unknown command uuid in DiskImageStatusRequest";
    response.set_failure_reason("Unknown command uuid");
    response_cb->Return(response);
    return;
  }

  auto op = iter->op.get();
  FormatDiskImageStatus(op, &response);

  // Erase operation form the list if it is no longer in progress.
  if (op->status() != DISK_STATUS_IN_PROGRESS) {
    disk_image_ops_.erase(iter);
  }

  response_cb->Return(response);
  return;
}

void Service::CancelDiskImageOperation(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        SuccessFailureResponse>> response_cb,
    const CancelDiskImageRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  // Locate the pending command in the list.
  auto iter = std::find_if(disk_image_ops_.begin(), disk_image_ops_.end(),
                           [&request](auto& info) {
                             return info.op->uuid() == request.command_uuid();
                           });

  if (iter == disk_image_ops_.end()) {
    LOG(ERROR) << "Unknown command uuid in CancelDiskImageRequest";
    response.set_failure_reason("Unknown command uuid");
    response_cb->Return(response);
    return;
  }

  auto op = iter->op.get();
  if (op->status() != DISK_STATUS_IN_PROGRESS) {
    response.set_failure_reason("Command is no longer in progress");
    response_cb->Return(response);
    return;
  }

  // Mark the operation as canceled. We can't erase it from the list right
  // away as there is a task posted for it. The task will erase this operation
  // when it gets to run.
  iter->canceled = true;

  response.set_success(true);
  response_cb->Return(response);
}

void Service::ListVmDisks(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<ListVmDisksResponse>>
        response_cb,
    const ListVmDisksRequest& request) {
  ASYNC_SERVICE_METHOD();

  ListVmDisksResponse response;

  if (!CheckVmNameAndOwner(request, response, true /* Empty VmName allowed*/)) {
    response_cb->Return(response);
    return;
  }

  response.set_success(true);
  response.set_total_size(0);

  for (int location = StorageLocation_MIN; location <= StorageLocation_MAX;
       location++) {
    if (request.all_locations() || location == request.storage_location()) {
      if (!ListVmDisksInLocation(request.cryptohome_id(),
                                 static_cast<StorageLocation>(location),
                                 request.vm_name(), &response)) {
        break;
      }
    }
  }

  response_cb->Return(response);
}

void Service::AttachNetDevice(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        AttachNetDeviceResponse>> response_cb,
    const AttachNetDeviceRequest& request) {
  ASYNC_SERVICE_METHOD();

  AttachNetDeviceResponse response;

  VmId vm_id(request.owner_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    response.set_failure_reason("Requested VM " + vm_id.name() +
                                " with owner " + vm_id.owner_id() +
                                " does not exist");
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }

  uint8_t out_bus;

  if (!iter->second->AttachNetDevice(request.tap_name(), &out_bus)) {
    response.set_failure_reason(
        "Failed to attach tap device due to crosvm error.");
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }
  response.set_success(true);
  response.set_guest_bus(out_bus);
  response_cb->Return(response);
}

void Service::DetachNetDevice(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        SuccessFailureResponse>> response_cb,
    const DetachNetDeviceRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  VmId vm_id(request.owner_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    response.set_failure_reason("Requested VM " + vm_id.name() +
                                " with owner " + vm_id.owner_id() +
                                " does not exist");
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }

  if (request.guest_bus() == 0 || request.guest_bus() > 0xFF) {
    response.set_failure_reason("PCI bus number " +
                                std::to_string(request.guest_bus()) +
                                " is out of valid range 1-255");
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }

  if (!iter->second->DetachNetDevice(request.guest_bus())) {
    response.set_failure_reason(
        "Failed to detach tap device due to crosvm error.");
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }

  response.set_success(true);
  response_cb->Return(response);
}

void Service::AttachUsbDevice(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        AttachUsbDeviceResponse>> response_cb,
    const AttachUsbDeviceRequest& request,
    const base::ScopedFD& fd) {
  ASYNC_SERVICE_METHOD();

  AttachUsbDeviceResponse response;

  VmId vm_id(request.owner_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    response.set_reason("Requested VM does not exist");
    response_cb->Return(response);
    return;
  }

  if (request.bus_number() > 0xFF) {
    LOG(ERROR) << "Bus number out of valid range " << request.bus_number();
    response.set_reason("Invalid bus number");
    response_cb->Return(response);
    return;
  }

  if (request.port_number() > 0xFF) {
    LOG(ERROR) << "Port number out of valid range " << request.port_number();
    response.set_reason("Invalid port number");
    response_cb->Return(response);
    return;
  }

  if (request.vendor_id() > 0xFFFF) {
    LOG(ERROR) << "Vendor ID out of valid range " << request.vendor_id();
    response.set_reason("Invalid vendor ID");
    response_cb->Return(response);
    return;
  }

  if (request.product_id() > 0xFFFF) {
    LOG(ERROR) << "Product ID out of valid range " << request.product_id();
    response.set_reason("Invalid product ID");
    response_cb->Return(response);
    return;
  }

  uint8_t guest_port{};
  if (!iter->second->AttachUsbDevice(
          request.bus_number(), request.port_number(), request.vendor_id(),
          request.product_id(), fd.get(), &guest_port)) {
    LOG(ERROR) << "Failed to attach USB device.";
    response.set_reason("Error from crosvm");
    response_cb->Return(response);
    return;
  }
  response.set_success(true);
  response.set_guest_port(guest_port);
  response_cb->Return(response);
}

void Service::DetachUsbDevice(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        SuccessFailureResponse>> response_cb,
    const DetachUsbDeviceRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  VmId vm_id(request.owner_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    response.set_failure_reason("Requested VM does not exist");
    response_cb->Return(response);
    return;
  }

  if (request.guest_port() > 0xFF) {
    LOG(ERROR) << "Guest port number out of valid range "
               << request.guest_port();
    response.set_failure_reason("Invalid guest port number");
    response_cb->Return(response);
    return;
  }

  if (!iter->second->DetachUsbDevice(request.guest_port())) {
    LOG(ERROR) << "Failed to detach USB device";
    response_cb->Return(response);
    return;
  }
  response.set_success(true);
  response_cb->Return(response);
}

void Service::AttachKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<AttachKeyResponse>>
        response_cb,
    const AttachKeyRequest& request,
    const base::ScopedFD& hidraw) {
  ASYNC_SERVICE_METHOD();

  AttachKeyResponse response;

  VmId vm_id(request.owner_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    response.set_reason("Requested VM does not exist");
    response_cb->Return(response);
    return;
  }

  uint8_t guest_port{};
  // TODO(b/333838456): refactor virtualization metrics in a single module
  std::string metric_name = base::StrCat(
      {"Virtualization.", apps::VmType_Name(iter->second->GetInfo().type), ".",
       "SecurityKeyAttach"});
  if (!iter->second->AttachKey(hidraw.get(), &guest_port)) {
    LOG(ERROR) << "Failed to attach security key.";
    response.set_reason("Error from crosvm");
    response_cb->Return(response);
    metrics_->SendBoolToUMA(metric_name, false);
    return;
  }
  metrics_->SendBoolToUMA(metric_name, true);
  response.set_success(true);
  response.set_guest_port(guest_port);
  response_cb->Return(response);
}

void Service::ListUsbDevices(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        ListUsbDeviceResponse>> response_cb,
    const ListUsbDeviceRequest& request) {
  ASYNC_SERVICE_METHOD();

  ListUsbDeviceResponse response;

  VmId vm_id(request.owner_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    response_cb->Return(response);
    return;
  }

  std::vector<UsbDeviceEntry> usb_list;
  if (!iter->second->ListUsbDevice(&usb_list)) {
    LOG(ERROR) << "Failed to list USB devices";
    response_cb->Return(response);
    return;
  }
  for (auto usb : usb_list) {
    UsbDeviceMessage* usb_proto = response.add_usb_devices();
    usb_proto->set_guest_port(usb.port);
    usb_proto->set_vendor_id(usb.vendor_id);
    usb_proto->set_product_id(usb.product_id);
  }
  response.set_success(true);
  response_cb->Return(response);
}

DnsSettings Service::ComposeDnsResponse() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DnsSettings dns_settings;
  for (const auto& server : nameservers_) {
    dns_settings.add_nameservers(server);
  }
  for (const auto& domain : search_domains_) {
    dns_settings.add_search_domains(domain);
  }
  return dns_settings;
}

void Service::GetDnsSettings(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<DnsSettings>>
        response_cb) {
  ASYNC_SERVICE_METHOD();

  response_cb->Return(ComposeDnsResponse());
}

void Service::SetVmCpuRestriction(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        SetVmCpuRestrictionResponse>> response_cb,
    const SetVmCpuRestrictionRequest& request) {
  ASYNC_SERVICE_METHOD();

  // TODO(yusukes,hashimoto): Instead of allowing Chrome to decide when to
  // restrict each VM's CPU usage, let Concierge itself do that for potentially
  // better security. See crrev.com/c/3564880 for more context.
  SetVmCpuRestrictionResponse response;

  bool success = false;
  const CpuRestrictionState state = request.cpu_restriction_state();
  switch (request.cpu_cgroup()) {
    case CPU_CGROUP_TERMINA:
      success = TerminaVm::SetVmCpuRestriction(state);
      break;
    case CPU_CGROUP_PLUGINVM:
      success = PluginVm::SetVmCpuRestriction(state);
      break;
    case CPU_CGROUP_ARCVM:
      success = ArcVm::SetVmCpuRestriction(state, GetCpuQuota());
      break;
    default:
      LOG(ERROR) << "Unknown cpu_group";
      break;
  }

  response.set_success(success);
  response_cb->Return(response);
}

void Service::ListVms(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<ListVmsResponse>>
        response_cb,
    const ListVmsRequest& request) {
  ASYNC_SERVICE_METHOD();

  ListVmsResponse response;

  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  for (const auto& vm_entry : vms_) {
    const auto& id = vm_entry.first;
    const auto& vm = vm_entry.second;

    if (id.owner_id() != request.owner_id()) {
      continue;
    }

    VmBaseImpl::Info info = vm->GetInfo();
    // The vms_ member only contains VMs with running crosvm instances. So the
    // STOPPED case below should not be possible.
    DCHECK(info.status != VmBaseImpl::Status::STOPPED);

    ExtendedVmInfo* proto = response.add_vms();
    proto->set_name(id.name());
    proto->set_owner_id(id.owner_id());
    *proto->mutable_vm_info() = ToVmInfo(info, false);
    proto->set_status(ToVmStatus(info.status));
  }
  response.set_success(true);
  response_cb->Return(response);
}

void Service::ReclaimVmMemory(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        ReclaimVmMemoryResponse>> response_cb,
    const ReclaimVmMemoryRequest& request) {
  ASYNC_SERVICE_METHOD();

  ReclaimVmMemoryResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    response.set_failure_reason("Requested VM does not exist");
    response_cb->Return(response);
    return;
  }

  const pid_t pid = iter->second->GetInfo().pid;
  const auto page_limit = request.page_limit();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ReclaimVmMemoryInternal, pid, page_limit),
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 ReclaimVmMemoryResponse>> response_sender,
             ReclaimVmMemoryResponse response) {
            std::move(response_sender)->Return(response);
          },
          std::move(response_cb)));
}

using AggressiveBalloonResponder = std::unique_ptr<
    brillo::dbus_utils::DBusMethodResponse<SuccessFailureResponse>>;

void Service::AggressiveBalloon(AggressiveBalloonResponder response_cb,
                                const AggressiveBalloonRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    response.set_failure_reason("Requested VM does not exist");
    response_cb->Return(response);
    return;
  }

  auto type = iter->second->GetInfo().type;

  if (!vm_memory_management_service_ ||
      !mm::MmService::ManagedVms().contains(type)) {
    LOG(ERROR) << "Requested VM " << vm_id.name()
               << " does not support aggressive balloon";
    response.set_failure_reason(
        "Requested VM does not support aggressive balloon");
    response_cb->Return(response);
    return;
  }

  auto cid = iter->second->GetInfo().cid;
  if (request.enable()) {
    LOG(INFO) << "Starting Aggressive Baloon for CID: " << cid;
    auto cb = base::BindPostTaskToCurrentDefault(base::BindOnce(
        &Service::OnAggressiveBalloonFinished, weak_ptr_factory_.GetWeakPtr(),
        std::move(response_cb), cid));
    vm_memory_management_service_->ReclaimUntilBlocked(
        cid, mm::ResizePriority::kAggressiveBalloon, std::move(cb));
  } else {
    LOG(INFO) << "Stopping Aggressive Baloon for CID: " << cid;
    vm_memory_management_service_->StopReclaimUntilBlocked(cid);
    response.set_success(true);
    response_cb->Return(response);
  }
}

void Service::OnAggressiveBalloonFinished(
    AggressiveBalloonResponder response_sender,
    int cid,
    bool success,
    const char* err_msg) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "Aggressive Balloon finished for VM: " << cid;

  // After aggressive balloon finishes, clear the blockers on the ARCVM balloon
  // so cached apps aren't immediately killed when they become cached.
  if (vm_memory_management_service_) {
    LOG(INFO) << "Clearing balloon blockers for VM: " << cid;
    vm_memory_management_service_->ClearBlockersUpToInclusive(
        cid, mm::ResizePriority::kAggressiveBalloon);
  }

  SuccessFailureResponse response;
  response.set_success(success);
  if (!success) {
    response.set_failure_reason(err_msg);
  }
  std::move(response_sender)->Return(response);
}

void Service::GetVmMemoryManagementKillsConnection(
    GetVmmmsKillsConnectionResponseSender response_cb,
    const GetVmMemoryManagementKillsConnectionRequest& in_request) {
  ASYNC_SERVICE_METHOD();

  GetVmMemoryManagementKillsConnectionResponse response;
  std::vector<base::ScopedFD> fds;

  if (!vm_memory_management_service_) {
    static constexpr char error[] = "Service is not enabled.";
    LOG(ERROR) << error;
    response.set_failure_reason(error);
    std::move(response_cb)->Return(response, fds);
    return;
  }

  auto fd = vm_memory_management_service_->GetKillsServerConnection();
  if (!fd.is_valid()) {
    static constexpr char error[] = "Failed to connect.";
    LOG(ERROR) << error;
    response.set_failure_reason(error);
    std::move(response_cb)->Return(response, fds);
    return;
  }

  fds.push_back(std::move(fd));

  response.set_success(true);

  // The timeout that the host (resourced) should use when waiting on a kill
  // decision response from VMMMS.
  static constexpr base::TimeDelta kVmMemoryManagementHostKillDecisionTimeout =
      base::Milliseconds(300);

  response.set_host_kill_request_timeout_ms(
      kVmMemoryManagementHostKillDecisionTimeout.InMilliseconds());
  std::move(response_cb)->Return(response, fds);
}

void Service::OnResolvConfigChanged(std::vector<std::string> nameservers,
                                    std::vector<std::string> search_domains) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (nameservers_ == nameservers && search_domains_ == search_domains) {
    return;
  }

  nameservers_ = std::move(nameservers);
  search_domains_ = std::move(search_domains);

  for (auto& vm_entry : vms_) {
    auto& vm = vm_entry.second;
    if (vm->IsSuspended()) {
      // The VM is currently suspended and will not respond to RPCs.
      // SetResolvConfig() will be called when the VM resumes.
      continue;
    }
    vm->SetResolvConfig(nameservers_, search_domains_);
  }

  // Broadcast DnsSettingsChanged signal so Plugin VM dispatcher is aware as
  // well.
  concierge_adaptor_->SendDnsSettingsChangedSignal(ComposeDnsResponse());
}

void Service::OnDefaultNetworkServiceChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& vm_entry : vms_) {
    auto& vm = vm_entry.second;
    if (vm->IsSuspended()) {
      continue;
    }
    vm->HostNetworkChanged();
  }
}

void Service::NotifyCiceroneOfVmStarted(const VmId& vm_id,
                                        uint32_t cid,
                                        pid_t pid,
                                        std::string vm_token,
                                        vm_tools::apps::VmType vm_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  vm_tools::cicerone::NotifyVmStartedRequest request;
  request.set_owner_id(vm_id.owner_id());
  request.set_vm_name(vm_id.name());
  request.set_cid(cid);
  request.set_vm_token(std::move(vm_token));
  request.set_pid(pid);
  request.set_vm_type(vm_type);

  bus_->GetDBusTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](org::chromium::VmCiceroneProxy* proxy,
                        vm_tools::cicerone::NotifyVmStartedRequest request) {
                       brillo::ErrorPtr error;
                       EmptyMessage unused;
                       if (!proxy->NotifyVmStarted(request, &unused, &error)) {
                         LOG(ERROR)
                             << "Failed notifying cicerone of VM startup";
                       }
                     },
                     cicerone_service_proxy_.get(), std::move(request)));
}

void Service::HandleControlSocketReady(const VmId& vm_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto path = base::FilePath(vms_[vm_id]->GetVmSocketPath());

  // Initialize the watcher before we check if the path exists
  // to avoid racing with the socket being created.
  vm_socket_ready_watchers_.emplace(std::piecewise_construct,
                                    std::make_tuple(vm_id), std::make_tuple());
  if (!vm_socket_ready_watchers_[vm_id].Watch(
          path, base::FilePathWatcher::Type::kNonRecursive,
          base::BindRepeating(&Service::OnControlSocketChange,
                              weak_ptr_factory_.GetWeakPtr(), vm_id))) {
    PLOG(ERROR) << "Failed to initialize file watcher " << vm_id;
    vm_socket_ready_watchers_.erase(vm_id);
  }

  if (base::PathExists(path)) {
    OnControlSocketReady(vm_id);
  }
}

void Service::OnControlSocketChange(const VmId& vm_id,
                                    const base::FilePath&,
                                    bool error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "VM " << vm_id.name() << " stopped prematurely";
    vm_socket_ready_watchers_.erase(vm_id);
    return;
  }

  if (error) {
    LOG(ERROR) << "Control socket watcher error " << vm_id;
    vm_socket_ready_watchers_.erase(vm_id);
  }

  if (base::PathExists(base::FilePath(iter->second->GetVmSocketPath()))) {
    OnControlSocketReady(vm_id);
  }
}

void Service::OnControlSocketReady(const VmId& vm_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  vm_socket_ready_watchers_.erase(vm_id);

  std::unique_ptr<VmBaseImpl>& vm = vms_[vm_id];
  VmBaseImpl::Info info = vm->GetInfo();

  if (vm_memory_management_service_ && vm->GetGuestMemorySize()) {
    vm_memory_management_service_->NotifyVmStarted(
        info.type, info.cid, vm->GetVmSocketPath(), *vm->GetGuestMemorySize());
  }

  if (BalloonTimerShouldRun() && !balloon_resizing_timer_.IsRunning()) {
    LOG(INFO) << "New VM. Starting balloon resize timer.";
    balloon_resizing_timer_.Start(FROM_HERE, base::Seconds(1), this,
                                  &Service::RunBalloonPolicy);
  }

  SendVmStartedSignal(vm_id, info);
}

void Service::SendVmStartedSignal(const VmId& vm_id,
                                  const VmBaseImpl::Info& vm_info) {
  vm_tools::concierge::VmStartedSignal proto;
  proto.set_owner_id(vm_id.owner_id());
  proto.set_name(vm_id.name());
  *proto.mutable_vm_info() = ToVmInfo(vm_info, false);
  proto.set_status(ToVmStatus(vm_info.status));
  concierge_adaptor_->SendVmStartedSignalSignal(proto);
}

void Service::SendVmStartingUpSignal(const VmId& vm_id,
                                     apps::VmType vm_type,
                                     uint64_t cid) {
  vm_tools::concierge::VmStartingUpSignal proto;
  proto.set_owner_id(vm_id.owner_id());
  proto.set_name(vm_id.name());
  proto.set_vm_type(ToLegacyVmType(vm_type));
  proto.set_cid(cid);
  concierge_adaptor_->SendVmStartingUpSignalSignal(proto);
}

void Service::SendVmGuestUserlandReadySignal(
    const VmId& vm_id, const vm_tools::concierge::GuestUserlandReady ready) {
  vm_tools::concierge::VmGuestUserlandReadySignal proto;
  proto.set_owner_id(vm_id.owner_id());
  proto.set_name(vm_id.name());
  proto.set_ready(ready);
  concierge_adaptor_->SendVmGuestUserlandReadySignalSignal(proto);
}

void Service::NotifyVmStopping(const VmId& vm_id, int64_t cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (vm_memory_management_service_) {
    vm_memory_management_service_->NotifyVmStopping(cid);
  }

  // Notify cicerone.
  {
    vm_tools::cicerone::NotifyVmStoppingRequest request;
    request.set_owner_id(vm_id.owner_id());
    request.set_vm_name(vm_id.name());

    bus_->GetDBusTaskRunner()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](org::chromium::VmCiceroneProxy* proxy,
               vm_tools::cicerone::NotifyVmStoppingRequest request) {
              brillo::ErrorPtr error;
              EmptyMessage unused;
              if (!proxy->NotifyVmStopping(request, &unused, &error)) {
                LOG(ERROR) << "Failed notifying cicerone of stopping VM";
              }
            },
            cicerone_service_proxy_.get(), std::move(request)));
  }

  // Send the D-Bus signal out to notify everyone that we are stopping a VM.
  vm_tools::concierge::VmStoppingSignal proto;
  proto.set_owner_id(vm_id.owner_id());
  proto.set_name(vm_id.name());
  proto.set_cid(cid);
  concierge_adaptor_->SendVmStoppingSignalSignal(proto);
}

void Service::NotifyVmStopped(const VmId& vm_id,
                              int64_t cid,
                              VmStopReason reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Note: In the case of a VM crash, NotifyVmStopped is called without a
  // proceeding NotifyVmStopping(). In this case, the
  // vm_memory_management_service should still be informed that the VM has
  // stopped. Multiple NotifyVmStopping calls for the same VM are supported.
  if (vm_memory_management_service_) {
    vm_memory_management_service_->NotifyVmStopping(cid);
  }

  // Notify cicerone.
  {
    vm_tools::cicerone::NotifyVmStoppedRequest request;
    request.set_owner_id(vm_id.owner_id());
    request.set_vm_name(vm_id.name());

    bus_->GetDBusTaskRunner()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](org::chromium::VmCiceroneProxy* proxy,
               vm_tools::cicerone::NotifyVmStoppedRequest request) {
              brillo::ErrorPtr error;
              EmptyMessage unused;
              if (!proxy->NotifyVmStopped(request, &unused, &error)) {
                LOG(ERROR) << "Failed notifying cicerone of VM stopped";
              }
            },
            cicerone_service_proxy_.get(), std::move(request)));
  }

  // Send the D-Bus signal out to notify everyone that we have stopped a VM.
  vm_tools::concierge::VmStoppedSignal proto;
  proto.set_owner_id(vm_id.owner_id());
  proto.set_name(vm_id.name());
  proto.set_cid(cid);
  proto.set_reason(reason);
  concierge_adaptor_->SendVmStoppedSignalSignal(proto);
}

std::string Service::GetContainerToken(const VmId& vm_id,
                                       const std::string& container_name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  vm_tools::cicerone::ContainerTokenRequest request;
  request.set_owner_id(vm_id.owner_id());
  request.set_vm_name(vm_id.name());
  request.set_container_name(container_name);

  return PostTaskAndWaitForResult(
      bus_->GetDBusTaskRunner(),
      base::BindOnce(
          [](org::chromium::VmCiceroneProxy* proxy,
             vm_tools::cicerone::ContainerTokenRequest request) {
            brillo::ErrorPtr error;
            vm_tools::cicerone::ContainerTokenResponse response;

            if (!proxy->GetContainerToken(request, &response, &error)) {
              LOG(ERROR) << "Failed getting container token from cicerone";
              return std::string();
            }
            return response.container_token();
          },
          cicerone_service_proxy_.get(), std::move(request)));
}

std::string Service::GetHostTimeZone() {
  base::FilePath system_timezone;
  // Timezone is set by creating a symlink to an existing file at
  // /usr/share/zoneinfo.
  if (!base::NormalizeFilePath(base::FilePath(kLocaltimePath),
                               &system_timezone)) {
    LOG(ERROR) << "Failed to get system timezone";
    return "";
  }

  base::FilePath zoneinfo(kZoneInfoPath);
  base::FilePath system_timezone_name;
  if (!zoneinfo.AppendRelativePath(system_timezone, &system_timezone_name)) {
    LOG(ERROR) << "Could not get name of timezone " << system_timezone.value();
    return "";
  }

  return system_timezone_name.value();
}

void Service::OnLocaltimeFileChanged(const base::FilePath& path, bool error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (error) {
    LOG(WARNING) << "Error while reading system timezone change";
    return;
  }

  LOG(INFO) << "System timezone changed, updating VM timezones";

  std::string timezone = GetHostTimeZone();
  for (auto& vm_entry : vms_) {
    auto& vm = vm_entry.second;
    std::string error_msg;
    if (!vm->SetTimezone(timezone, &error_msg)) {
      LOG(WARNING) << "Failed to set timezone for " << vm_entry.first.name()
                   << ": " << error_msg;
    }
  }
}

void Service::OnTremplinStartedSignal(
    const vm_tools::cicerone::TremplinStartedSignal& tremplin_started_signal) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  VmId vm_id(tremplin_started_signal.owner_id(),
             tremplin_started_signal.vm_name());
  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Received signal from an unknown VM " << vm_id.name();
    return;
  }
  LOG(INFO) << "Received request: " << __func__ << " for " << iter->first;
  iter->second->SetTremplinStarted();
}

void Service::OnVmToolsStateChangedSignal(dbus::Signal* signal) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string owner_id, vm_name;
  bool running;
  if (!pvm::dispatcher::ParseVmToolsChangedSignal(signal, &owner_id, &vm_name,
                                                  &running)) {
    return;
  }

  VmId vm_id(owner_id, vm_name);
  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Received signal from an unknown VM " << vm_id.name();
    return;
  }
  LOG(INFO) << "Received request: " << __func__ << " for " << iter->first;
  iter->second->VmToolsStateChanged(running);
}

void Service::OnSignalConnected(const std::string& interface_name,
                                const std::string& signal_name,
                                bool is_connected) {
  if (!is_connected) {
    LOG(ERROR) << "Failed to connect to interface name: " << interface_name
               << " for signal " << signal_name;
  } else {
    LOG(INFO) << "Connected to interface name: " << interface_name
              << " for signal " << signal_name;
  }

  if (interface_name == vm_tools::cicerone::kVmCiceroneInterface) {
    DCHECK_EQ(signal_name, vm_tools::cicerone::kTremplinStartedSignal);
    is_tremplin_started_signal_connected_ = is_connected;
  }
}

void Service::HandleSuspendImminent() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const auto& pair : vms_) {
    VMT_TRACE(kCategory, "Service::HandleSuspendImminent::vm", "name",
              pair.first.name());
    auto& vm = pair.second;
    if (vm->UsesExternalSuspendSignals()) {
      continue;
    }
    vm->Suspend();
  }
}

void Service::HandleSuspendDone() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const auto& vm_entry : vms_) {
    VMT_TRACE(kCategory, "Service::HandleSuspendDone::vm", "name",
              vm_entry.first.name());
    auto& vm = vm_entry.second;
    if (vm->UsesExternalSuspendSignals()) {
      continue;
    }

    vm->Resume();

    std::string failure_reason;
    if (!vm->SetTime(&failure_reason)) {
      LOG(ERROR) << "Failed to set VM clock in " << vm_entry.first << ": "
                 << failure_reason;
    }

    vm->SetResolvConfig(nameservers_, search_domains_);
  }
}

Service::VmMap::iterator Service::FindVm(const VmId& vm_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return vms_.find(vm_id);
}

// TODO(b/244486983): move this functionality to shadercached
Service::VMGpuCacheSpec Service::PrepareVmGpuCachePaths(
    const VmId& vm_id, bool enable_render_server, bool enable_foz_db_list) {
  // We want to delete and recreate the cache directory atomically, and in order
  // to do that we ensure that this method runs on the main thread always.
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::FilePath cache_path = GetVmGpuCachePathInternal(vm_id);
  // Cache ID is either boot id or OS build hash
  base::FilePath cache_id_path = cache_path.DirName();
  base::FilePath base_path = cache_id_path.DirName();

  base::FilePath cache_device_path = cache_path.Append("device");
  base::FilePath cache_render_server_path =
      enable_render_server ? cache_path.Append("render_server")
                           : base::FilePath();
  base::FilePath foz_db_list_file =
      enable_render_server ? cache_render_server_path.Append("foz_db_list.txt")
                           : base::FilePath();

  const base::FilePath* cache_subdir_paths[] = {&cache_device_path,
                                                &cache_render_server_path};
  const base::FilePath* permissions_to_update[] = {
      &base_path, &cache_id_path, &cache_path, &cache_device_path,
      &cache_render_server_path};

  // In order to always provide an empty GPU shader cache on each boot or
  // build id change, we hash the boot_id or build number, and erase the whole
  // GPU cache if a directory matching the current boot id or build number hash
  // is not found.
  // For example:
  // VM cache dir: /run/daemon-store/crosvm/<uid>/gpucache/<cacheid>/<vmid>/
  // Cache ID dir: /run/daemon-store/crosvm/<uid>/gpucache/<cacheid>/
  // Base dir: /run/daemon-store/crosvm/<uid>/gpucache/
  // If Cache ID dir exists we know another VM has already created a fresh base
  // dir during this boot or OS release. Otherwise, we erase Base dir to wipe
  // out any previous Cache ID dir.
  if (!base::DirectoryExists(cache_id_path)) {
    LOG(INFO) << "GPU cache dir not found, deleting base directory";
    if (!base::DeletePathRecursively(base_path)) {
      LOG(WARNING) << "Failed to delete gpu cache directory: " << base_path
                   << " shader caching will be disabled.";
      return VMGpuCacheSpec{};
    }
  }

  for (const base::FilePath* path : cache_subdir_paths) {
    if (path->empty()) {
      continue;
    }

    if (!base::DirectoryExists(*path)) {
      base::File::Error dir_error;
      if (!base::CreateDirectoryAndGetError(*path, &dir_error)) {
        LOG(WARNING) << "Failed to create crosvm gpu cache directory in "
                     << *path << ": " << base::File::ErrorToString(dir_error);
        base::DeletePathRecursively(cache_path);
        return VMGpuCacheSpec{};
      }
    }
  }

  for (const base::FilePath* path : permissions_to_update) {
    if (base::IsLink(*path)) {
      continue;
    }
    // Group rx permission needed for VM shader cache management by shadercached
    if (!base::SetPosixFilePermissions(*path, 0750)) {
      LOG(WARNING) << "Failed to set directory permissions for " << *path;
    }
  }

  if (!foz_db_list_file.empty()) {
    bool file_exists = base::PathExists(foz_db_list_file);
    if (enable_foz_db_list) {
      // Initiate foz db file, if it already exists, continue using it
      if (!file_exists) {
        if (!base::WriteFile(foz_db_list_file, "")) {
          LOG(WARNING) << "Failed to create foz db list file";
          return VMGpuCacheSpec{};
        }
      }
      if (!base::SetPosixFilePermissions(foz_db_list_file, 0774)) {
        LOG(WARNING) << "Failed to set file permissions for "
                     << foz_db_list_file;
        return VMGpuCacheSpec{};
      }
    } else if (file_exists) {
      LOG(WARNING) << "Dynamic GPU RO cache loading is disabled but the "
                      "feature management file exists";
    }
  }

  return VMGpuCacheSpec{.device = std::move(cache_device_path),
                        .render_server = std::move(cache_render_server_path),
                        .foz_db_list = std::move(foz_db_list_file)};
}

void AddGroupPermissionChildren(const base::FilePath& path) {
  auto enumerator = base::FileEnumerator(
      path, true,
      base::FileEnumerator::DIRECTORIES ^ base::FileEnumerator::SHOW_SYM_LINKS);

  for (base::FilePath child_path = enumerator.Next(); !child_path.empty();
       child_path = enumerator.Next()) {
    if (child_path == path) {
      // Do not change permission for the root path
      continue;
    }

    int permission;
    if (!base::GetPosixFilePermissions(child_path, &permission)) {
      LOG(WARNING) << "Failed to get permission for " << path.value();
    } else if (!base::SetPosixFilePermissions(
                   child_path, permission | base::FILE_PERMISSION_GROUP_MASK)) {
      LOG(WARNING) << "Failed to change permission for " << child_path.value();
    }
  }
}

void Service::AddGroupPermissionMesa(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response_cb,
    const AddGroupPermissionMesaRequest& request) {
  ASYNC_SERVICE_METHOD();

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request,
                           request /* in place of a response proto */)) {
    response_cb->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                                DBUS_ERROR_FAILED,
                                "Empty or malformed owner ID / VM name");
    return;
  }

  base::FilePath cache_path = GetVmGpuCachePathInternal(vm_id);
  AddGroupPermissionChildren(cache_path);

  response_cb->Return();
}

void Service::GetVmLaunchAllowed(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        GetVmLaunchAllowedResponse>> response_cb,
    const GetVmLaunchAllowedRequest& request) {
  ASYNC_SERVICE_METHOD();

  std::string reason;
  bool allowed = untrusted_vm_utils_.SafeToRunVirtualMachines(&reason);

  if (allowed) {
    LOG(INFO) << "VM launch allowed";
  } else {
    LOG(INFO) << "VM launch not allowed: " << reason;
  }

  GetVmLaunchAllowedResponse response;
  response.set_allowed(allowed);
  response.set_reason(reason);
  response_cb->Return(response);
}

void Service::GetVmLogs(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<GetVmLogsResponse>>
        response_cb,
    const GetVmLogsRequest& request) {
  ASYNC_SERVICE_METHOD();

  GetVmLogsResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                                DBUS_ERROR_FAILED,
                                "Empty or malformed owner ID / VM name");
    return;
  }

  base::FilePath log_path = GetVmLogPath(vm_id, kCrosvmLogFileExt);

  std::vector<base::FilePath> paths;
  int64_t remaining_log_space = kMaxGetVmLogsSize;
  if (base::PathExists(log_path)) {
    std::optional<int64_t> size = base::GetFileSize(log_path);
    if (!size.has_value()) {
      response_cb->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                                  DBUS_ERROR_FAILED, "Failed to get log size");
      return;
    }
    remaining_log_space -= size.value();
    paths.push_back(log_path);

    for (int i = 1; i <= 5; i++) {
      base::FilePath older_log_path =
          log_path.AddExtension(base::NumberToString(i));

      // Don't read older logs if the total log size read is above the limit.
      if (base::PathExists(older_log_path) && remaining_log_space > 0) {
        size = base::GetFileSize(older_log_path);
        if (!size.has_value()) {
          break;
        }

        remaining_log_space -= size.value();
        paths.push_back(older_log_path);
      } else {
        break;
      }
    }
  }

  for (auto& path : std::ranges::reverse_view(paths)) {
    std::string file_contents;
    if (!base::ReadFileToString(path, &file_contents)) {
      continue;
    }

    std::string_view contents_view{file_contents};
    // Truncate the earliest log, if it would exceed the log size limit.
    if (remaining_log_space < 0) {
      contents_view.remove_prefix(-remaining_log_space);
      remaining_log_space = 0;
    }

    response.mutable_log()->append(contents_view);
  }

  response_cb->Return(response);
}

void Service::SwapVm(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                         SuccessFailureResponse>> response_cb,
                     const SwapVmRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM " << vm_id.name() << " does not exist";
    response.set_failure_reason("Requested VM does not exist");
    response_cb->Return(response);
    return;
  }

  iter->second->HandleSwapVmRequest(
      request, base::BindOnce(
                   [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                          SuccessFailureResponse>> response_sender,
                      SuccessFailureResponse response) {
                     std::move(response_sender)->Return(response);
                   },
                   std::move(response_cb)));
}

void Service::NotifyVmSwapping(const VmId& vm_id,
                               SwappingState swapping_state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Send the D-Bus signal out to notify everyone that we are swapping a VM.
  vm_tools::concierge::VmSwappingSignal proto;
  proto.set_owner_id(vm_id.owner_id());
  proto.set_name(vm_id.name());
  proto.set_state(swapping_state);
  concierge_adaptor_->SendVmSwappingSignalSignal(proto);
}

void Service::InstallPflash(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        SuccessFailureResponse>> response_cb,
    const InstallPflashRequest& request,
    const base::ScopedFD& pflash_src_fd) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;

  VmId vm_id(request.owner_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  std::optional<PflashMetadata> pflash_metadata = GetPflashMetadata(vm_id);
  if (!pflash_metadata) {
    response.set_failure_reason("Failed to get pflash install path");
    response_cb->Return(response);
    return;
  }

  // We only allow one Pflash file to be allowed during the lifetime of a VM.
  if (pflash_metadata->is_installed) {
    response.set_failure_reason("Pflash already installed");
    response_cb->Return(response);
    return;
  }

  // No Pflash is installed that means we can associate the given file with
  // the VM by copying it to a file derived from the VM's name itself.
  base::FilePath pflash_src_path =
      base::FilePath(kProcFileDescriptorsPath)
          .Append(base::NumberToString(pflash_src_fd.get()));

  LOG(INFO) << "Installing Pflash file for VM: " << vm_id.name()
            << " to: " << pflash_metadata->path;
  if (!base::CopyFile(pflash_src_path, pflash_metadata->path)) {
    response.set_failure_reason("Failed to copy pflash image");
    response_cb->Return(response);
    return;
  }

  response.set_success(true);
  response_cb->Return(response);
}

// TODO(b/244486983): separate out GPU VM cache methods out of service.cc file
void Service::GetVmGpuCachePath(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        GetVmGpuCachePathResponse>> response_cb,
    const GetVmGpuCachePathRequest& request) {
  ASYNC_SERVICE_METHOD();

  GetVmGpuCachePathResponse response;

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                                DBUS_ERROR_FAILED,
                                "Empty or malformed owner ID / VM name");
    return;
  }

  base::FilePath path = GetVmGpuCachePathInternal(vm_id);
  if (!base::DirectoryExists(path)) {
    response_cb->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                                DBUS_ERROR_FAILED,
                                "GPU cache path does not exist");
    return;

  } else if (path.empty()) {
    response_cb->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                                DBUS_ERROR_FAILED, "GPU cache path is empty");
    return;
  }

  response.set_path(path.value());
  response_cb->Return(response);
  return;
}

int Service::GetCpuQuota() {
  const feature::PlatformFeatures::ParamsResult result =
      feature::PlatformFeatures::Get()->GetParamsAndEnabledBlocking(
          {&kArcVmInitialThrottleFeature});

  const auto result_iter = result.find(kArcVmInitialThrottleFeatureName);
  if (result_iter == result.end()) {
    LOG(ERROR) << "Failed to get params for "
               << kArcVmInitialThrottleFeatureName;
    return kCpuPercentUnlimited;
  }

  const auto& entry = result_iter->second;
  if (!entry.enabled) {
    return kCpuPercentUnlimited;  // cfs_quota feature is disabled.
  }

  auto quota =
      FindIntValue(entry.params, kArcVmInitialThrottleFeatureQuotaParam);
  if (!quota) {
    return kCpuPercentUnlimited;
  }

  return std::min(100, std::max(1, *quota));
}

void Service::OnStatefulDiskSpaceUpdate(
    const spaced::StatefulDiskSpaceUpdate& update) {
  VMT_TRACE(kCategory, "Service::OnStatefulDiskSpaceUpdate");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& iter : vms_) {
    iter.second->HandleStatefulUpdate(update);
  }
}

bool Service::BalloonTimerShouldRun() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If there are no VMs, there is no need for the balloon timer.
  if (vms_.size() == 0) {
    return false;
  }

  // If there are VMs but VmMemoryManagementService has not been initialized,
  // the balloon timer should run.
  if (!vm_memory_management_service_) {
    return true;
  }

  // If any VM is not managed by the VM Memory Management Service, the balloon
  // timer should run.
  for (const auto& vm : vms_) {
    if (!mm::MmService::ManagedVms().contains(vm.second->GetInfo().type)) {
      return true;
    }
  }

  return false;
}

// Sends a message to the Upstart DBUS service, which should be owned by
// init/root, to run the trim_filesystem.conf script
// (see platform2/vm_tools/init/trim_filesystem.conf). The script runs
// fstrim on the user filesystem if lvm is being used.
void Service::TrimUserFilesystem() {
  dbus::ObjectProxy* startup_proxy = bus_->GetObjectProxy(
      "com.ubuntu.Upstart",
      dbus::ObjectPath("/com/ubuntu/Upstart/jobs/trim_5ffilesystem"));
  if (!startup_proxy) {
    LOG(ERROR) << "Unable to get dbus proxy for Upstart";
    return;
  }

  dbus::MethodCall method_call("com.ubuntu.Upstart0_6.Job", "Start");
  dbus::MessageWriter writer(&method_call);
  writer.AppendArrayOfStrings({});
  writer.AppendBool(true /* wait for response */);

  startup_proxy->CallMethodWithErrorResponse(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::BindOnce([](dbus::Response* response, dbus::ErrorResponse* error) {
        if (response) {
          LOG(INFO) << "trim_filesystem returned successfully";
        } else {
          std::string message;
          dbus::MessageReader reader(error);
          reader.PopString(&message);
          LOG(ERROR) << "trim_filesystem failed: " << message;
        }
      }));
}

void Service::RejectRequestDuringShutdown(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponseBase> response) {
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_FAILED, "Shutdown in progress");
}

void Service::SetUpVmUser(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<SetUpVmUserResponse>>
        response_cb,
    const SetUpVmUserRequest& request) {
  ASYNC_SERVICE_METHOD();

  SetUpVmUserResponse response;
  response.set_success(false);

  VmId vm_id(request.owner_id(), request.vm_name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    response.set_failure_reason("Requested VM " + vm_id.name() +
                                " does not exist");
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }
  auto& vm = iter->second;

  std::vector<std::string> group_names(request.group_names().begin(),
                                       request.group_names().end());
  std::optional<uid_t> uid;
  if (request.has_uid()) {
    uid = request.uid();
  }

  auto success = vm->SetUpUser(uid, request.username(), group_names,
                               response.mutable_username(),
                               response.mutable_failure_reason());

  response.set_success(success);
  response_cb->Return(response);
}

void Service::ModifyFakePowerConfig(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        SuccessFailureResponse>> response_cb,
    const ModifyFakePowerConfigRequest& request) {
  ASYNC_SERVICE_METHOD();

  SuccessFailureResponse response;
  response.set_success(false);

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    response.set_failure_reason("Requested VM " + vm_id.name() +
                                " does not exist");
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }

  if (request.action() == FakePowerAction::SET) {
    if (!iter->second->SetFakePowerConfig("goldfish",
                                          request.capacity_limit())) {
      response.set_failure_reason("Set fake power config failed");
      LOG(ERROR) << response.failure_reason();
      response_cb->Return(response);
      return;
    }
  } else if (request.action() == FakePowerAction::CANCEL) {
    if (!iter->second->CancelFakePowerConfig("goldfish")) {
      response.set_failure_reason("Cancel fake power config failed");
      LOG(ERROR) << response.failure_reason();
      response_cb->Return(response);
      return;
    }
  } else {
    response.set_failure_reason(
        "No valid action in ModifyFakePowerConfigRequest");
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }
  response.set_success(true);
  response_cb->Return(response);
}

void Service::MuteVmAudio(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        SuccessFailureResponse>> response_cb,
    const MuteVmAudioRequest& request) {
  ASYNC_SERVICE_METHOD();
  SuccessFailureResponse response;
  response.set_success(false);

  VmId vm_id(request.owner_id(), request.name());
  if (!CheckVmNameAndOwner(request, response)) {
    response_cb->Return(response);
    return;
  }

  auto iter = FindVm(vm_id);
  if (iter == vms_.end()) {
    response.set_failure_reason("Requested VM " + vm_id.name() +
                                " does not exist");
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }

  if (!iter->second->MuteVmAudio(request.muted())) {
    response.set_failure_reason(
        base::StringPrintf("Failed to set muted to %s from crosvm",
                           (request.muted() ? "true" : "false")));
    LOG(ERROR) << response.failure_reason();
    response_cb->Return(response);
    return;
  }

  response.set_success(true);
  response_cb->Return(response);
}

void Service::GetBaguetteImageUrl(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<GetBaguetteImageUrlResponse>>
        response_cb) {
  ASYNC_SERVICE_METHOD();

  // The URL follows the following format:
  // https://storage.googleapis.com/cros-containers/baguette/images/
  //    baguette_rootfs_$ARCH_$VERSION.img.zstd
  constexpr char prefix[] =
      "https://storage.googleapis.com/cros-containers/baguette/images/"
      "baguette_rootfs";
  constexpr char suffix[] = "img.zstd";

#if defined(__x86_64__)
  constexpr char arch[] = "amd64";
  const char* sha = kBaguetteSHA256X86;
#elif defined(__aarch64__) || defined(__arm__)
  constexpr char arch[] = "arm64";
  const char* sha = kBaguetteSHA256Arm;
#else
#error "Unsupported architecture for baguette"
#endif

  GetBaguetteImageUrlResponse response;
  response.set_url(base::StringPrintf("%s_%s_%s.%s", prefix, arch,
                                      kBaguetteVersion, suffix));
  response.set_sha256(sha);
  response_cb->Return(response);
}

std::optional<VhostUserFrontParam> Service::InvokeVhostUserFsBackend(
    SharedDirParam param, std::string_view syslog_tag) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Vhost-user-fs frontend device should share same tag with backend device.
  std::string shared_tag = param.tag;

  // Set up vhost-user-virtio-fs device, stub_device_socket_fds is a socket pair
  // used for connecting vhost_user frontend and backend.
  std::optional<VhostUserSocketPair> stub_device_socket_fds =
      internal::SetupVhostUserSocketPair();
  if (!stub_device_socket_fds.has_value()) {
    LOG(ERROR) << "Fail to create stub device vhost user socket pair.";
    return std::nullopt;
  }

  // Remove the CLOEXEC flag from the vhost-user frontend socket fd. This is
  // important to allow the fd to be inherited by the crosvm process.
  if (std::string failure_reason =
          internal::RemoveCloseOnExec(stub_device_socket_fds->front_end_fd);
      !failure_reason.empty()) {
    LOG(ERROR) << "Could not clear CLOEXEC for vhost_user fs frontend fd: "
               << failure_reason;
    return std::nullopt;
  }

  // Send dbus request to vhost_user_starter daemon to delegate starting backend
  // device.
  vhost_user_starter_client_->StartVhostUserFs(
      std::move(stub_device_socket_fds->back_end_fd), param, syslog_tag);

  return VhostUserFrontParam{
      .type = "fs",
      .socket_fd = std::move(stub_device_socket_fds->front_end_fd)};
}

}  // namespace vm_tools::concierge
