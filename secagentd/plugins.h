// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_PLUGINS_H_
#define SECAGENTD_PLUGINS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "attestation-client/attestation/dbus-proxies.h"
#include "base/check.h"
#include "base/files/file_path.h"
#include "base/memory/ptr_util.h"
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
#include "tpm_manager-client/tpm_manager/dbus-proxies.h"
#include "user_data_auth/dbus-proxies.h"

namespace secagentd {

using AuthFactorType = cros_xdr::reporting::Authentication_AuthenticationType;

// If the auth factor is not yet filled wait to see
// if dbus signal is late.
static constexpr base::TimeDelta kWaitForAuthFactorS = base::Seconds(1);
static constexpr uint64_t kMaxDelayForLockscreenAttemptsS = 3;

// File path types (from your original code)
enum class FilePathName {
  USER_FILES_DIR,
  COOKIES_DIR,
  COOKIES_JOURNAL_DIR,
  SAFE_BROWSING_COOKIES_DIR,
  SAFE_BROWSING_COOKIES_JOURNAL_DIR,
  USER_SECRET_STASH_DIR,
  ROOT,
  MOUNTED_ARCHIVE,
  GOOGLE_DRIVE_FS,
  STATEFUL_PARTITION,
  USB_STORAGE,
  DEVICE_SETTINGS_POLICY_DIR,
  DEVICE_SETTINGS_OWNER_KEY,
  SESSION_MANAGER_POLICY_DIR,
  SESSION_MANAGER_POLICY_KEY,
  CRYPTOHOME_KEY,
  CRYPTOHOME_ECC_KEY,
  // Add the last element of the enum, used for counting
  FILE_PATH_NAME_COUNT,
};

// Enum for file path categories
enum class FilePathCategory { USER_PATH, SYSTEM_PATH, REMOVABLE_PATH };

// Structure to hold path information
struct PathInfo {
  std::string pathPrefix;  // Store the full path for non-user paths and for
                           // user part before the hash placeholder
  std::optional<std::string> pathSuffix;  // Only for user hash paths. Store the
                                          // part after the hash placeholder
  bpf::file_monitoring_mode monitoringMode;
  cros_xdr::reporting::SensitiveFileType fileType;
  FilePathCategory pathCategory;
  std::optional<std::string> fullResolvedPath;
  bpf::device_monitoring_type deviceMonitoringType =
      bpf::device_monitoring_type::MONITOR_SPECIFIC_FILES;
};

namespace testing {
class AgentPluginTestFixture;
class ProcessPluginTestFixture;
class NetworkPluginTestFixture;
class AuthenticationPluginTestFixture;
class FilePluginTestFixture;
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
  // Flushes the batch sender if it exists.
  virtual void Flush() = 0;
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

class BpfSkeletonHelperInterface {
 public:
  virtual absl::Status LoadAndAttach(struct BpfCallbacks callbacks) = 0;
  virtual absl::Status DetachAndUnload() = 0;
  virtual bool IsAttached() const = 0;
  virtual absl::StatusOr<int> FindBpfMapByName(const std::string& name) = 0;
  virtual ~BpfSkeletonHelperInterface() = default;
};

template <Types::BpfSkeleton BpfSkeletonType>
class BpfSkeletonHelper : public BpfSkeletonHelperInterface {
 public:
  BpfSkeletonHelper(
      scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
      uint32_t batch_interval_s)
      : batch_interval_s_(batch_interval_s), weak_ptr_factory_(this) {
    CHECK(bpf_skeleton_factory);
    factory_ = std::move(bpf_skeleton_factory);
  }

  void BpfSkeletonConsumeEvent() const { skeleton_wrapper_->ConsumeEvent(); }

