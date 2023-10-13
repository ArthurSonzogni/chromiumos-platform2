// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_PLUGINS_H_
#define SECAGENTD_PLUGINS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "attestation/proto_bindings/interface.pb.h"
#include "attestation-client/attestation/dbus-proxies.h"
#include "base/check.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "cryptohome/proto_bindings/auth_factor.pb.h"
#include "cryptohome/proto_bindings/UserDataAuth.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "secagentd/batch_sender.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/common.h"
#include "secagentd/device_user.h"
#include "secagentd/message_sender.h"
#include "secagentd/metrics_sender.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/process_cache.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "tpm_manager/proto_bindings/tpm_manager.pb.h"
#include "tpm_manager-client/tpm_manager/dbus-proxies.h"
#include "user_data_auth/dbus-proxies.h"

namespace secagentd {

using AuthFactorType = cros_xdr::reporting::Authentication_AuthenticationType;

// If the auth factor is not yet filled wait to see
// if dbus signal is late.
static constexpr base::TimeDelta kWaitForAuthFactorS = base::Seconds(1);
static constexpr uint64_t kMaxDelayForLockscreenAttemptsS = 3;

namespace testing {
class AgentPluginTestFixture;
class ProcessPluginTestFixture;
class NetworkPluginTestFixture;
class AuthenticationPluginTestFixture;
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
template <typename HashT,
          typename XdrT,
          typename XdrAtomicVariantT,
          Types::BpfSkeleton SkelType,
          reporting::Destination destination>
struct PluginConfig {
  using HashType = HashT;
  using XdrType = XdrT;
  using XdrAtomicType = XdrAtomicVariantT;
  static constexpr Types::BpfSkeleton skeleton_type{SkelType};
  static constexpr reporting::Destination reporting_destination{destination};
};

using NetworkPluginConfig =
    PluginConfig<std::string,
                 cros_xdr::reporting::XdrNetworkEvent,
                 cros_xdr::reporting::NetworkEventAtomicVariant,
                 Types::BpfSkeleton::kNetwork,
                 reporting::Destination::CROS_SECURITY_NETWORK>;

template <typename Config>
class BpfPlugin : public PluginInterface {
 public:
  using BatchSenderType = BatchSender<typename Config::HashType,
                                      typename Config::XdrType,
                                      typename Config::XdrAtomicType>;
  using BatchSenderInterfaceType =
      BatchSenderInterface<typename Config::HashType,
                           typename Config::XdrType,
                           typename Config::XdrAtomicType>;
  using BatchKeyGenerator = base::RepeatingCallback<std::string(
      const typename Config::XdrAtomicType&)>;

  BpfPlugin(
      BatchKeyGenerator batch_key_generator,
      scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
      scoped_refptr<DeviceUserInterface> device_user,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache,
      scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
      uint32_t batch_interval_s)
      : batch_interval_s_(batch_interval_s),
        weak_ptr_factory_(this),
        device_user_(device_user),
        message_sender_(message_sender),
        policies_features_broker_(policies_features_broker),
        process_cache_(process_cache) {
    batch_sender_ = std::make_unique<BatchSenderType>(
        std::move(batch_key_generator), message_sender,
        Config::reporting_destination, batch_interval_s);
    CHECK(message_sender != nullptr);
    CHECK(process_cache != nullptr);
    CHECK(bpf_skeleton_factory);
    factory_ = std::move(bpf_skeleton_factory);
  }

  absl::Status Activate() override {
    // Was called previously, so do nothing and report OK.
    if (skeleton_wrapper_) {
      return absl::OkStatus();
    }
    struct BpfCallbacks callbacks;
    callbacks.ring_buffer_event_callback = base::BindRepeating(
        &BpfPlugin::HandleRingBufferEvent, weak_ptr_factory_.GetWeakPtr());
    callbacks.ring_buffer_read_ready_callback =
        base::BindRepeating(&BpfPlugin::HandleBpfRingBufferReadReady,
                            weak_ptr_factory_.GetWeakPtr());
    skeleton_wrapper_ = factory_->Create(
        Config::skeleton_type, std::move(callbacks), batch_interval_s_);
    if (skeleton_wrapper_ == nullptr) {
      return absl::InternalError(
          absl::StrFormat("%s BPF program loading error.", GetName()));
    }

    batch_sender_->Start();
    return absl::OkStatus();
  }

