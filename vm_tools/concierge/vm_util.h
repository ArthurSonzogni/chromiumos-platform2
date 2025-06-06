// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VM_UTIL_H_
#define VM_TOOLS_CONCIERGE_VM_UTIL_H_

#include <sys/types.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>
#include <base/values.h>
#include <brillo/files/safe_fd.h>
#include <brillo/process/process.h>
#include <vhost_user_starter/proto_bindings/vhost_user_starter.pb.h>
#include <vm_applications/apps.pb.h>

#include "vm_tools/concierge/balloon_policy.h"
#include "vm_tools/concierge/vm_base_impl.h"

namespace base {
class FilePath;
}  // namespace base

namespace vm_tools {

namespace apps {
enum VmType : int;
}

namespace concierge {

namespace internal {
std::string GetDevConfPath(apps::VmType type);
int64_t GetVmMemoryMiBInternal(int64_t system, bool is_32bit);
std::vector<vm_tools::vhost_user_starter::IdMapItem> IdMapStringToIdMapItem(
    const std::string& id_map_string);
}  // namespace internal

enum VmInfo_VmType : int;

// Path to process file descriptors.
constexpr char kProcFileDescriptorsPath[] = "/proc/self/fd/";

// Reserved memory for host when sizing a VM.
constexpr int kHostReservedNumMiB = 1024;

// Describes key components of a VM.
struct VMImageSpec {
  base::FilePath kernel;
  base::FilePath initrd;
  base::FilePath rootfs;
  base::FilePath bios;
  base::FilePath pflash;
  base::FilePath tools_disk;
};

// Path to the crosvm binary.
extern const char kCrosvmBin[];

// Calculates the amount of memory to give the virtual machine, in MiB.
// Currently configured to provide 75% of system memory. This is deliberately
// over provisioned with the expectation that we will use the balloon driver to
// reduce the actual memory footprint.
int64_t GetVmMemoryMiB();

// Retrieves the physical package ID for |cpu| from the topology information in
// cpu_info_path.
std::optional<int32_t> GetCpuPackageId(int32_t cpu,
                                       const base::FilePath& cpu_info_path);

// Retrieves the CPU capacity property for |cpu| from cpu_info_path.
std::optional<int32_t> GetCpuCapacity(int32_t cpu,
                                      const base::FilePath& cpu_info_path);

// Calculate an appropriate CPU affinity setting based on the host system's
// CPU clusters and capacity. CPUs will be grouped based on cluster if multiple
// clusters exist, or based on groupings of equal CPU capacity if more than one
// such grouping exists. Otherwise, |nullopt| will be returned.
std::optional<std::string> GetCpuAffinityFromClusters(
    const std::vector<std::vector<std::string>>& cpu_clusters,
    const std::map<int32_t, std::vector<std::string>>& cpu_capacity_groups);

// Puts the current process in a CPU cgroup specificed by |cpu_cgroup|, and
// then calls SetPgid(). This function can be called as brillo::ProcessImpl's
// PreExecCallback.
bool SetUpCrosvmProcess(const base::FilePath& cpu_cgroup);

// Sets the pgid of the current process to its pid.  This is needed because
// crosvm assumes that only it and its children are in the same process group
// and indiscriminately sends a SIGKILL if it needs to shut them down. This
// function can be called as brillo::ProcessImpl's PreExecCallback.
bool SetPgid();

// Waits for the |pid| to exit.  Returns true if |pid| successfully exited and
// false if it did not exit in time.
bool WaitForChild(pid_t child, base::TimeDelta timeout);

// Returns true if a process with |pid| exists.
bool CheckProcessExists(pid_t pid);

// Returns balloon stats info retrieved from virtio-balloon device.
std::optional<BalloonStats> GetBalloonStats(
    const std::string& socket_path, std::optional<base::TimeDelta> timeout);

// Returns the guest working set from the virtio-balloon device.
std::optional<BalloonWorkingSet> GetBalloonWorkingSet(
    const std::string& socket_path);

// Attach a host network tap device with tap_name.
bool AttachNetDevice(const std::string& socket_path,
                     const std::string& tap_name,
                     uint8_t* out_bus);

// Detach a host network tap device on guest attached at bus.
bool DetachNetDevice(const std::string& socket_path, uint8_t bus);

// Attaches an usb device at host |bus|:|addr|, with |vid|, |pid| and an
// opened |fd|.
bool AttachUsbDevice(const std::string& socket_path,
                     uint8_t bus,
                     uint8_t addr,
                     uint16_t vid,
                     uint16_t pid,
                     int fd,
                     uint8_t* out_port,
                     apps::VmType vm_type);

// Attaches and shares a security key from the given opened |hidraw_fd|.
bool AttachKey(const std::string& socket_path,
               int hidraw_fd,
               uint8_t* out_port,
               apps::VmType vm_type);

// Detaches the usb device at guest |port|.
bool DetachUsbDevice(const std::string& socket_path, uint8_t port);

// Lists all usb devices attached to guest.
bool ListUsbDevice(const std::string& socket_path,
                   std::vector<UsbDeviceEntry>* devices);

// Resizes the disk identified by |disk_index| to |new_size| in bytes.
bool CrosvmDiskResize(const std::string& socket_path,
                      int disk_index,
                      uint64_t new_size);

// Updates |cpu_cgroup|'s cpu.shares to |cpu_shares|.
bool UpdateCpuShares(const base::FilePath& cpu_cgroup, int cpu_shares);

// Updates |cpu_cgroup|'s cpu.cfs_quota_us and cpu.cfs_period_us
// based on |percent|.
bool UpdateCpuQuota(const base::FilePath& cpu_cgroup, int percent);

// Updates |cpu_cgroup|'s cpu.uclamp.latency_sensitive to |enable|.
bool UpdateCpuLatencySensitive(const base::FilePath& cpu_cgroup, bool enable);

// Updates |cpu_cgroup|'s cpu.uclamp.min based on |percent|.
bool UpdateCpuUclampMin(const base::FilePath& cpu_cgroup, double percent);

// Returns the new vCPU count to use in a Borealis VM
uint32_t GetBorealisCpuCountOverride(uint32_t num_cpus);

// Convert file path into fd path
// This will open the file and append SafeFD into provided container
std::string ConvertToFdBasedPath(brillo::SafeFD& parent_fd,
                                 base::FilePath* in_out_path,
                                 int flags,
                                 std::vector<brillo::SafeFD>& fd_storage);

// Convert the given |type| to the legacy VM type defined in
// vm_concierge/concierge.pb
VmInfo_VmType ToLegacyVmType(apps::VmType type);

// Convert the given VM |status| to public protobuf variant
// vm_tools::concierge::VmStatus
VmStatus ToVmStatus(VmBaseImpl::Status status);

// Converts VM |info| to public protobuf variant vm_tools::concierge::VmInfo.
// If |fill_sensitive_info| is true then sensitive data, such as permission
// token, will be included in the result. Typically sensitive data is excluded
// when result of the conversion is used in DBus signals, and included in
// results of DBus method calls.
VmInfo ToVmInfo(const VmBaseImpl::Info& info, bool fill_sensitive_info);

class CustomParametersForDev {
 public:
  // By default this class would do nothing.
  CustomParametersForDev() = default;
  CustomParametersForDev(const CustomParametersForDev&) = delete;
  CustomParametersForDev& operator=(CustomParametersForDev&) = delete;