  absl::Status LoadAndAttach(struct BpfCallbacks callbacks) override {
    if (skeleton_wrapper_) {
      return absl::OkStatus();
    }
    // If ring_buffer_read_ready_callback is set by the plugin, then don't
    // override. If not set, use this default callback
    if (!callbacks.ring_buffer_read_ready_callback) {
      callbacks.ring_buffer_read_ready_callback =
          base::BindRepeating(&BpfSkeletonHelper::BpfSkeletonConsumeEvent,
                              weak_ptr_factory_.GetWeakPtr());
    }
    skeleton_wrapper_ = factory_->Create(BpfSkeletonType, std::move(callbacks),
                                         batch_interval_s_);
    if (skeleton_wrapper_ == nullptr) {
      return absl::InternalError(
          absl::StrFormat("%s BPF program loading error.", BpfSkeletonType));
    }

    return absl::OkStatus();
  }

  absl::Status DetachAndUnload() override {
    // Unset the skeleton_wrapper_ unloads and cleans up the BPFs.
    skeleton_wrapper_ = nullptr;
    return absl::OkStatus();
  }

  absl::StatusOr<int> FindBpfMapByName(const std::string& name) override {
    return skeleton_wrapper_->FindBpfMapByName(name);
  }

  bool IsAttached() const override { return skeleton_wrapper_ != nullptr; }

 private:
  // friend testing::BpfSkeletonHelperTestFixture;

  uint32_t batch_interval_s_;
  base::WeakPtrFactory<BpfSkeletonHelper> weak_ptr_factory_;
  scoped_refptr<BpfSkeletonFactoryInterface> factory_;
  std::unique_ptr<BpfSkeletonInterface> skeleton_wrapper_;
};

class NetworkPlugin : public PluginInterface {
 public:
  NetworkPlugin(
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
  void Flush() override {
    if (batch_sender_) {
      batch_sender_->Flush();
    }
  }

  // Handles an individual incoming Process BPF event.
  void HandleRingBufferEvent(const bpf::cros_event& bpf_event);

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
  friend testing::NetworkPluginTestFixture;

  using BatchSenderType =
      BatchSenderInterface<std::string,
                           cros_xdr::reporting::XdrNetworkEvent,
                           cros_xdr::reporting::NetworkEventAtomicVariant>;
  // Inject the given (mock) BatchSender object for unit testing.
  void SetBatchSenderForTesting(std::unique_ptr<BatchSenderType> given) {
    batch_sender_ = std::move(given);
  }
  // Pushes the given process event into the next outgoing batch.
  void EnqueueBatchedEvent(
      std::unique_ptr<cros_xdr::reporting::NetworkEventAtomicVariant>
          atomic_event);

  void OnDeviceUserRetrieved(
      std::unique_ptr<cros_xdr::reporting::NetworkEventAtomicVariant>
          atomic_event,
      const std::string& device_user,
      const std::string& device_userhash);

  template <typename ProtoT>
  void FillProcessTree(ProtoT proto,
                       const bpf::cros_process_start& process_start,
                       bool has_full_process_start) const;

  std::unique_ptr<cros_xdr::reporting::NetworkSocketListenEvent>
  MakeListenEvent(
      const secagentd::bpf::cros_network_socket_listen& listen_event) const;
  std::unique_ptr<cros_xdr::reporting::NetworkFlowEvent> MakeFlowEvent(
      const secagentd::bpf::cros_synthetic_network_flow& flow_event) const;
  std::unique_ptr<
      base::LRUCache<bpf::cros_flow_map_key, bpf::cros_flow_map_value>>
      prev_tx_rx_totals_;  // declaring this as a value member strangely seems
                           // to make it const.

  base::WeakPtrFactory<NetworkPlugin> weak_ptr_factory_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker_;
  scoped_refptr<DeviceUserInterface> device_user_;
  std::unique_ptr<BatchSenderType> batch_sender_;
  std::unique_ptr<BpfSkeletonHelperInterface> bpf_skeleton_helper_;
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
  void Flush() override {
    if (batch_sender_) {
      batch_sender_->Flush();
    }
  }

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
      const std::string& device_user,
      const std::string& device_userhash);
  // Inject the given (mock) BatchSender object for unit testing.
  void SetBatchSenderForTesting(std::unique_ptr<BatchSenderType> given) {
    batch_sender_ = std::move(given);
  }