  absl::Status Deactivate() override {
    // destructing the skeleton_wrapper_ unloads and cleans up the BPFs.
    skeleton_wrapper_ = nullptr;
    return absl::OkStatus();
  }

  bool IsActive() const override { return skeleton_wrapper_ != nullptr; }

  void OnDeviceUserRetrieved(
      std::unique_ptr<typename Config::XdrAtomicType> atomic_event,
      const std::string& device_user) {
    atomic_event->mutable_common()->set_device_user(device_user);
    EnqueueBatchedEvent(std::move(atomic_event));
  }

 protected:
  uint32_t batch_interval_s_;
  base::WeakPtrFactory<BpfPlugin> weak_ptr_factory_;
  std::unique_ptr<BatchSenderInterfaceType> batch_sender_;
  scoped_refptr<DeviceUserInterface> device_user_;
  scoped_refptr<MessageSenderInterface> message_sender_;
  scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker_;
  scoped_refptr<ProcessCacheInterface> process_cache_;

 private:
  friend testing::NetworkPluginTestFixture;

  virtual void EnqueueBatchedEvent(
      std::unique_ptr<typename Config::XdrAtomicType> atomic_event) = 0;
  void HandleBpfRingBufferReadReady() const {
    skeleton_wrapper_->ConsumeEvent();
  }
  virtual void HandleRingBufferEvent(const bpf::cros_event& bpf_event) = 0;
  void SetBatchSenderForTesting(
      std::unique_ptr<BatchSenderInterfaceType> given) {
    batch_sender_ = std::move(given);
  }

  scoped_refptr<BpfSkeletonFactoryInterface> factory_;
  std::unique_ptr<BpfSkeletonInterface> skeleton_wrapper_;
};

class NetworkPlugin : public BpfPlugin<NetworkPluginConfig> {
 public:
  NetworkPlugin(
      scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache,
      scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
      scoped_refptr<DeviceUserInterface> device_user,
      uint32_t batch_interval_s)
      : BpfPlugin(base::BindRepeating(
                      [](const cros_xdr::reporting::NetworkEventAtomicVariant&)
                          -> std::string {
                        // TODO(b:282814056): Make hashing function optional
                        //  for batch_sender then drop this. Not all users
                        //  of batch_sender need the visit functionality.
                        return "";
                      }),
                  bpf_skeleton_factory,
                  device_user,
                  message_sender,
                  process_cache,
                  policies_features_broker,
                  batch_interval_s) {}

  std::string GetName() const override;

  /* Given a set of addresses (in network byte order)
   * ,a set of ports and a protocol ID compute the
   * community flow ID hash.
   */
  static std::string ComputeCommunityHashv1(
      const absl::Span<const uint8_t>& saddr_in,
      const absl::Span<const uint8_t>& daddr_in,
      uint16_t sport,
      uint16_t dport,
      uint8_t proto,
      uint16_t seed = 0);

