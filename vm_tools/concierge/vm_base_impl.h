// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VM_BASE_IMPL_H_
#define VM_TOOLS_CONCIERGE_VM_BASE_IMPL_H_
#include <stdint.h>
#include <unistd.h>

#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <spaced/proto_bindings/spaced.pb.h>
#include <vm_concierge/concierge_service.pb.h>

#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/balloon_policy.h"
#include "vm_tools/concierge/seneschal_server_proxy.h"

namespace patchpanel {
class Client;
}

namespace vm_tools::concierge {

// See VmBaseImpl.Info.vm_memory_id
typedef uint32_t VmMemoryId;

// A base class implementing common features that are shared with ArcVm,
// PluginVm and TerminaVm
class VmBaseImpl {
 public:
  struct Config {
    std::unique_ptr<patchpanel::Client> network_client;
    uint32_t vsock_cid{0};
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy;
    std::string cros_vm_socket{};
    base::FilePath runtime_dir;
  };
  explicit VmBaseImpl(Config config);

  VmBaseImpl(const VmBaseImpl&) = delete;
  VmBaseImpl& operator=(const VmBaseImpl&) = delete;
  virtual ~VmBaseImpl();

  // The pid of the child process.
  pid_t pid() const { return process_.pid(); }

  // The current status of the VM.
  enum class Status {
    STARTING,
    RUNNING,
    STOPPED,
  };

  // The types of stop sequences.
  enum class StopType {
    GRACEFUL,
    FORCEFUL,
  };

  // Possible results of a stop sequence.
  enum class StopResult {
    FAILURE,
    STOPPING,
    SUCCESS,
  };

  // Information about a virtual machine.
  struct Info {
    // The IPv4 address in network-byte order.
    uint32_t ipv4_address;

    // The pid of the main crosvm process for the VM.
    pid_t pid;

    // The vsock context id for the VM, if one exists.  Must be set to 0 if
    // there is no vsock context id.
    uint32_t cid;

    // ID for identifying a VM in the context of managing memory. This field is
    // valid for all VMs. On non-manaTEE systems, this is set by concierge. On
    // manaTEE, it is specified by the manatee memory service, and it specifies
    // the balloon control socket that this VM's crosvm instance should connect
    // to - /run/mms_control_%d.sock.
    VmMemoryId vm_memory_id;

    // The handle for the 9P server managed by seneschal on behalf of this VM
    // if one exists, 0 otherwise.
    uint32_t seneschal_server_handle;

    // Token assigned to the VM when registering with permission service.
    // Used to identify the VM to service providers and fetching set of
    // permissions granted to the VM.
    std::string permission_token;

    // The current status of the VM.
    Status status;

    // Type of the VM.
    apps::VmType type;

    // Whether the VM is using storage ballooning.
    bool storage_ballooning;
  };

  // Asynchronously stop the VM. Runs |callback| with the success value.
  // If the VM is currently stopping, |callback| is immediately run with false.
  // VM implementations supply a list of steps to perform to stop the VM.
  // Depending on the StopType, these steps can include both graceful and
  // forceful methods. After each step, a check is performed to see if the VM is
  // still alive with a timeout specified by the VM implementation. If the
  // timeout is exceeded, the next step in the Stop sequence is executed. It is
  // up to the VM implementation to make sure the worst case stop time for an
  // unresponsive VM is reasonable and that the individual steps do not block.
  void PerformStopSequence(StopType type,
                           base::OnceCallback<void(StopResult)> callback);

  using SwapVmCallback = base::OnceCallback<void(SwapVmResponse response)>;
  using AggressiveBalloonCallback =
      base::OnceCallback<void(AggressiveBalloonResponse response)>;

  // Suspends the VM.
  void Suspend() {
    HandleSuspendImminent();
    suspended_ = true;
  }

  // Resumes the VM.
  void Resume() {
    HandleSuspendDone();
    suspended_ = false;
  }

  bool IsSuspended() const { return suspended_; }

