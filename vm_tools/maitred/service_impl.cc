// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/maitred/service_impl.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/ethtool.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/sockios.h>
#include <linux/vm_sockets.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <absl/algorithm/container.h>
#include <absl/strings/str_join.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/posix/safe_strerror.h>
#include <base/process/process_iterator.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/synchronization/lock.h>
#include <base/system/sys_info.h>
#include <brillo/file_utils.h>
#include <brillo/files/file_util.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <re2/re2.h>

#include "vm_tools/common/paths.h"

using std::string;

namespace vm_tools {
namespace maitred {
namespace {

// Default name of the interface in the VM.
constexpr char kInterfaceName[] = "eth0";
constexpr char kLoopbackName[] = "lo";

const std::vector<string> kDefaultNameservers = {
    "8.8.8.8", "8.8.4.4", "2001:4860:4860::8888", "2001:4860:4860::8844"};
constexpr char kResolvConfOptions[] =
    "options single-request timeout:1 attempts:5\n";
constexpr char kResolvConfPath[] = "/run/resolv.conf";
constexpr char kRunPath[] = "/run";
constexpr char kTmpResolvConfPath[] = "/run/resolv.conf.tmp";

constexpr char kLocaltimePath[] = "/etc/localtime";
constexpr char kZoneInfoPath[] = "/usr/share/zoneinfo";

constexpr int64_t kGiB = 1024 * 1024 * 1024;

constexpr char kLogindManagerInterface[] = "org.freedesktop.login1.Manager";
constexpr char kLogindServicePath[] = "/org/freedesktop/login1";
constexpr char kLogindServiceName[] = "org.freedesktop.login1";

// https://manpages.debian.org/testing/adduser/adduser.conf.5.en.html#NAME_REGEX
constexpr LazyRE2 kUsernameRegexp = {R"([a-z][-a-z0-9_]*\$?)"};

// Convert a 32-bit int in network byte order into a printable string.
string AddressToString(uint32_t address) {
  struct in_addr in = {
      .s_addr = address,
  };
  char buf[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &in, buf, INET_ADDRSTRLEN) == nullptr) {
    PLOG(ERROR) << "Failed to parse address " << address;
    return string("<unknown>");
  }

  return string(buf);
}

// Set a network interface's flags to be up and running. Returns 0 on success,
// or the saved errno otherwise.
int EnableInterface(int sockfd, const char* ifname) {
  struct ifreq ifr;
  int ret;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

  ret = HANDLE_EINTR(ioctl(sockfd, SIOCGIFFLAGS, &ifr));
  if (ret) {
    int saved_errno = errno;
    PLOG(ERROR) << "Failed to fetch flags for interface " << ifname;
    return saved_errno;
  }

  ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
  ret = HANDLE_EINTR(ioctl(sockfd, SIOCSIFFLAGS, &ifr));
  if (ret) {
    int saved_errno = errno;
    PLOG(ERROR) << "Failed to set flags for interface " << ifname;
    return saved_errno;
  }

  return 0;
}

// Prints an error log with the message in |error| concatenated with the
// string representation of the current value of errno. The same error message
// will also be stored in |out_error|.
void PLogAndSaveError(const string& error, string* out_error) {
  string error_with_strerror = error + ": " + base::safe_strerror(errno);
  LOG(ERROR) << error_with_strerror;
  out_error->assign(error_with_strerror);
}

// Sets a sysctl node to a supplied value.
bool SetSysctl(const char* path, const char* val, string* out_error) {
  DCHECK(out_error);
  base::ScopedFD sysctl_node(open(path, O_RDWR | O_CLOEXEC));

  if (!sysctl_node.is_valid()) {
    PLogAndSaveError(base::StringPrintf("unable to open sysctl node: %s", path),
                     out_error);
    return false;
  }

  ssize_t count = write(sysctl_node.get(), val, strlen(val));
  if (count != strlen(val)) {
    PLogAndSaveError(
        base::StringPrintf("failed to write sysctl node: %s", path), out_error);
    return false;
  }

  return true;
}

// Writes a resolv.conf with the supplied |nameservers| and |search_domains|.
// The default Chrome OS resolver options will be used. Returns true on
// success, and returns false on failure with an error message stored in
// |out_error|.
bool WriteResolvConf(const std::vector<string> nameservers,
                     const std::vector<string> search_domains,
                     string* out_error) {
  DCHECK(out_error);

  base::ScopedFD resolv_fd(
      HANDLE_EINTR(open(kRunPath, O_TMPFILE | O_WRONLY | O_CLOEXEC, 0644)));
  if (!resolv_fd.is_valid()) {
    PLogAndSaveError(
        base::StringPrintf("failed to open tmpfile in %s", kRunPath),
        out_error);
    return false;
  }

  for (auto& ns : nameservers) {
    string nameserver_line = base::StringPrintf("nameserver %s\n", ns.c_str());
    if (!base::WriteFileDescriptor(resolv_fd.get(), nameserver_line)) {
      PLogAndSaveError("failed to write nameserver to tmpfile", out_error);
      return false;
    }
  }

  if (!search_domains.empty()) {
    string search_domains_line = base::StringPrintf(
        "search %s\n", base::JoinString(search_domains, " ").c_str());
    if (!base::WriteFileDescriptor(resolv_fd.get(), search_domains_line)) {
      PLogAndSaveError("failed to write search domains to tmpfile", out_error);
      return false;
    }
  }

  if (!base::WriteFileDescriptor(resolv_fd.get(), kResolvConfOptions)) {
    PLogAndSaveError("failed to write resolver options to tmpfile", out_error);
    return false;
  }

  // The file has been successfully written to, so link it into place.
  // First link it to a named file with linkat(2), then atomically move it
  // into place with rename(2). linkat(2) will not overwrite the destination,
  // hence the need to do this in two steps.
  const base::FilePath source_path(
      base::StringPrintf("/proc/self/fd/%d", resolv_fd.get()));
  if (HANDLE_EINTR(linkat(AT_FDCWD, source_path.value().c_str(), AT_FDCWD,
                          kTmpResolvConfPath, AT_SYMLINK_FOLLOW)) < 0) {
    PLogAndSaveError(
        base::StringPrintf("failed to link tmpfile to %s", kTmpResolvConfPath),
        out_error);
    return false;
  }

  if (HANDLE_EINTR(rename(kTmpResolvConfPath, kResolvConfPath)) < 0) {
    PLogAndSaveError(
        base::StringPrintf("failed to rename tmpfile to %s", kResolvConfPath),
        out_error);
    return false;
  }

  return true;
}

}  // namespace

ServiceImpl::ServiceImpl(std::unique_ptr<vm_tools::maitred::Init> init,
                         bool maitred_is_pid1)
    : maitred_is_pid1_(maitred_is_pid1),
      init_(std::move(init)),
      lxd_env_({{"LXD_DIR", "/mnt/stateful/lxd"},
                {"LXD_CONF", "/mnt/stateful/lxd_conf"}}),
      stateful_mount_(base::FilePath("/mnt/stateful")),
      localtime_file_path_(kLocaltimePath),
      zoneinfo_file_path_(kZoneInfoPath) {}

bool ServiceImpl::Init() {
  string error;
  return WriteResolvConf(kDefaultNameservers, {}, &error);
}

grpc::Status ServiceImpl::ConfigureNetwork(grpc::ServerContext* ctx,
                                           const NetworkConfigRequest* request,
                                           EmptyMessage* response) {
  static_assert(sizeof(uint32_t) == sizeof(in_addr_t),
                "in_addr_t is not the same width as uint32_t");
  LOG(INFO) << "Received network configuration request";

  const IPv4Config& ipv4_config = request->ipv4_config();
  if (ipv4_config.address() == 0) {
    return grpc::Status(grpc::INVALID_ARGUMENT, "IPv4 address cannot be 0");
  }
  if (ipv4_config.netmask() == 0) {
    return grpc::Status(grpc::INVALID_ARGUMENT, "IPv4 netmask cannot be 0");
  }
  if (ipv4_config.gateway() == 0) {
    return grpc::Status(grpc::INVALID_ARGUMENT, "IPv4 gateway cannot be 0");
  }

  // Enable IP forwarding.
  string error;
  if (!SetSysctl("/proc/sys/net/ipv4/ip_forward", "1", &error)) {
    return grpc::Status(grpc::INTERNAL, error);
  }
  // accept_ra = 2: To accept RA packet even if forwarding == 1
  if (!SetSysctl(base::StringPrintf("/proc/sys/net/ipv6/conf/%s/accept_ra",
                                    kInterfaceName)
                     .c_str(),
                 "2", &error)) {
    return grpc::Status(grpc::INTERNAL, error);
  }
  if (!SetSysctl("/proc/sys/net/ipv6/conf/all/forwarding", "1", &error)) {
    return grpc::Status(grpc::INTERNAL, error);
  }

  base::ScopedFD fd(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!fd.is_valid()) {
    int saved_errno = errno;
    PLOG(ERROR) << "Failed to create socket";
    return grpc::Status(grpc::INTERNAL, string("failed to create socket: ") +
                                            strerror(saved_errno));
  }

  // Set up the address.
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, kInterfaceName, sizeof(ifr.ifr_name));