  // Allow custom parameters on development devices with arcvm_dev.conf.
  // Loads custom parameters from a string. Please check
  // vm_tools/init/arcvm_dev.conf for the list of supported directives.
  explicit CustomParametersForDev(const std::string& data);

  // Apply the parsed result of configuration files to |args| as a vector of
  // string pairs.
  void Apply(base::StringPairs& args);

  std::optional<const std::string> ObtainSpecialParameter(
      const std::string& key) const;
  std::vector<std::string> ObtainSpecialParameters(
      const std::string& key) const;

  // Parameters before crosvm command itself. Useful for adding things like
  // strace.
  std::vector<std::string> ObtainPrecrosvmParams() const {
    return precrosvm_params_;
  }
  // Append to parameters between crosvm's `crosvm` and `run` command.
  void AppendPrerunParams(base::StringPairs& pre_run_args) const;

 private:
  // Command line parameters before `run` option.
  base::StringPairs prerun_params_{};
  // Command line before `crosvm`
  std::vector<std::string> precrosvm_params_{};
  // Command line parameter prefix to 'crosvm run' to remove.
  std::vector<std::string> run_prefix_to_remove_{};
  // Command line parameters for 'crosvm run' to prepend.
  base::StringPairs run_params_to_prepend_{};
  // Command line parameters to 'crosvm run' to add.
  base::StringPairs run_params_to_add_{};
  // Other special handling.
  std::map<std::string, std::vector<std::string>> special_parameters_{};
  bool initialized_{false};
};

// Load file for custom parameters for dev systems. Or return nullptr when not
// available.
// |type| Vm type used to specify what file name to use.
// |use_dev_conf| Whether to allow use of dev_conf, sent via request.
std::unique_ptr<CustomParametersForDev> MaybeLoadCustomParametersForDev(
    apps::VmType type, bool use_dev_conf = true);

// Uid and gid mappings for the android data directory. This is a
// comma-separated list of 3 values: <start of range inside the user namespace>
// <start of range outside the user namespace> <count>. The values are taken
// from platform2/arc/container-bundle/pi/config.json.
constexpr char kAndroidUidMap[] =
    "0 655360 5000,5000 600 50,5050 660410 1994950";
constexpr char kAndroidGidMap[] =
    "0 655360 1065,1065 20119 1,1066 656426 3934,5000 600 50,5050 660410 "
    "1994950";

// Uid and gid mappings for the android data media directory(/run/arcvm/media/).
// The android root user(0) is mapped to chromeOS's chronos(1000), android
// root group(0) is mapped to chromeOS's chronos-access(1001). The android
// system group(1000) is mapped to android-root(656360).
constexpr char kStubUidMap[] = "0 1000 1";
constexpr char kStubGidMap[] = "0 1001 1,1000 656360 1";

// shared-dir parameter for crosvm.
struct SharedDirParam {
  enum class Cache {
    kAuto,
    kAlways,
    kNever,
  };
  struct CacheParameters {
    std::string cache;
    uint32_t timeout;
    bool writeback;
    uint32_t negative_timeout;
  };
  // Get command line parameter string for use with crosvm's --shared-dir
  // command-line option.
  std::string to_string() const;
  // Get VhostUserVirtioFsConfig proto message according to SharedDirParam,
  // this message will be parsed by vhost_user_starter daemon and config
  // vhost-user-fs backend's --cfg option in crosvm.
  vhost_user_starter::StartVhostUserFsRequest
  get_start_vhost_user_virtio_fs_request(std::string_view syslog_tag) const;