 private:
  void EnqueueBatchedEvent(
      std::unique_ptr<cros_xdr::reporting::NetworkEventAtomicVariant>
          atomic_event) override;
  template <typename ProtoT>
  void FillProcessTree(ProtoT proto,
                       const bpf::cros_process_task_info& task) const {
    auto hierarchy =
        process_cache_->GetProcessHierarchy(task.pid, task.start_time, 2);
    if (hierarchy.empty()) {
      LOG(ERROR) << absl::StrFormat(
          "pid %d cmdline(%s) not in process cache. "
          "Creating a degraded %s filled with information available from BPF "
          "process map.",
          task.pid, task.commandline, proto->GetTypeName());

      ProcessCache::PartiallyFillProcessFromBpfTaskInfo(
          task, proto->mutable_process(),
          device_user_->GetUsernamesForRedaction());
      auto parent_process = process_cache_->GetProcessHierarchy(
          task.ppid, task.parent_start_time, 1);
      if (parent_process.size() == 1) {
        proto->set_allocated_parent_process(parent_process[0].release());
      }
    }
    if (hierarchy.size() >= 1) {
      proto->set_allocated_process(hierarchy[0].release());
    }
    if (hierarchy.size() == 2) {
      proto->set_allocated_parent_process(hierarchy[1].release());
    }
  }
  void HandleRingBufferEvent(const bpf::cros_event& bpf_event) override;
  std::unique_ptr<cros_xdr::reporting::NetworkSocketListenEvent>
  MakeListenEvent(
      const secagentd::bpf::cros_network_socket_listen& listen_event) const;
  std::unique_ptr<cros_xdr::reporting::NetworkFlowEvent> MakeFlowEvent(
      const secagentd::bpf::cros_synthetic_network_flow& flow_event) const;
};

// TODO(b:283278819): convert this over to use the generic BpfPlugin.
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

  // Pushes the given process event into the next outgoing batch.
  void EnqueueBatchedEvent(
      std::unique_ptr<cros_xdr::reporting::ProcessEventAtomicVariant>
          atomic_event);
  // Converts the BPF process start event into a XDR process exec
  // protobuf.
  std::unique_ptr<cros_xdr::reporting::ProcessExecEvent> MakeExecEvent(
      const secagentd::bpf::cros_process_start& process_start);
  std::unique_ptr<cros_xdr::reporting::ProcessTerminateEvent>
  // Converts the BPF process exit event into a XDR process terminate
  // protobuf.
  MakeTerminateEvent(const secagentd::bpf::cros_process_exit& process_exit);
  // Callback function that is ran when the device user is ready.
  void OnDeviceUserRetrieved(
      std::unique_ptr<cros_xdr::reporting::ProcessEventAtomicVariant>
          atomic_event,
      const std::string& device_user);
  // Inject the given (mock) BatchSender object for unit testing.
  void SetBatchSenderForTesting(std::unique_ptr<BatchSenderType> given) {
    batch_sender_ = std::move(given);
  }

  base::WeakPtrFactory<ProcessPlugin> weak_ptr_factory_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker_;
  scoped_refptr<DeviceUserInterface> device_user_;
  scoped_refptr<BpfSkeletonFactoryInterface> factory_;
  std::unique_ptr<BpfSkeletonInterface> skeleton_wrapper_;
  std::unique_ptr<BatchSenderType> batch_sender_;
};

class AuthenticationPlugin : public PluginInterface {
 public:
  AuthenticationPlugin(
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
      scoped_refptr<DeviceUserInterface> device_user,
      uint32_t batch_interval_s);
  // Starts reporting user authentication events.
  absl::Status Activate() override;
  absl::Status Deactivate() override;
  bool IsActive() const override;
  std::string GetName() const override;

 private:
  friend class testing::AuthenticationPluginTestFixture;

  using BatchSenderType =
      BatchSenderInterface<std::monostate,
                           cros_xdr::reporting::XdrUserEvent,
                           cros_xdr::reporting::UserEventAtomicVariant>;

  // Creates and sends a screen Lock event.
  void HandleScreenLock();
  // Creates and sends a screen Unlock event.
  void HandleScreenUnlock();
  // Logs error if registration fails.
  void HandleRegistrationResult(const std::string& interface,
                                const std::string& signal,
                                bool success);
  // Creates and sends a login/out event based on the state.
  void HandleSessionStateChange(const std::string& state);
  // Used to fill the auth factor for login and unlock.
  // Also fills in the device user.
  void HandleAuthenticateAuthFactorCompleted(
      const user_data_auth::AuthenticateAuthFactorCompleted& result);
  // Fills the proto's auth factor if auth_factor_ is known.
  // Returns if auth factor was filled.
  bool FillAuthFactor(cros_xdr::reporting::Authentication* proto);
  // If there is an entry event but auth factor is not filled, wait and
  // then check again for auth factor. If still not found send message anyway.
  void DelayedCheckForAuthSignal(
      std::unique_ptr<cros_xdr::reporting::UserEventAtomicVariant> xdr_proto,
      cros_xdr::reporting::Authentication* authentication);
  // Callback function that is ran when the device user is ready.
  void OnDeviceUserRetrieved(
      std::unique_ptr<cros_xdr::reporting::UserEventAtomicVariant> atomic_event,
      const std::string& device_user);
  // When the first session start happens in the case of a secagentd restart
  // check if a user is already signed in and if so send a login event.
  void OnFirstSessionStart(const std::string& device_user);
  // Inject the given (mock) BatchSender object for unit testing.
  void SetBatchSenderForTesting(std::unique_ptr<BatchSenderType> given) {
    batch_sender_ = std::move(given);
  }