  // Holy fuck, who designed this interface?  Did you know that ifr_addr and
  // ifr_name are actually macros?!?  For example, ifr_addr expands to
  // ifr_ifru.ifru_addr and ifr_name expands to ifr_ifrn.ifrn_name.  This is
  // because the address, the flags, the netmask, and basically everything
  // else all share the same underlying storage via a union.  "Let's just put
  // everything into one union.  Who needs type safety anyway?". smh.
  struct sockaddr_in* addr =
      reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_addr);
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = static_cast<in_addr_t>(ipv4_config.address());

  if (HANDLE_EINTR(ioctl(fd.get(), SIOCSIFADDR, &ifr)) != 0) {
    int saved_errno = errno;
    PLOG(ERROR) << "Failed to set IPv4 address for interface " << kInterfaceName
                << " to " << AddressToString(ipv4_config.address());
    return grpc::Status(grpc::INTERNAL, string("failed to set IPv4 address: ") +
                                            strerror(saved_errno));
  }

  LOG(INFO) << "Set IPv4 address for interface " << kInterfaceName << " to "
            << AddressToString(ipv4_config.address());

  // Set the netmask.
  struct sockaddr_in* netmask =
      reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_netmask);
  netmask->sin_family = AF_INET;
  netmask->sin_addr.s_addr = static_cast<in_addr_t>(ipv4_config.netmask());

  if (HANDLE_EINTR(ioctl(fd.get(), SIOCSIFNETMASK, &ifr)) != 0) {
    int saved_errno = errno;
    PLOG(ERROR) << "Failed to set IPv4 netmask for interface " << kInterfaceName
                << " to " << AddressToString(ipv4_config.netmask());
    return grpc::Status(grpc::INTERNAL, string("failed to set IPv4 netmask: ") +
                                            strerror(saved_errno));
  }

  LOG(INFO) << "Set IPv4 netmask for interface " << kInterfaceName << " to "
            << AddressToString(ipv4_config.netmask());

  // Set the interface up and running.  This needs to happen before the kernel
  // will let us set the gateway.
  int ret = EnableInterface(fd.get(), kInterfaceName);
  if (ret) {
    return grpc::Status(
        grpc::INTERNAL,
        string("failed to enable network interface: ") + strerror(ret));
  }
  LOG(INFO) << "Set interface " << kInterfaceName << " up and running";

  // Bring up the loopback interface too.
  ret = EnableInterface(fd.get(), kLoopbackName);
  if (ret) {
    return grpc::Status(
        grpc::INTERNAL,
        string("failed to enable loopback interface") + strerror(ret));
  }

  // Set the gateway.
  struct rtentry route;
  memset(&route, 0, sizeof(route));

  struct sockaddr_in* gateway =
      reinterpret_cast<struct sockaddr_in*>(&route.rt_gateway);
  gateway->sin_family = AF_INET;
  gateway->sin_addr.s_addr = static_cast<in_addr_t>(ipv4_config.gateway());

  struct sockaddr_in* dst =
      reinterpret_cast<struct sockaddr_in*>(&route.rt_dst);
  dst->sin_family = AF_INET;
  dst->sin_addr.s_addr = INADDR_ANY;

  struct sockaddr_in* genmask =
      reinterpret_cast<struct sockaddr_in*>(&route.rt_genmask);
  genmask->sin_family = AF_INET;
  genmask->sin_addr.s_addr = INADDR_ANY;

  route.rt_flags = RTF_UP | RTF_GATEWAY;

  string gateway_str = AddressToString(ipv4_config.gateway());
  if (HANDLE_EINTR(ioctl(fd.get(), SIOCADDRT, &route)) != 0) {
    int saved_errno = errno;
    PLOG(ERROR) << "Failed to set default IPv4 gateway for interface "
                << kInterfaceName << " to " << gateway_str;
    return grpc::Status(grpc::INTERNAL, string("failed to set IPv4 gateway: ") +
                                            strerror(saved_errno));
  }

  LOG(INFO) << "Set default IPv4 gateway for interface " << kInterfaceName
            << " to " << gateway_str;

  return grpc::Status::OK;
}

