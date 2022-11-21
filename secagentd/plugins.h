// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_PLUGINS_H_
#define SECAGENTD_PLUGINS_H_

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "attestation/proto_bindings/interface.pb.h"
#include "attestation-client/attestation/dbus-proxies.h"
#include "base/memory/scoped_refptr.h"
#include "base/timer/timer.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/message_sender.h"
#include "secagentd/process_cache.h"
#include "tpm_manager/proto_bindings/tpm_manager.pb.h"
#include "tpm_manager-client/tpm_manager/dbus-proxies.h"

namespace secagentd {

namespace testing {
class AgentPluginTestFixture;
}

class PluginInterface {
 public:
  // Activate the plugin.
  virtual absl::Status Activate() = 0;
  virtual std::string GetName() const = 0;
  virtual ~PluginInterface() = default;
};

class ProcessPlugin : public PluginInterface {
 public:
  ProcessPlugin(scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
                scoped_refptr<MessageSenderInterface> message_sender,
                scoped_refptr<ProcessCacheInterface> process_cache);
  // Load, verify and attach the process BPF applications.
  absl::Status Activate() override;
  std::string GetName() const override;

  void HandleRingBufferEvent(const bpf::cros_event& bpf_event);
  void HandleBpfRingBufferReadReady() const;

 private:
  std::unique_ptr<cros_xdr::reporting::XdrProcessEvent> MakeExecEvent(
      const secagentd::bpf::cros_process_start& process_start);
  std::unique_ptr<cros_xdr::reporting::XdrProcessEvent> MakeTerminateEvent(
      const secagentd::bpf::cros_process_exit& process_exit);
  // This is static because it must be accessible to a C style function.
  static struct BpfCallbacks callbacks_;
  base::WeakPtrFactory<ProcessPlugin> weak_ptr_factory_;
  scoped_refptr<MessageSenderInterface> message_sender_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  scoped_refptr<BpfSkeletonFactoryInterface> factory_;
  std::unique_ptr<BpfSkeletonInterface> skeleton_wrapper_;
};

class AgentPlugin : public PluginInterface {
 public:
  explicit AgentPlugin(scoped_refptr<MessageSenderInterface> message_sender,
                       std::unique_ptr<org::chromium::AttestationProxyInterface>
                           attestation_proxy,
                       std::unique_ptr<org::chromium::TpmManagerProxyInterface>
                           tpm_manager_proxy,
                       base::OnceCallback<void()> cb);

  // Initialize Agent proto and starts agent heartbeat events.
  absl::Status Activate() override;
  std::string GetName() const override;

 private:
  friend class testing::AgentPluginTestFixture;

  // Starts filling in the tcb fields of the agent proto and initializes async
  // timers that wait for tpm_manager and attestation to be ready. When services
  // are ready GetBootInformation() and GetTpmInformation() will be called to
  // fill remaining fields.
  void StartInitializingAgentProto();
  // Delayed function that will be called when attestation is ready. Fills the
  // boot information in the agent proto.
  void GetBootInformation(bool available);
  // Delayed function that will be called when tpm_manager is ready. Fills the
  // tpm information in the agent proto.
  void GetTpmInformation(bool available);
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
  std::unique_ptr<org::chromium::AttestationProxyInterface> attestation_proxy_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_proxy_;
  base::OnceCallback<void()> daemon_cb_;
  base::Lock tcb_attributes_lock_;
};

class PluginFactoryInterface {
 public:
  enum class PluginType { kAgent, kProcess };

  virtual std::unique_ptr<PluginInterface> Create(
      PluginType type,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache) = 0;
  virtual std::unique_ptr<PluginInterface> Create(
      PluginType type,
      scoped_refptr<MessageSenderInterface> message_sender,
      std::unique_ptr<org::chromium::AttestationProxyInterface>
          attestation_proxy,
      std::unique_ptr<org::chromium::TpmManagerProxyInterface>
          tpm_manager_proxy,
      base::OnceCallback<void()> cb) = 0;
  virtual ~PluginFactoryInterface() = default;
};

namespace Types {
using Plugin = PluginFactoryInterface::PluginType;
}  // namespace Types

// Support absl format for PluginType.
absl::FormatConvertResult<absl::FormatConversionCharSet::kString>
AbslFormatConvert(const PluginFactoryInterface::PluginType& type,
                  const absl::FormatConversionSpec& conversion_spec,
                  absl::FormatSink* output_sink);

// Support streaming for PluginType.
std::ostream& operator<<(std::ostream& out,
                         const PluginFactoryInterface::PluginType& type);

class PluginFactory : public PluginFactoryInterface {
 public:
  PluginFactory();
  explicit PluginFactory(
      scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory)
      : bpf_skeleton_factory_(bpf_skeleton_factory) {}
  std::unique_ptr<PluginInterface> Create(
      PluginType type,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache) override;
  std::unique_ptr<PluginInterface> Create(
      PluginType type,
      scoped_refptr<MessageSenderInterface> message_sender,
      std::unique_ptr<org::chromium::AttestationProxyInterface>
          attestation_proxy,
      std::unique_ptr<org::chromium::TpmManagerProxyInterface>
          tpm_manager_proxy,
      base::OnceCallback<void()> cb) override;

 private:
  scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory_;
};

}  // namespace secagentd
#endif  // SECAGENTD_PLUGINS_H_