  base::WeakPtrFactory<ProcessPlugin> weak_ptr_factory_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker_;
  scoped_refptr<DeviceUserInterface> device_user_;
  std::unique_ptr<BatchSenderType> batch_sender_;
  std::unique_ptr<BpfSkeletonHelperInterface> bpf_skeleton_helper_;
};
class FilePlugin : public PluginInterface {
 public:
  class FileEventKey {
   public:
    std::string process_uuid;
    uint64_t device_id;
    uint64_t inode;
    cros_xdr::reporting::FileEventAtomicVariant::VariantTypeCase event_type;
    template <typename H>
    friend H AbslHashValue(H h, const FileEventKey& key);
    bool operator==(const FileEventKey& other) const = default;
  };

  using FileEventMap =
      absl::flat_hash_map<FileEventKey,
                          cros_xdr::reporting::FileEventAtomicVariant*>;

  FilePlugin(
      scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache,
      scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
      scoped_refptr<DeviceUserInterface> device_user,
      uint32_t batch_interval_s);

  // Load, verify and attach the file BPF applications.
  absl::Status Activate() override;
  absl::Status Deactivate() override;
  bool IsActive() const override;
  std::string GetName() const override;
  void Flush() override {
    if (batch_sender_) {
      batch_sender_->Flush();
    }
  }

  // Handles an individual incoming File BPF event.
  void HandleRingBufferEvent(const bpf::cros_event& bpf_event);

 private:
  friend class testing::FilePluginTestFixture;

  using BatchSenderType =
      BatchSenderInterface<std::string,
                           cros_xdr::reporting::XdrFileEvent,
                           cros_xdr::reporting::FileEventAtomicVariant>;
  // Inject the given (mock) BatchSender object for unit testing.
  void SetBatchSenderForTesting(std::unique_ptr<BatchSenderType> given) {
    batch_sender_ = std::move(given);
  }

  // Collect an event for coalescing across a batching period.
  void CollectEvent(std::unique_ptr<cros_xdr::reporting::FileEventAtomicVariant>
                        atomic_event);

  // Flush the collected events to the batch sender.
  void FlushCollectedEvents();

  // Callback function that is ran when the device user is ready.
  void OnDeviceUserRetrieved(
      std::unique_ptr<cros_xdr::reporting::FileEventAtomicVariant> atomic_event,
      const std::string& device_user,
      const std::string& device_userhash);

  // Flushes out the coalesced file events in the map to the batch sender.
  void PeriodicMapFlush();

  void OnSessionStateChange(const std::string& state);

  std::unique_ptr<cros_xdr::reporting::FileReadEvent> MakeReadEvent(
      const secagentd::bpf::cros_file_event& close_event) const;
  std::unique_ptr<cros_xdr::reporting::FileModifyEvent> MakeModifyEvent(
      const secagentd::bpf::cros_file_event& close_event) const;
  std::unique_ptr<cros_xdr::reporting::FileModifyEvent>
  MakeAttributeModifyEvent(
      const secagentd::bpf::cros_file_event& attribute_modify_event) const;

  // Updates BPF maps with paths and their associated information.
  // This function updates various BPF maps based on the provided paths and
  // their monitoring modes. It uses a helper interface to retrieve the file
  // descriptors for the BPF maps and performs updates on the maps accordingly.
  // It includes error handling for map retrieval and update operations, with
  // relevant logging for diagnostics.
  absl::Status UpdateBPFMapForPathMaps(
      const std::optional<std::string>& optionalUserhash,
      const std::map<FilePathName, std::vector<PathInfo>>& paths_map);

  absl::Status InitializeFileBpfMaps(const std::string& userhash);

  void OnUserLogin(const std::string& device_user, const std::string& userHash);

  absl::Status OnUserLogout(const std::string& userHash);