grpc::Status ServiceImpl::Shutdown(grpc::ServerContext* ctx,
                                   const EmptyMessage* request,
                                   EmptyMessage* response) {
  LOG(INFO) << "Received shutdown request";

  if (maitred_is_pid1_) {
    init_->Shutdown();
    std::move(shutdown_cb_).Run();
    return grpc::Status::OK;
  }

  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;

  scoped_refptr<dbus::Bus> bus = new dbus::Bus(std::move(opts));
  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return grpc::Status(grpc::INTERNAL, "Failed to connect to system bus");
  }

  dbus::ObjectProxy* logind_service_proxy = bus->GetObjectProxy(
      kLogindServiceName, dbus::ObjectPath(kLogindServicePath));
  if (!logind_service_proxy) {
    LOG(ERROR) << "Failed to get D-Bus proxy for " << kLogindServiceName;
    return grpc::Status(
        grpc::INTERNAL,
        base::StrCat({"Failed to get D-Bus proxy for ", kLogindServiceName}));
  }

  // When running as a service, ask logind to shut down the system.
  dbus::MethodCall method_call(kLogindManagerInterface, "PowerOff");
  dbus::MessageWriter writer(&method_call);
  writer.AppendBool(false);  // interactive = false
  auto dbus_response = logind_service_proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response.has_value()) {
    return grpc::Status(
        grpc::INTERNAL,
        base::StrCat({"failed to send power off request to logind: ",
                      dbus_response.error().name(), ", ",
                      dbus_response.error().message()}));
  }

  return grpc::Status::OK;
}

grpc::Status ServiceImpl::LaunchProcess(
    grpc::ServerContext* ctx,
    const vm_tools::LaunchProcessRequest* request,
    vm_tools::LaunchProcessResponse* response) {
  LOG(INFO) << "Received request to launch process";
  if (!init_) {
    return grpc::Status(grpc::FAILED_PRECONDITION, "not running as init");
  }

  if (request->argv_size() <= 0) {
    return grpc::Status(grpc::INVALID_ARGUMENT, "missing argv");
  }

  if (request->respawn() && request->wait_for_exit()) {
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "respawn and wait_for_exit cannot both be true");
  }

  std::vector<string> argv(request->argv().begin(), request->argv().end());
  std::map<string, string> env;
  for (const auto& pair : request->env()) {
    env[pair.first] = pair.second;
  }

  Init::ProcessLaunchInfo launch_info;
  if (!init_->Spawn(std::move(argv), std::move(env), request->respawn(),
                    request->use_console(), request->wait_for_exit(),
                    &launch_info)) {
    return grpc::Status(grpc::INTERNAL, "failed to spawn process");
  }

  switch (launch_info.status) {
    case Init::ProcessStatus::UNKNOWN:
      LOG(WARNING) << "Child process has unknown status";

      response->set_status(vm_tools::UNKNOWN);
      break;
    case Init::ProcessStatus::EXITED:
      LOG(INFO) << "Requested process " << request->argv()[0] << " exited with "
                << "status " << launch_info.code;

      response->set_status(vm_tools::EXITED);
      response->set_code(launch_info.code);
      break;
    case Init::ProcessStatus::SIGNALED:
      LOG(INFO) << "Requested process " << request->argv()[0] << " killed by "
                << "signal " << launch_info.code;

      response->set_status(vm_tools::SIGNALED);
      response->set_code(launch_info.code);
      break;
    case Init::ProcessStatus::LAUNCHED:
      LOG(INFO) << "Launched process " << request->argv()[0];

      response->set_status(vm_tools::LAUNCHED);
      break;
    case Init::ProcessStatus::FAILED:
      LOG(ERROR) << "Failed to launch requested process";

      response->set_status(vm_tools::FAILED);
      break;
  }

  // Return OK no matter what because the RPC itself succeeded even if there
  // was an issue with launching the process.
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::Mount(grpc::ServerContext* ctx,
                                const MountRequest* request,
                                MountResponse* response) {
  LOG(INFO) << "Received mount request for " << request->target();

  // TODO(b/280685257): concierge shouldn't send requests to mount the external
  // disk. The code to do it should be removed once the relevant vms' uprevs
  // pass. Then, this workaround can be removed.
  if (request->target() == "/mnt/external/0") {
    return grpc::Status::OK;
  }

  const base::FilePath target_path = base::FilePath(request->target());
  if (request->create_target()) {
    // Create a mount point if it doesn't exist.
    if (!brillo::MkdirRecursively(target_path, 0755).is_valid()) {
      PLOG(ERROR) << "Failed to create " << request->target();
      return grpc::Status(grpc::INTERNAL,
                          base::StringPrintf("failed to create a directory: %s",
                                             request->target().c_str()));
    }
  }

  int ret = mount(request->source().c_str(), request->target().c_str(),
                  request->fstype().c_str(), request->mountflags(),
                  request->options().c_str());

  if (ret < 0 && errno == EINVAL && request->mkfs_if_needed()) {
    // When the source has an invalid superblock (e.g. not formatted), run
    // mkfs.btrfs and retry mount.

    LOG(INFO) << "Formatting" << request->source() << " as btrfs";

    Init::ProcessLaunchInfo launch_info;
    if (!init_->Spawn({"mkfs.btrfs", request->source().c_str()}, lxd_env_,
                      false /*respawn*/, false /*use_console*/,
                      true /*wait_for_exit*/, &launch_info)) {
      return grpc::Status(grpc::INTERNAL, "failed to spawn mkfs.btrfs");
    }
    if (launch_info.status != Init::ProcessStatus::EXITED) {
      return grpc::Status(grpc::INTERNAL, "mkfs.btrfs did not complete");
    }

    ret = mount(request->source().c_str(), request->target().c_str(),
                request->fstype().c_str(), request->mountflags(),
                request->options().c_str());
  }

  if (ret < 0) {
    response->set_error(errno);
    PLOG(ERROR) << "Failed to mount \"" << request->source() << "\" on \""
                << request->target() << "\"";
  } else {
    response->set_error(0);
    LOG(INFO) << "Mounted \"" << request->source() << "\" on \""
              << request->target() << "\"";
  }

  if (request->permissions() != 0) {
    ret = chmod(request->target().c_str(), request->permissions());

    if (ret < 0) {
      response->set_error(errno);
      PLOG(ERROR) << "Failed to change the mode of \"" << request->target()
                  << "\"";

      // Unmount the disk. Since this is cleanup, we ignore its return value.
      umount(request->target().c_str());
      return grpc::Status(grpc::INTERNAL, "failed to change the mode");
    }
  }

  return grpc::Status::OK;
}