  // Returns true if the VM is currently performing its stop sequence.
  bool IsStopping() { return !!stop_complete_callback_; }

  // Information about the VM.
  virtual Info GetInfo() const = 0;

  // Returns balloon stats info retrieved from virtio-balloon device.
  virtual std::optional<BalloonStats> GetBalloonStats(
      std::optional<base::TimeDelta> timeout);

  // Returns guest working set info retrieved from virtio-balloon device.
  virtual std::optional<BalloonWorkingSet> GetBalloonWorkingSet();

  // Resize the balloon size.
  virtual bool SetBalloonSize(int64_t byte_size);

  // Set the working set config.
  virtual bool SetBalloonWorkingSetConfig(const BalloonWSRConfigFfi* config);

  // Get the virtio_balloon sizing policy for this VM.
  virtual const std::unique_ptr<BalloonPolicyInterface>& GetBalloonPolicy(
      const MemoryMargins& margins, const std::string& vm);

  // Attach an usb device at host bus:addr, with vid, pid and an opened fd.
  virtual bool AttachUsbDevice(uint8_t bus,
                               uint8_t addr,
                               uint16_t vid,
                               uint16_t pid,
                               int fd,
                               uint8_t* out_port);

  // Detach the usb device at guest port.
  virtual bool DetachUsbDevice(uint8_t port);

  // List all usb devices attached to guest.
  virtual bool ListUsbDevice(std::vector<UsbDeviceEntry>* devices);

  // Returns true if this VM depends on external signals for suspend and resume.
  // The D-Bus suspend/resume messages from powerd, SuspendImminent and
  // SuspendDone will not be propagated to this VM. Otherwise,
  // HandleSuspendImminent and HandleSuspendDone will be invoked when these
  // messages received.
  virtual bool UsesExternalSuspendSignals() { return false; }

  // Update resolv.conf data.
  virtual bool SetResolvConfig(
      const std::vector<std::string>& nameservers,
      const std::vector<std::string>& search_domains) = 0;

  // Perform necessary cleanup when host network changes.
  virtual void HostNetworkChanged() {}

  // Set the guest time to the current time as given by gettimeofday.
  virtual bool SetTime(std::string* failure_reason) = 0;

  // Set the guest timezone
  virtual bool SetTimezone(const std::string& timezone,
                           std::string* out_error) = 0;

  // Get enterprise reporting information. Also sets the
  // response fields for success and failure_reason.
  virtual bool GetVmEnterpriseReportingInfo(
      GetVmEnterpriseReportingInfoResponse* response) = 0;

  // Notes that TremplinStartedSignal has been received for the VM.
  virtual void SetTremplinStarted() = 0;

  // Notes that guest agent is running in the VM.
  virtual void VmToolsStateChanged(bool running) = 0;

  // Initiate a disk resize operation for the VM.
  // |new_size| is the requested size in bytes.
  virtual vm_tools::concierge::DiskImageStatus ResizeDisk(
      uint64_t new_size, std::string* failure_reason) = 0;

  // Get the status of the most recent ResizeDisk operation.
  virtual vm_tools::concierge::DiskImageStatus GetDiskResizeStatus(
      std::string* failure_reason) = 0;

  // Get the smallest valid resize parameter for this disk,
  // or 0 for unknown.
  virtual uint64_t GetMinDiskSize() { return 0; }

  // Get the space that is available/unallocated on the disk,
  // or 0 for unknown.
  virtual uint64_t GetAvailableDiskSpace() { return 0; }

  // Makes RT vCPU for the VM.
  virtual void MakeRtVcpu();

  virtual void HandleSwapVmRequest(const SwapVmRequest& request,
                                   SwapVmCallback callback);

  // Inflate balloon until perceptible processes are tried to kill.
  virtual void InflateAggressiveBalloon(AggressiveBalloonCallback callback);

  // Stop inflating aggressive balloon.
  virtual void StopAggressiveBalloon(AggressiveBalloonResponse& response);