  absl::Status OnDeviceMount(const std::string& mount_point);

  base::WeakPtrFactory<FilePlugin> weak_ptr_factory_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker_;
  scoped_refptr<DeviceUserInterface> device_user_;
  std::unique_ptr<BatchSenderType> batch_sender_;
  std::unique_ptr<BpfSkeletonHelperInterface> bpf_skeleton_helper_;

  std::unique_ptr<FileEventMap> event_map_;
  std::vector<std::unique_ptr<cros_xdr::reporting::FileEventAtomicVariant>>
      ordered_events_;

  uint32_t batch_interval_s_;

  base::RepeatingTimer coalesce_timer_;
};

template <typename H>
H AbslHashValue(H h, const FilePlugin::FileEventKey& key) {
  return H::combine(std::move(h), key.process_uuid, key.device_id,
                    key.event_type, key.inode);
}

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
  void Flush() override {
    if (batch_sender_) {
      batch_sender_->Flush();
    }
  }

 private:
  friend class testing::AuthenticationPluginTestFixture;

  using BatchSenderType =
      BatchSenderInterface<std::monostate,
                           cros_xdr::reporting::XdrUserEvent,
                           cros_xdr::reporting::UserEventAtomicVariant>;

  // Creates and sends a screen Lock event.
  void OnScreenLock();
  // Creates and sends a screen Unlock event.
  void OnScreenUnlock();
  // Logs error if registration fails.
  void OnRegistrationResult(const std::string& interface,
                            const std::string& signal,
                            bool success);
  // Creates and sends a login/out event based on the state.
  void OnSessionStateChange(const std::string& state);
  // Used to fill the auth factor for login and unlock.
  // Also fills in the device user.
  void OnAuthenticateAuthFactorCompleted(
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
      const std::string& device_user,
      const std::string& device_userhash);
  // When the first session start happens in the case of a secagentd restart
  // check if a user is already signed in and if so send a login event.
  void OnFirstSessionStart(const std::string& device_user,
                           const std::string& sanitized_username);
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
  std::string signed_in_user_ = device_user::kEmpty;
  int64_t latest_successful_login_timestamp_{-1};
  uint64_t latest_pin_failure_{0};
  bool is_active_{false};
  bool last_auth_was_password_{false};
};

class AgentPlugin : public PluginInterface {
  static constexpr char kBootDataFilepath[] = "sys/kernel/boot_params/data";

 public:
  AgentPlugin(scoped_refptr<MessageSenderInterface> message_sender,
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
  void Flush() override {}

 private:
  friend class testing::AgentPluginTestFixture;

  // Allow calling the private test-only constructor without befriending
  // unique_ptr.
  template <typename... Args>
  static std::unique_ptr<AgentPlugin> CreateForTesting(Args&&... args) {
    return base::WrapUnique(new AgentPlugin(std::forward<Args>(args)...));
  }

  // Accepts root_path for testing.
  AgentPlugin(scoped_refptr<MessageSenderInterface> message_sender,
              scoped_refptr<DeviceUserInterface> device_user,
              std::unique_ptr<org::chromium::AttestationProxyInterface>
                  attestation_proxy,
              std::unique_ptr<org::chromium::TpmManagerProxyInterface>
                  tpm_manager_proxy,
              base::OnceCallback<void()> cb,
              const base::FilePath& root_path,
              uint32_t heartbeat_timer);

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
      const std::string& device_user,
      const std::string& device_userhash);

  base::RepeatingTimer agent_heartbeat_timer_;
  cros_xdr::reporting::TcbAttributes tcb_attributes_;
  base::WeakPtrFactory<AgentPlugin> weak_ptr_factory_;
  scoped_refptr<MessageSenderInterface> message_sender_;
  scoped_refptr<DeviceUserInterface> device_user_;
  std::unique_ptr<org::chromium::AttestationProxyInterface> attestation_proxy_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_proxy_;
  base::OnceCallback<void()> daemon_cb_;
  const base::FilePath root_path_;
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