grpc::Status ServiceImpl::ResetIPv6(grpc::ServerContext* ctx,
                                    const vm_tools::EmptyMessage* request,
                                    vm_tools::EmptyMessage* response) {
  // This method is deprecated, but should otherwise perform the same actions
  // as OnHostNetworkChanged.
  return OnHostNetworkChanged(ctx, request, response);
}

grpc::Status ServiceImpl::OnHostNetworkChanged(
    grpc::ServerContext* ctx,
    const vm_tools::EmptyMessage* request,
    vm_tools::EmptyMessage* response) {
  LOG(INFO) << "Received OnHostNetworkChanged request";
  string error;

  // Reset IPv6 to force SLAAC renegotiation.
  if (!SetSysctl(base::StringPrintf("/proc/sys/net/ipv6/conf/%s/disable_ipv6",
                                    kInterfaceName)
                     .c_str(),
                 "1", &error)) {
    return grpc::Status(grpc::INTERNAL, error + ", cannot disable ipv6");
  }
  if (!SetSysctl(base::StringPrintf("/proc/sys/net/ipv6/conf/%s/disable_ipv6",
                                    kInterfaceName)
                     .c_str(),
                 "0", &error)) {
    return grpc::Status(grpc::INTERNAL, error + ", cannot enable ipv6");
  }

  // Send SIGHUP to dnsmasq to flush caches.
  base::NamedProcessIterator iter("dnsmasq", nullptr);
  while (const base::ProcessEntry* entry = iter.NextProcessEntry()) {
    kill(entry->pid(), SIGHUP);
  }

  // TODO(http://crbug/1058730): Existing sockets should also be shut down.
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::ConfigureContainerGuest(
    grpc::ServerContext* ctx,
    const vm_tools::ConfigureContainerGuestRequest* request,
    vm_tools::EmptyMessage* response) {
  LOG(INFO) << "Received ConfigureContainerGuest request";
  Init::ProcessLaunchInfo launch_info;

  base::ScopedFD token_fd(
      HANDLE_EINTR(open(vm_tools::kGarconContainerTokenFile,
                        O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644)));
  if (!token_fd.is_valid()) {
    return grpc::Status(grpc::INTERNAL,
                        "failed to open container token for writing");
  }

  // Tell garcon what the token is.
  if (!base::WriteFileDescriptor(token_fd.get(),
                                 request->container_token().c_str())) {
    return grpc::Status(grpc::INTERNAL,
                        "failed to write container token to file");
  }
  LOG(INFO) << "Wrote container token to "
            << vm_tools::kGarconContainerTokenFile;

#if USE_VM_BOREALIS
  // Run garcon.
  if (!init_->Spawn({"/etc/init.d/cros-garcon", "daemon"}, {}, true /*respawn*/,
                    false /*use_console*/, false /*wait_for_exit*/,
                    &launch_info)) {
    return grpc::Status(grpc::INTERNAL, "failed to launch garcon");
  }
  LOG(INFO) << "Launched garcon";
#endif
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::StartTermina(grpc::ServerContext* ctx,
                                       const StartTerminaRequest* request,
                                       StartTerminaResponse* response) {
  LOG(INFO) << "Received StartTermina request";

  response->set_mount_result(StartTerminaResponse::UNKNOWN);

  if (!init_) {
    return grpc::Status(grpc::FAILED_PRECONDITION, "not running as init");
  }

  Init::ProcessLaunchInfo launch_info;
  stateful_device_ = request->stateful_device().empty()
                         ? "/dev/vdb"
                         : request->stateful_device();
  if (!init_->Spawn({"mkfs.btrfs", stateful_device_}, lxd_env_,
                    false /*respawn*/, false /*use_console*/,
                    true /*wait_for_exit*/, &launch_info)) {
    return grpc::Status(grpc::INTERNAL, "failed to spawn mkfs.btrfs");
  }
  if (launch_info.status != Init::ProcessStatus::EXITED) {
    return grpc::Status(grpc::INTERNAL, "mkfs.btrfs did not complete");
  }
  // mkfs.btrfs will fail if the disk is already formatted as btrfs.
  // Optimistically continue on - if the mount fails, then return an error.

  int ret = mount(stateful_device_.c_str(), stateful_mount_.value().c_str(),
                  "btrfs", 0, "user_subvol_rm_allowed,discard");
  if (ret != 0) {
    int saved_errno = errno;
    PLOG(ERROR) << "Failed to mount stateful disk";

    ret = mount(stateful_device_.c_str(), stateful_mount_.value().c_str(),
                "btrfs", 0, "user_subvol_rm_allowed,discard,usebackuproot");

    if (ret != 0) {
      int saved_errno_retry = errno;
      response->set_mount_result(StartTerminaResponse::FAILURE);
      return grpc::Status(grpc::INTERNAL,
                          string("failed to mount stateful(") +
                              stateful_device_ + "): " + strerror(saved_errno) +
                              ", " + strerror(saved_errno_retry));
    } else {
      response->set_mount_result(StartTerminaResponse::PARTIAL_DATA_LOSS);
    }
  } else {
    response->set_mount_result(StartTerminaResponse::SUCCESS);
  }

  // Register our crash reporter.
  if (!init_->Spawn({"/sbin/crash_reporter", "--init"}, {} /*env*/,
                    false /*respawn*/, true /*use_console*/,
                    true /*wait_for_exit*/, &launch_info)) {
    LOG(ERROR) << "Failed to spawn crash_reporter registration";
  } else if (launch_info.status != Init::ProcessStatus::EXITED ||
             launch_info.code != 0) {
    LOG(ERROR) << "Failed to register crash_reporter";
  }

  // Resize the stateful filesystem to fill the block device in case
  // the size was increased while the VM wasn't booted.
  if (!init_->Spawn(
          {"btrfs", "filesystem", "resize", "max", stateful_mount_.value()},
          lxd_env_, false /*respawn*/, false /*use_console*/,
          true /*wait_for_exit*/, &launch_info)) {
    return grpc::Status(grpc::INTERNAL, "failed to spawn btrfs resize");
  }
  // btrfs resize operation should not fail, but if it does, attempt to
  // continue anyway.
  if (launch_info.status != Init::ProcessStatus::EXITED) {
    PLOG(ERROR) << "btrfs resize did not complete";
  } else if (launch_info.code != 0) {
    PLOG(ERROR) << "btrfs resize returned non-zero";
  }

  int64_t free_bytes = base::SysInfo::AmountOfFreeDiskSpace(stateful_mount_);
  if (free_bytes >= 0) {
    response->set_free_bytes(free_bytes);
    response->set_free_bytes_has_value(true);
  }

  // TODO(davidriley): Replace this #if with StartBorealis.
#if !USE_VM_BOREALIS
  // Start lxcfs.
  if (!init_->Spawn({"lxcfs", "/var/lib/lxcfs"}, {} /*env*/, true /*respawn*/,
                    true /*use_console*/, false /*wait_for_exit*/,
                    &launch_info)) {
    return grpc::Status(grpc::INTERNAL, "failed to spawn lxcfs");
  }
  if (launch_info.status != Init::ProcessStatus::LAUNCHED) {
    return grpc::Status(grpc::INTERNAL, "lxcfs did not launch");
  }

  std::vector<std::string> tremplin_argv{"tremplin", "-lxd_subnet",
                                         request->lxd_ipv4_subnet()};
  if (!init_->Spawn(tremplin_argv, lxd_env_, true /*respawn*/,
                    true /*use_console*/, false /*wait_for_exit*/,
                    &launch_info)) {
    return grpc::Status(grpc::INTERNAL, "failed to spawn tremplin");
  }
  if (launch_info.status != Init::ProcessStatus::LAUNCHED) {
    return grpc::Status(grpc::INTERNAL, "tremplin did not launch");
  }

  if (!init_->Spawn({"ndproxyd", "eth0", "lxdbr0"}, lxd_env_, true /*respawn*/,
                    true /*use_console*/, false /*wait_for_exit*/,
                    &launch_info)) {
    LOG(WARNING) << "failed to spawn ndproxyd";
  } else if (launch_info.status != Init::ProcessStatus::LAUNCHED) {
    LOG(WARNING) << "ndproxyd did not launch";
  }

  if (!init_->Spawn({"mcastd", "eth0", "lxdbr0"}, lxd_env_, true /*respawn*/,
                    true /*use_console*/, false /*wait_for_exit*/,
                    &launch_info)) {
    LOG(WARNING) << "failed to spawn mcastd";
  } else if (launch_info.status != Init::ProcessStatus::LAUNCHED) {
    LOG(WARNING) << "mcastd did not launch";
  }
#endif

  return grpc::Status::OK;
}

void ServiceImpl::ResizeCommandExitCallback(Init::ProcessStatus status,
                                            int code) {
  base::AutoLock auto_lock(resize_state_.lock);
  LOG(INFO) << "Resize command completed";
  resize_state_.resize_in_progress = false;

  if (status == Init::ProcessStatus::EXITED) {
    LOG(INFO) << "btrfs filesystem resize exited with code " << code;
    if (code == 0) {
      // Resize was successful.
      resize_state_.current_size = resize_state_.target_size;
    }
  } else if (status == Init::ProcessStatus::SIGNALED) {
    LOG(INFO) << "btrfs filesystem resize was terminated by signal " << code;
  } else {
    LOG(ERROR) << "Unexpected exit status " << static_cast<int>(status);
  }
}

grpc::Status ServiceImpl::ResizeFilesystem(
    grpc::ServerContext* ctx,
    const ResizeFilesystemRequest* request,
    ResizeFilesystemResponse* response) {
  base::AutoLock auto_lock(resize_state_.lock);

  if (resize_state_.resize_in_progress) {
    LOG(INFO) << "Resize already in progress";
    response->set_status(ResizeFilesystemResponse::ALREADY_IN_PROGRESS);
    return grpc::Status::OK;
  }

#if USE_VM_BOREALIS
  // For borealis, the stateful device is hard-coded by init.
  stateful_device_ = "/dev/vda";
#else
  if (stateful_device_.empty()) {
    // Fall back to /dev/vdb mounted at / if StartTermina did not run (Baguette).
    stateful_device_ = "/dev/vdb";
    stateful_mount_ = base::FilePath("/");
  }
#endif

  base::ScopedFD stateful_fd(
      open(stateful_device_.c_str(), O_RDONLY | O_CLOEXEC));
  if (!stateful_fd.is_valid()) {
    return grpc::Status(grpc::INTERNAL, "unable to open mount point");
  }

  // The disk resize should be complete by the time this function is called
  // (when expanding), but it's possible that the guest kernel hasn't processed
  // the configuration change notification and updated the block device size
  // yet. This loop waits until the disk is at least as large as the target
  // filesystem size. If the disk resize does not finish in a reasonable amount
  // of time, fall through and attempt the btrfs resize anyway; it will fail if
  // the disk is still not a sufficient size.
  useconds_t retry_delay = 100000;  // 0.1 seconds
  int size_retries = 5;             // Retry up to 6 times (6.3 seconds).
  while (size_retries--) {
    uint64_t disk_bytes = 0;
    if (ioctl(stateful_fd.get(), BLKGETSIZE64, &disk_bytes) == 0 &&
        disk_bytes >= request->size()) {
      break;
    }
    usleep(retry_delay);
    retry_delay *= 2;
  }
  if (size_retries < 0) {
    LOG(WARNING) << "disk size did not match expected value";
  }

  Init::ProcessLaunchInfo launch_info;
  if (!init_->Spawn({"btrfs", "filesystem", "resize",
                     std::to_string(request->size()), stateful_mount_.value()},
                    lxd_env_, false /*respawn*/, true /*use_console*/,
                    false /*wait_for_exit*/, &launch_info,
                    base::BindOnce(&ServiceImpl::ResizeCommandExitCallback,
                                   base::Unretained(this)))) {
    return grpc::Status(grpc::INTERNAL, "failed to spawn btrfs resize");
  }

  if (launch_info.status != Init::ProcessStatus::LAUNCHED) {
    return grpc::Status(grpc::INTERNAL, "btrfs resize could not be launched");
  }

  resize_state_.resize_in_progress = true;
  resize_state_.target_size = request->size();

  response->set_status(ResizeFilesystemResponse::STARTED);
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::GetResizeStatus(
    grpc::ServerContext* ctx,
    const EmptyMessage* request,
    vm_tools::GetResizeStatusResponse* response) {
  base::AutoLock auto_lock(resize_state_.lock);
  response->set_resize_in_progress(resize_state_.resize_in_progress);
  response->set_current_size(resize_state_.current_size);
  response->set_target_size(resize_state_.target_size);
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::GetResizeBounds(
    grpc::ServerContext* ctx,
    const EmptyMessage* request,
    vm_tools::GetResizeBoundsResponse* response) {
  Init::ProcessLaunchInfo launch_info;
  if (!init_->Spawn({"btrfs", "inspect-internal", "min-dev-size",
                     stateful_mount_.value()},
                    lxd_env_, false /*respawn*/, false /*use_console*/,
                    true /*wait_for_exit*/, &launch_info)) {
    LOG(ERROR) << "btrfs inspect-internal min-dev-size failed: "
               << launch_info.output;
    return grpc::Status(grpc::INTERNAL,
                        "btrfs inspect-internal min-dev-size failed");
  }

  std::string& btrfs_out = launch_info.output;

  // btrfs inspect-internal min-dev-size returns a string like:
  // "9701425152 bytes (9.04GiB)"
  // Extract the first space-separated word and parse it as a 64-bit integer.
  size_t space_pos = btrfs_out.find_first_of(' ');
  if (space_pos == std::string::npos) {
    LOG(ERROR) << "failed to parse btrfs output (no space found): "
               << btrfs_out;
    return grpc::Status(grpc::INTERNAL, "failed to parse btrfs output");
  }

  std::string min_size_str = btrfs_out.substr(0, space_pos);
  uint64_t min_size = 0;
  if (!base::StringToUint64(min_size_str, &min_size)) {
    LOG(ERROR) << "failed to parse btrfs output as uint64: " << min_size_str;
    return grpc::Status(grpc::INTERNAL, "failed to parse btrfs output");
  }

  response->set_minimum_size(min_size);
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::GetAvailableSpace(
    grpc::ServerContext* ctx,
    const EmptyMessage* request,
    vm_tools::GetAvailableSpaceResponse* response) {
  response->set_available_space(
      base::SysInfo::AmountOfFreeDiskSpace(stateful_mount_));
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::Mount9P(grpc::ServerContext* ctx,
                                  const Mount9PRequest* request,
                                  MountResponse* response) {
  LOG(INFO) << "Received request to mount 9P file system";
  base::ScopedFD server(socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!server.is_valid()) {
    response->set_error(errno);
    PLOG(ERROR) << "Failed to create vsock socket";
    return grpc::Status(grpc::INTERNAL, "unable to create vsock socket");
  }

  struct sockaddr_vm svm = {
      .svm_family = AF_VSOCK,
      .svm_port = static_cast<unsigned int>(request->port()),
      .svm_cid = VMADDR_CID_HOST,
  };
  if (connect(server.get(), reinterpret_cast<struct sockaddr*>(&svm),
              sizeof(svm)) != 0) {
    response->set_error(errno);
    PLOG(ERROR) << "Unable to connect to server";
    return grpc::Status(grpc::INTERNAL, "unable to connect to server");
  }

  const base::FilePath target_path = base::FilePath(request->target());
  // Create a mount point if it doesn't exist.
  if (!brillo::MkdirRecursively(target_path, 0755).is_valid()) {
    PLOG(ERROR) << "Failed to create " << request->target();
    return grpc::Status(grpc::INTERNAL,
                        base::StringPrintf("failed to create a directory: %s",
                                           request->target().c_str()));
  }

  // Do the mount.
  string data = base::StringPrintf(
      "trans=fd,rfdno=%d,wfdno=%d,cache=none,access=any,version=9p2000.L",
      server.get(), server.get());
  if (mount("9p", request->target().c_str(), "9p", MS_NOSUID | MS_NODEV,
            data.c_str()) != 0) {
    response->set_error(errno);
    PLOG(ERROR) << "Failed to mount 9p file system";
    return grpc::Status(grpc::INTERNAL, "failed to mount file system");
  }

  LOG(INFO) << "Mounted 9P file system on " << request->target();
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::SetResolvConfig(grpc::ServerContext* ctx,
                                          const SetResolvConfigRequest* request,
                                          EmptyMessage* response) {
  LOG(INFO) << "Received request to update VM resolv.conf";
  const vm_tools::ResolvConfig& resolv_config = request->resolv_config();

  std::vector<string> nameservers(resolv_config.nameservers().begin(),
                                  resolv_config.nameservers().end());
  if (nameservers.empty()) {
    LOG(WARNING) << "Host sent empty nameservers list; using default";
    nameservers = kDefaultNameservers;
  }

  std::vector<string> search_domains(resolv_config.search_domains().begin(),
                                     resolv_config.search_domains().end());
  string error;
  if (!WriteResolvConf(nameservers, search_domains, &error)) {
    return grpc::Status(grpc::INTERNAL, error);
  }

  return grpc::Status::OK;
}

grpc::Status ServiceImpl::SetTime(grpc::ServerContext* ctx,
                                  const vm_tools::SetTimeRequest* request,
                                  EmptyMessage* response) {
  struct timeval new_time;
  new_time.tv_sec = request->time().seconds();
  new_time.tv_usec = request->time().nanos() / 1000;

  LOG(INFO) << "Recieved request to set time to " << new_time.tv_sec << "s, "
            << new_time.tv_usec << "us";

  if (new_time.tv_sec == 0) {
    LOG(ERROR) << "Ignored attempt to set time to the epoch";

    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "ignored attempt to set time to the epoch");
  }

  if (settimeofday(&new_time, /*tz=*/nullptr) < 0) {
    string error = strerror(errno);
    LOG(ERROR) << "Failed to set time: " << error;
    return grpc::Status(
        grpc::INTERNAL,
        base::StringPrintf("failed to set time: %s", error.c_str()));
  }

  LOG(INFO) << "Successfully set time.";
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::SetTimezoneSymlink(const base::FilePath& zoneinfo) {
  // Create a symlink pointing to the new zoneinfo at /etc/localtime.new.
  base::FilePath temp_symlink_path = localtime_file_path_.AddExtension(".new");
  if (!base::CreateSymbolicLink(zoneinfo, temp_symlink_path)) {
    return grpc::Status(grpc::INTERNAL, "failed to create symlink");
  }

  // Atomically replace /etc/localtime with /etc/localtime.new.
  base::File::Error replace_error;
  if (!base::ReplaceFile(temp_symlink_path, localtime_file_path_,
                         &replace_error)) {
    LOG(ERROR) << "Failed to replace " << localtime_file_path_ << " with "
               << temp_symlink_path << ": " << replace_error;
    brillo::DeleteFile(temp_symlink_path);
    return grpc::Status(grpc::INTERNAL, "failed to replace symlink");
  }

  return grpc::Status::OK;
}

// TODO(b/237960004): deprecate bind-mount implementation once Steam supports
// chained symlinks.
grpc::Status ServiceImpl::SetTimezoneBindMount(const std::string& bind_source) {
  LOG(INFO) << "Re-mounting " << localtime_file_path_;
  umount(localtime_file_path_.value().c_str());
  auto result = mount(bind_source.c_str(), localtime_file_path_.value().c_str(),
                      NULL, MS_BIND, NULL);
  if (result < 0) {
    LOG(ERROR) << "Failed to create bind-mount: " << result;
    return grpc::Status(grpc::INTERNAL, "failed to create bind-mount");
  }
  return grpc::Status::OK;
}

grpc::Status ServiceImpl::SetTimezone(
    grpc::ServerContext* ctx,
    const vm_tools::SetTimezoneRequest* request,
    vm_tools::EmptyMessage* response) {
  if (request->timezone_name().empty()) {
    return grpc::Status(grpc::INTERNAL, "timezone cannot be empty");
  }

  LOG(INFO) << "Setting timezone to " << request->timezone_name();

  base::FilePath zoneinfo_file =
      zoneinfo_file_path_.Append(request->timezone_name());

  // TODO(b/237963590): Add support to update timezone in VM using
  // tzif_parser data if zoneinfo file is missing or outdated.
  if (!base::PathExists(zoneinfo_file)) {
    LOG(ERROR) << "Zoneinfo file does not exist in VM, unable to set timezone";
    return grpc::Status(grpc::INTERNAL, "zone info file does not exist");
  }

  if (request->use_bind_mount()) {
    return SetTimezoneBindMount(zoneinfo_file.value());
  }
  return SetTimezoneSymlink(zoneinfo_file);
}

grpc::Status ServiceImpl::GetKernelVersion(
    grpc::ServerContext* ctx,
    const EmptyMessage* request,
    vm_tools::GetKernelVersionResponse* response) {
  LOG(INFO) << "Received request to get kernel version information.";

  struct utsname buffer;
  if (uname(&buffer) < 0) {
    const std::string error_message = base::StringPrintf(
        "Failed to retrieve kernel version: %s", strerror(errno));
    LOG(ERROR) << error_message;
    return grpc::Status(grpc::INTERNAL, error_message);
  }

  response->set_kernel_release(buffer.release);
  response->set_kernel_version(buffer.version);

  return grpc::Status::OK;
}

grpc::Status ServiceImpl::PrepareToSuspend(grpc::ServerContext* ctx,
                                           const EmptyMessage* request,
                                           EmptyMessage* response) {
  LOG(INFO) << "Received request to prepare to suspend.";

  // Commit filesystem caches to disks. This is important especially when a disk
  // is on external storage which can be unplugged while the device is asleep.
  sync();

  return grpc::Status::OK;
}

grpc::Status ServiceImpl::UpdateStorageBalloon(
    grpc::ServerContext* ctx,
    const vm_tools::UpdateStorageBalloonRequest* request,
    vm_tools::UpdateStorageBalloonResponse* response) {
  base::AutoLock auto_lock(balloon_update_lock_);
  response->set_result(vm_tools::UpdateStorageBalloonResult::SUCCESS);
  if (!balloon_) {
    balloon_ = brillo::StorageBalloon::GenerateStorageBalloon(stateful_mount_);
  }
  if (!balloon_ ||
      !balloon_->Adjust(std::max(
          int64_t(request->free_space_bytes() - (1 * kGiB)), int64_t(0)))) {
    LOG(ERROR) << "Failed to adjust balloon, free_space_bytes:"
               << request->free_space_bytes() << " state:" << request->state();
    response->set_result(
        vm_tools::UpdateStorageBalloonResult::BALLOON_INFLATE_FAILED);
  }

  return grpc::Status::OK;
}

grpc::Status ServiceImpl::SetUserLinger(const uid_t uid) {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;

  scoped_refptr<dbus::Bus> bus = new dbus::Bus(std::move(opts));
  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return grpc::Status(grpc::INTERNAL, "Failed to connect to system bus");
  }

  dbus::ObjectProxy* logind_service_proxy = bus->GetObjectProxy(
      kLogindServiceName, dbus::ObjectPath(kLogindServicePath));
  if (!logind_service_proxy) {
    LOG(ERROR) << "Failed to get D-Bus proxy for " << kLogindServiceName;
    return grpc::Status(
        grpc::INTERNAL,
        base::StrCat({"Failed to get D-Bus proxy for ", kLogindServiceName}));
  }

  dbus::MethodCall method_call(kLogindManagerInterface, "SetUserLinger");
  dbus::MessageWriter writer(&method_call);
  writer.AppendUint32(uid);  // uid
  writer.AppendBool(true);   // enable
  writer.AppendBool(false);  // interactive

  auto dbus_response = logind_service_proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response.has_value()) {
    LOG(ERROR) << "Failed to send SetUserLinger request to logind.";
    return grpc::Status(
        grpc::INTERNAL,
        base::StrCat({"Failed to send SetUserLinger request to logind: ",
                      dbus_response.error().name(), ", ",
                      dbus_response.error().message()}));
  }

  return grpc::Status::OK;
}

grpc::Status ServiceImpl::SetUpUser(grpc::ServerContext* ctx,
                                    const vm_tools::SetUpUserRequest* request,
                                    vm_tools::SetUpUserResponse* response) {
  LOG(INFO) << "Received request to setup a new user.";

  if (request->username().empty()) {
    LOG(ERROR) << "Cannot setup a user with <empty> username";
    return grpc::Status(grpc::INVALID_ARGUMENT, "username is empty");
  }
  if (!RE2::FullMatch(request->username(), *kUsernameRegexp)) {
    LOG(ERROR) << "Invalid username specified: " << request->username();
    return grpc::Status(grpc::INVALID_ARGUMENT, "username is invalid");
  }
  response->set_username(request->username());
  response->set_success(false);

  uid_t uid = request->uid();
  if (!request->has_uid()) {
    LOG(WARNING) << "Unspecified uid for new user; defaulting to 1000";
    uid = 1000;
  } else if (uid == 0) {
    LOG(ERROR) << "Cannot setup a new user with root uid: 0";
    return grpc::Status(grpc::INVALID_ARGUMENT, "new uid cannot be 0");
  }

  struct passwd* pwd = getpwuid(uid);
  if ((pwd != NULL) && (request->username() != pwd->pw_name)) {
    response->set_failure_reason("Another user with uid " +
                                 std::to_string(uid) + " already exists");
    response->set_username(pwd->pw_name);
    LOG(ERROR) << response->failure_reason() << ": " << pwd->pw_name;
    return grpc::Status::OK;
  }

  bool user_exists = false;
  if ((pwd = getpwnam(request->username().c_str())) != NULL) {
    user_exists = true;
    if (pwd->pw_uid != uid) {
      response->set_failure_reason("User exists, but with a different uid: " +
                                   std::to_string(pwd->pw_uid));
      LOG(ERROR) << response->failure_reason();
      return grpc::Status::OK;
    } else {
      LOG(INFO) << "User " << response->username() << " already exists";
    }
  }

  // Although the useradd/usermod invocation below will check for existence of
  // groups, our `getgrnam` check below also serves as a sanitization step to
  // avoid passing unsafe strings as part of the command line.
  std::vector<std::string> nonexistent_group_names;
  absl::c_copy_if(
      request->group_names(), std::back_inserter(nonexistent_group_names),
      [](const std::string& gname) { return getgrnam(gname.c_str()) == NULL; });
  if (!nonexistent_group_names.empty()) {
    LOG(ERROR) << "Nonexistent group names specified: "
               << absl::StrJoin(nonexistent_group_names, ",");
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "one or more specified groups do not exist");
  }

  // All of the user-specified strings (the username and the group names) have
  // been sanitized at this point.
  std::vector<string> argv;
  if (!user_exists) {
    argv = {"/usr/sbin/useradd", "--uid",   std::to_string(uid),
            "--create-home",     "--shell", "/bin/bash"};
    if (!request->group_names().empty()) {
      argv.push_back("--groups");
      argv.push_back(absl::StrJoin(request->group_names(), ","));
    }
    argv.push_back(request->username());
  } else if (!request->group_names().empty()) {
    argv = {"/usr/sbin/usermod", "--append", "--groups",
            absl::StrJoin(request->group_names(), ","), request->username()};
  }

  // If the user already exists and we have no more groups to append,
  // then there's nothing to do.
  if (!argv.empty()) {
    Init::ProcessLaunchInfo launch_info;
    auto res = init_->Spawn(argv, {}, /*respawn=*/false, /*use_console=*/false,
                            /*wait_for_exit=*/true, &launch_info);
    if (!res || launch_info.status != Init::ProcessStatus::EXITED) {
      LOG(ERROR) << "Failed to invoke " << argv[0] << ": "
                 << launch_info.output;
      return grpc::Status(grpc::INTERNAL, argv[0] + " invocation failed");
    } else if (launch_info.code != 0) {
      response->set_failure_reason("Could not create new user");
      LOG(ERROR) << response->failure_reason();
      return grpc::Status::OK;
    }
  }

  // Enable linger to keep systemd user services running even after the user's
  // session has terminated.
  auto status = SetUserLinger(uid);
  if (!status.ok()) {
    return status;
  }

  response->set_success(true);
  return grpc::Status::OK;
}

}  // namespace maitred
}  // namespace vm_tools