  base::FilePath data_dir;
  std::string tag;
  std::string uid_map;
  std::string gid_map;
  Cache enable_caches;
  bool rewrite_security_xattrs{true};
  bool ascii_casefold;
  bool posix_acl;
  int32_t max_dynamic_perm{0};
  int32_t max_dynamic_xattr{0};
  std::vector<uid_t> privileged_quota_uids;

 private:
  CacheParameters create_cache_parameters(SharedDirParam::Cache enable_caches,
                                          bool ascii_casefold) const;
};

// Parameters for vhost-user frontend.
struct VhostUserFrontParam {
  // Device type (e.g. "fs").
  std::string type;
  // Socket kept alive in concierge long enough until crosvm forks off.
  base::ScopedFD socket_fd;
  std::string to_string() const;
};

// Creates the font-specific shared dir parameter for crosvm.
SharedDirParam CreateFontsSharedDirParam();

class ArcVmCPUTopology {
 public:
  ArcVmCPUTopology(uint32_t num_cpus, uint32_t num_rt_cpus);
  ~ArcVmCPUTopology() = default;

  ArcVmCPUTopology(const ArcVmCPUTopology&) = delete;
  ArcVmCPUTopology& operator=(const ArcVmCPUTopology&) = delete;

  void CreateCPUAffinity();