  base::WeakPtrFactory<AuthenticationPlugin> weak_ptr_factory_;
  scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker_;
  scoped_refptr<DeviceUserInterface> device_user_;
  std::unique_ptr<BatchSenderType> batch_sender_;
  std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyInterface>
      cryptohome_proxy_;
  AuthFactorType auth_factor_type_ =
      AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN;
  const std::unordered_map<user_data_auth::AuthFactorType, AuthFactorType>
      auth_factor_map_ = {
          {user_data_auth::AUTH_FACTOR_TYPE_UNSPECIFIED,
           AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN},
          {user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
           AuthFactorType::Authentication_AuthenticationType_AUTH_PASSWORD},
          {user_data_auth::AUTH_FACTOR_TYPE_PIN,
           AuthFactorType::Authentication_AuthenticationType_AUTH_PIN},
          {user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
           AuthFactorType::
               Authentication_AuthenticationType_AUTH_ONLINE_RECOVERY},
          {user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
           AuthFactorType::Authentication_AuthenticationType_AUTH_KIOSK},
          {user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD,
           AuthFactorType::Authentication_AuthenticationType_AUTH_SMART_CARD},
          {user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT,
           AuthFactorType::Authentication_AuthenticationType_AUTH_FINGERPRINT},
          {user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT,
           AuthFactorType::Authentication_AuthenticationType_AUTH_FINGERPRINT},
      };
  int64_t latest_successful_login_timestamp_{-1};
  uint64_t latest_pin_failure_{0};
  bool is_active_{false};
  bool last_auth_was_password_{false};
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
  // Callback that is used when the attestation service is ready that calls
  // GetCrosSecureBootInformation and sends metrics.
  void AttestationCb(bool available);
  // Delayed function that will be called when attestation is ready. Fills the
  // boot information in the agent proto if Cros Secure boot is used.
  metrics::CrosBootmode GetCrosSecureBootInformation(bool available);
  // Callback that is used when the tpm service is ready that calls
  // GetTpmInformation and sends metrics.
  void TpmCb(bool available);
  // Delayed function that will be called when tpm_manager is ready. Fills the
  // tpm information in the agent proto.
  metrics::Tpm GetTpmInformation(bool available);
  // Fills the boot information in the agent proto if Uefi Secure boot is used.
  // Note: Only for flex machines.
  metrics::UefiBootmode GetUefiSecureBootInformation(
      const base::FilePath& boot_params_filepath);
  // Sends an agent event dependant on whether it is start or heartbeat event.
  // Uses the StartEventStatusCallback() to handle the status of the message.
  void SendAgentEvent(bool is_agent_start);
  // Checks the message status of the agent start event. If the message is
  // successfully sent it calls the daemon callback to run the remaining
  // plugins. If the message fails to send it will retry sending the message
  // every 3 seconds.
  void StartEventStatusCallback(reporting::Status status);
  inline void SendStartEvent() { SendAgentEvent(true); }
  inline void SendHeartbeatEvent() { SendAgentEvent(false); }
  // Callback function that is ran when the device user is ready.
  void OnDeviceUserRetrieved(
      std::unique_ptr<cros_xdr::reporting::AgentEventAtomicVariant>
          atomic_event,
      const std::string& device_user);

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
