// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_PLUGINS_H_
#define SECAGENTD_PLUGINS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "attestation/proto_bindings/interface.pb.h"
#include "attestation-client/attestation/dbus-proxies.h"
#include "base/memory/scoped_refptr.h"
#include "base/timer/timer.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/device_user.h"
#include "secagentd/message_sender.h"
#include "secagentd/metrics_sender.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/process_cache.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "tpm_manager/proto_bindings/tpm_manager.pb.h"
#include "tpm_manager-client/tpm_manager/dbus-proxies.h"

namespace secagentd {

namespace testing {
class AgentPluginTestFixture;
class ProcessPluginTestFixture;
}  // namespace testing

class PluginInterface {
 public:
  // Activate the plugin, must be idempotent.
  virtual absl::Status Activate() = 0;
  // Deactivate the plugin, must be idempotent.
  virtual absl::Status Deactivate() = 0;
  // Is the plugin currently activated?
  virtual bool IsActive() const = 0;
  virtual std::string GetName() const = 0;
  virtual ~PluginInterface() = default;
};

class ProcessPlugin : public PluginInterface {
 public:
  ProcessPlugin(
      scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache,
      scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
      scoped_refptr<DeviceUserInterface> device_user,
      uint32_t batch_interval_s);
  // Load, verify and attach the process BPF applications.
  absl::Status Activate() override;
  absl::Status Deactivate() override;
  bool IsActive() const override;
  std::string GetName() const override;

  // Handles an individual incoming Process BPF event.
  void HandleRingBufferEvent(const bpf::cros_event& bpf_event);
  // Requests immediate event consumption from BPF.
  void HandleBpfRingBufferReadReady() const;

 private:
  friend class testing::ProcessPluginTestFixture;

  using BatchSenderType =
      BatchSenderInterface<std::string,
                           cros_xdr::reporting::XdrProcessEvent,
                           cros_xdr::reporting::ProcessEventAtomicVariant>;

  // Immediately enqueues the given process event to ERP. This method will copy
  // the given message into one of the deprecated unbatched fields in
  // XdrProcessEvent.
  void DeprecatedSendImmediate(
      std::unique_ptr<cros_xdr::reporting::ProcessEventAtomicVariant>
          atomic_event);
  // Pushes the given process event into the next outgoing batch.
  void EnqueueBatchedEvent(
      std::unique_ptr<cros_xdr::reporting::ProcessEventAtomicVariant>
          atomic_event);
  // Converts the BPF process start event into a XDR process exec protobuf.
  std::unique_ptr<cros_xdr::reporting::ProcessExecEvent> MakeExecEvent(
      const secagentd::bpf::cros_process_start& process_start);
  std::unique_ptr<cros_xdr::reporting::ProcessTerminateEvent>
  // Converts the BPF process exit event into a XDR process terminate protobuf.
  MakeTerminateEvent(const secagentd::bpf::cros_process_exit& process_exit);
  // Inject the given (mock) BatchSender object for unit testing.
  void SetBatchSenderForTesting(std::unique_ptr<BatchSenderType> given) {
    batch_sender_ = std::move(given);
  }
  // This is static because it must be accessible to a C style function.
  static struct BpfCallbacks callbacks_;
  base::WeakPtrFactory<ProcessPlugin> weak_ptr_factory_;
  scoped_refptr<MessageSenderInterface> message_sender_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker_;
  scoped_refptr<DeviceUserInterface> device_user_;
  scoped_refptr<BpfSkeletonFactoryInterface> factory_;
  std::unique_ptr<BpfSkeletonInterface> skeleton_wrapper_;
  std::unique_ptr<BatchSenderType> batch_sender_;
};

class AgentPlugin : public PluginInterface {
 public:
  explicit AgentPlugin(scoped_refptr<MessageSenderInterface> message_sender,
                       scoped_refptr<DeviceUserInterface> device_user,
                       std::unique_ptr<org::chromium::AttestationProxyInterface>
                           attestation_proxy,
                       std::unique_ptr<org::chromium::TpmManagerProxyInterface>
                           tpm_manager_proxy,
                       base::OnceCallback<void()> cb,
                       uint32_t heartbeat_timer);

  // Initialize Agent proto and starts agent heartbeat events.
  absl::Status Activate() override;
  absl::Status Deactivate() override;
  std::string GetName() const override;
  bool IsActive() const override { return is_active_; }