  // Handle the low disk notification from spaced.
  virtual void HandleStatefulUpdate(
      const spaced::StatefulDiskSpaceUpdate update) = 0;

  const std::string& GetVmSocketPath() const;

  // How long to wait before timing out on child process exits.
  static constexpr base::TimeDelta kChildExitTimeout = base::Seconds(10);

 protected:
  // Adjusts the amount of CPU the VM processes are allowed to use.
  static bool SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state,
                                  const char* cpu_cgroup);

  static void RunFailureAggressiveBalloonCallback(
      AggressiveBalloonCallback callback, std::string failure_reason);

  // Starts |process_| with |args|. Returns true iff started successfully.
  bool StartProcess(base::StringPairs args);

  // Attempts to stop the VM via the crosvm control socket.
  void StopViaCrosvm(base::OnceClosure callback);

  // Suspends this VM
  // Returns true on success, false otherwise
  bool SuspendCrosvm() const;

  // Resumes this VM
  // Returns true on success, false otherwise
  bool ResumeCrosvm() const;

  // The 9p server managed by seneschal that provides access to shared files for
  // this VM. Returns 0 if there is no seneschal server associated with this
  // VM.
  uint32_t seneschal_server_handle() const;

  // A stop step performs a specific part of the stopping process. It runs
  // the supplied callback upon completion. The corresponding timeout is how
  // long to wait for the VM process to exit after running the step.
  struct StopStep {
    base::OnceCallback<void(base::OnceClosure)> task;
    base::TimeDelta exit_timeout;
  };

  // VM implementations supply one or more steps that must be called in order to
  // stop the VM. After every step, a check with a specified timeout is
  // performed to see if the VM is alive. If it is still alive, the next step
  // will be called.
  // Depending on the stop type a VM may have a different sequence of actions
  // to take to shut down.
  virtual std::vector<StopStep> GetStopSteps(StopType type) = 0;

  // Attempts to directly kill the VM process with the supplied signal then runs
  // the supplied callback.
  void KillVmProcess(int signal, base::OnceClosure callback);

  // DBus client for the networking service.
  std::unique_ptr<patchpanel::Client> network_client_;

  // Runtime directory for this VM.
  // TODO(abhishekbh): Try to move this to private.
  base::ScopedTempDir runtime_dir_;

  // Handle to the VM process.
  brillo::ProcessImpl process_;

  // Proxy to the server providing shared directory access for this VM.
  std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy_;

  // Virtual socket context id to be used when communicating with this VM.
  uint32_t vsock_cid_ = 0;

  // Balloon policy with its state.
  std::unique_ptr<BalloonPolicyInterface> balloon_policy_;

 private:
  // Performs the necessary steps to stop the VM.
  void PerformStopSequenceInternal(StopType type,
                                   std::vector<StopStep>::iterator next_step);

  // Checks for the VM's exit until deadline is reached.
  // Runs OnStopSequenceComplete(true) if the VM exits, or timeout_callback
  // if the VM fails to exit before the timeout.
  void CheckForExit(base::TimeTicks deadline,
                    base::OnceClosure timeout_callback);

  // Called once the VM stop sequence is finished.
  void OnStopSequenceComplete(StopResult result);

  // Returns true if the VM process is running. False otherwise.
  bool IsRunning();

  // Handle the device going to suspend.
  virtual void HandleSuspendImminent() = 0;

  // Handle the device resuming from a suspend.
  virtual void HandleSuspendDone() = 0;

  // The socket that communicates directly with crosvm to change VM
  // configuration.
  const std::string control_socket_path_;

  // Whether the VM is currently suspended.
  bool suspended_ = false;

  // Stores the callback to be run upon the stop sequence finishing.
  base::OnceCallback<void(StopResult)> stop_complete_callback_;

  // The ordered list of steps that must be run to stop the VM.
  std::vector<StopStep> stop_steps_;

  // This should be the last member of the class.
  base::WeakPtrFactory<VmBaseImpl> weak_ptr_factory_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VM_BASE_IMPL_H_