  bool IsSymmetricCPU();

  uint32_t NumCPUs();
  uint32_t NumRTCPUs();
  void SetNumRTCPUs(uint32_t num_rt_cpus);
  const std::string& AffinityMask();
  const std::string& RTCPUMask();
  const std::string& NonRTCPUMask();
  const std::string& CapacityMask();
  const std::vector<std::string>& PackageMask();
  int TopAppUclampMin();
  double GlobalVMBoost();

  // Unit Testing crud
  void AddCpuToCapacityGroupForTesting(uint32_t cpu, uint32_t capacity);
  void AddCpuToPackageGroupForTesting(uint32_t cpu, uint32_t package);
  void CreateCPUAffinityForTesting();

 private:
  void CreateTopology();
  void CreateAffinity();

  // Total number of CPUs VM will be configured with
  uint32_t num_cpus_;
  // Number of RT CPUs
  uint32_t num_rt_cpus_;
  // CPU mask for RT CPUs
  std::string rt_cpu_mask_;
  // CPU mask for non RT CPUs
  std::string non_rt_cpu_mask_;
  // CPU affinity
  std::string affinity_mask_;
  // A set of RT CPUs
  std::set<uint32_t> rt_cpus_;
  // CPU capacity grouping
  std::map<uint32_t, std::vector<uint32_t>> capacity_;
  // CPU package grouping
  std::map<uint32_t, std::vector<uint32_t>> package_;
  // CPU capacity mask
  std::string capacity_mask_;
  // CPU package mask
  std::vector<std::string> package_mask_;
  // Default uclamp.min for performance tasks based on capacity
  int top_app_uclamp_min_;
  // Amount of the global VM boost, which should be applied to the host cgroups
  double global_vm_boost_;
};

class VmStartChecker {
 public:
  // The return type when we call |Wait|.
  enum Status {
    // VM is ready.
    READY = 0,

    // Invalid event received while epoll-ing.
    EPOLL_INVALID_EVENT,

    // Invalid fd received while epoll-ing.
    EPOLL_INVALID_FD,

    // Timed out waiting for the VM to start i.e. no event or signal received.
    TIMEOUT,

    // Invalid signal info.
    INVALID_SIGNAL_INFO,

    // Signal received while waiting.
    SIGNAL_RECEIVED
  };

  // Create an instance of |VmStartChecker|. |signal_fd| is owned by the client.
  static std::unique_ptr<VmStartChecker> Create(int32_t signal_fd);
  ~VmStartChecker() = default;

  VmStartChecker(const VmStartChecker&) = delete;
  VmStartChecker& operator=(const VmStartChecker&) = delete;

  // Wait for the VM to start with |timeout|.
  Status Wait(base::TimeDelta timeout);

  int32_t GetEventFd() const;

 private:
  VmStartChecker(int32_t signal_fd,
                 base::ScopedFD event_fd,
                 base::ScopedFD epoll_fd);

  // Signal fd associated with the client that constructs this object.
  int32_t signal_fd_;

  // Event fd created to monitor VM start up.
  base::ScopedFD event_fd_;

  // Epoll fd that will wait on both |event_fd_| and |signal_fd_|.
  base::ScopedFD epoll_fd_;
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_VM_UTIL_H_