 private:
  friend class testing::AgentPluginTestFixture;

  // Starts filling in the tcb fields of the agent proto and initializes async
  // timers that wait for tpm_manager and attestation to be ready. When services
  // are ready GetCrosSecureBootInformation() and GetTpmInformation()
  // will be called to fill remaining fields.
  void StartInitializingAgentProto();
  // Delayed function that will be called when attestation is ready. Fills the
  // boot information in the agent proto if Cros Secure boot is used.
  void GetCrosSecureBootInformation(bool available);
  // Delayed function that will be called when tpm_manager is ready. Fills the
  // tpm information in the agent proto.
  void GetTpmInformation(bool available);
  // Fills the boot information in the agent proto if Uefi Secure boot is used.
  // Note: Only for flex machines.
  void GetUefiSecureBootInformation(const base::FilePath& boot_params_filepath);
  // Sends the agent start event. Uses the StartEventStatusCallback() to handle
  // the status of the message.
  void SendAgentStartEvent();
  // Sends an agent heartbeat event every 5 minutes.
  void SendAgentHeartbeatEvent();
  // Checks the message status of the agent start event. If the message is
  // successfully sent it calls the daemon callback to run the remaining
  // plugins. If the message fails to send it will retry sending the message
  // every 3 seconds.
  void StartEventStatusCallback(reporting::Status status);

  base::RepeatingTimer agent_heartbeat_timer_;
  cros_xdr::reporting::TcbAttributes tcb_attributes_;
  base::WeakPtrFactory<AgentPlugin> weak_ptr_factory_;
  scoped_refptr<MessageSenderInterface> message_sender_;
  scoped_refptr<DeviceUserInterface> device_user_;
  std::unique_ptr<org::chromium::AttestationProxyInterface> attestation_proxy_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_proxy_;
  base::OnceCallback<void()> daemon_cb_;
  base::Lock tcb_attributes_lock_;
  base::TimeDelta heartbeat_timer_ = base::Minutes(5);
  metrics::CrosBootmode cros_bootmode_metric_ =
      metrics::CrosBootmode::kValueNotSet;
  metrics::UefiBootmode uefi_bootmode_metric_ = metrics::UefiBootmode::kSuccess;
  metrics::Tpm tpm_metric_ = metrics::Tpm::kValueNotSet;
  bool is_active_{false};
};

class PluginFactoryInterface {
 public:
  virtual std::unique_ptr<PluginInterface> Create(
      Types::Plugin type,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache,
      scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
      scoped_refptr<DeviceUserInterface> device_user,
      uint32_t batch_interval_s) = 0;
  virtual std::unique_ptr<PluginInterface> CreateAgentPlugin(
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<DeviceUserInterface> device_user,
      std::unique_ptr<org::chromium::AttestationProxyInterface>
          attestation_proxy,
      std::unique_ptr<org::chromium::TpmManagerProxyInterface>
          tpm_manager_proxy,
      base::OnceCallback<void()> cb,
      uint32_t heartbeat_timer) = 0;
  virtual ~PluginFactoryInterface() = default;
};

// Support absl format for PluginType.
absl::FormatConvertResult<absl::FormatConversionCharSet::kString>
AbslFormatConvert(const Types::Plugin& type,
                  const absl::FormatConversionSpec& conversion_spec,
                  absl::FormatSink* output_sink);

// Support streaming for PluginType.
std::ostream& operator<<(std::ostream& out, const Types::Plugin& type);

class PluginFactory : public PluginFactoryInterface {
 public:
  PluginFactory();
  explicit PluginFactory(
      scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory)
      : bpf_skeleton_factory_(bpf_skeleton_factory) {}
  std::unique_ptr<PluginInterface> Create(
      Types::Plugin type,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache,
      scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
      scoped_refptr<DeviceUserInterface> device_user,
      uint32_t batch_interval_s) override;
  std::unique_ptr<PluginInterface> CreateAgentPlugin(
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<DeviceUserInterface> device_user,
      std::unique_ptr<org::chromium::AttestationProxyInterface>
          attestation_proxy,
      std::unique_ptr<org::chromium::TpmManagerProxyInterface>
          tpm_manager_proxy,
      base::OnceCallback<void()> cb,
      uint32_t heartbeat_timer) override;

 private:
  scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory_;
};

}  // namespace secagentd
#endif  // SECAGENTD_PLUGINS_H_
