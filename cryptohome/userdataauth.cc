// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <base/notreached.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/system/sys_info.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/cryptohome.h>
#include <chaps/isolate.h>
#include <chaps/token_manager_client.h>
#include <chromeos/constants/cryptohome.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <libhwsec/status.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <metrics/timer.h>

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_utils.h"
#include "cryptohome/auth_session.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/bootlockbox/boot_lockbox_client.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"
#include "cryptohome/cleanup/disk_cleanup.h"
#include "cryptohome/cleanup/low_disk_space_handler.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/converter.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/key_challenge_service_factory.h"
#include "cryptohome/key_challenge_service_factory_impl.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11/real_pkcs11_token_factory.h"
#include "cryptohome/signature_sealing/structures_proto.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/tpm.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/real_user_session.h"
#include "cryptohome/user_session/real_user_session_factory.h"
#include "cryptohome/uss_experiment_config_fetcher.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::Blob;
using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeMountError;
using cryptohome::error::CryptohomeTPMError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::Sha1;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

constexpr char kMountThreadName[] = "MountThread";
constexpr char kNotFirstBootFilePath[] = "/run/cryptohome/not_first_boot";
constexpr char kDeviceMapperDevicePrefix[] = "/dev/mapper/dmcrypt";

namespace {
// Some utility functions used by UserDataAuth.

// Get the Account ID for an AccountIdentifier proto.
const std::string& GetAccountId(const AccountIdentifier& id) {
  if (id.has_account_id()) {
    return id.account_id();
  }
  return id.email();
}

// Returns whether the Chrome OS image is a test one.
bool IsOsTestImage() {
  std::string chromeos_release_track;
  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_TRACK",
                                         &chromeos_release_track)) {
    // Fall back to the safer assumption that we're not in a test image.
    return false;
  }
  return base::StartsWith(chromeos_release_track, "test",
                          base::CompareCase::SENSITIVE);
}

// Whether the key can be used for lightweight challenge-response authentication
// check against the given user session.
bool KeyMatchesForLightweightChallengeResponseCheck(
    const KeyData& key_data, const UserSession& session) {
  DCHECK_EQ(key_data.type(), KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  DCHECK_EQ(key_data.challenge_response_key_size(), 1);
  if (session.key_data().type() != KeyData::KEY_TYPE_CHALLENGE_RESPONSE ||
      session.key_data().label().empty() ||
      session.key_data().label() != key_data.label())
    return false;
  if (session.key_data().challenge_response_key_size() != 1) {
    // Using multiple challenge-response keys at once is currently unsupported.
    return false;
  }
  if (session.key_data().challenge_response_key(0).public_key_spki_der() !=
      key_data.challenge_response_key(0).public_key_spki_der()) {
    LOG(WARNING) << "Public key mismatch for lightweight challenge-response "
                    "authentication check";
    return false;
  }
  return true;
}

CryptohomeVault::Options MountArgsToVaultOptions(
    const UserDataAuth::MountArgs& mount_args) {
  CryptohomeVault::Options vault_options;
  if (mount_args.force_dircrypto) {
    // If dircrypto is forced, it's an error to mount ecryptfs home unless
    // we are migrating from ecryptfs.
    vault_options.block_ecryptfs = true;
  } else if (mount_args.create_as_ecryptfs) {
    vault_options.force_type = EncryptedContainerType::kEcryptfs;
  }
  vault_options.migrate = mount_args.to_migrate_from_ecryptfs;
  return vault_options;
}

// Returns true if any of the path in |prefixes| starts with |path|
// Note that this function is case insensitive
bool PrefixPresent(const std::vector<FilePath>& prefixes,
                   const std::string path) {
  return std::any_of(
      prefixes.begin(), prefixes.end(), [&path](const FilePath& prefix) {
        return base::StartsWith(path, prefix.value(),
                                base::CompareCase::INSENSITIVE_ASCII);
      });
}

// Groups dm-crypt mounts for each user. Mounts for a user may have a source
// in either dmcrypt-<>-data or dmcrypt-<>-cache. Strip the application
// specific suffix for the device and use <> as the group key.
void GroupDmcryptDeviceMounts(
    std::multimap<const FilePath, const FilePath>* mounts,
    std::multimap<const FilePath, const FilePath>* grouped_mounts) {
  for (auto match = mounts->begin(); match != mounts->end(); ++match) {
    // Group dmcrypt-<>-data and dmcrypt-<>-cache mounts. Strip out last
    // '-' from the path.
    size_t last_component_index = match->first.value().find_last_of("-");

    if (last_component_index == std::string::npos) {
      continue;
    }

    base::FilePath device_group(
        match->first.value().substr(0, last_component_index));
    grouped_mounts->insert({device_group, match->second});
  }
}

// Creates KeyBlobs and AuthBlockState for the given |new_credentials| on
// |auth_block_utility|.
CryptoStatus CreateKeyBlobs(const AuthBlockUtility& auth_block_utility,
                            const KeysetManagement& keyset_management,
                            bool is_le_credential,
                            bool is_challenge_credential,
                            const Credentials& credentials,
                            KeyBlobs& out_key_blobs,
                            AuthBlockState& out_state) {
  AuthBlockType auth_block_type =
      auth_block_utility.GetAuthBlockTypeForCreation(
          is_le_credential, /*is_recovery=*/false, is_challenge_credential,
          AuthFactorStorageType::kVaultKeyset);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    LOG(ERROR) << "Error in obtaining AuthBlock type.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kUserDataAuthInvalidAuthBlockTypeInCreateKeyBlobs),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  CryptoStatus err = auth_block_utility.CreateKeyBlobsWithAuthBlock(
      auth_block_type, credentials, std::nullopt /*reset_secret*/, out_state,
      out_key_blobs);
  if (!err.ok()) {
    LOG(ERROR) << "Error in creating AuthBlock.";
    return err;
  }
  return OkStatus<CryptohomeCryptoError>();
}

// Derives KeyBlobs for the given |credentials| on |auth_block_utility|.
CryptoStatus DeriveKeyBlobs(AuthBlockUtility& auth_block_utility,
                            const Credentials& credentials,
                            KeyBlobs& out_key_blobs) {
  AuthBlockState auth_state;
  if (!auth_block_utility.GetAuthBlockStateFromVaultKeyset(
          credentials.key_data().label(), credentials.GetObfuscatedUsername(),
          auth_state /*Out*/)) {
    LOG(ERROR) << "Error in obtaining AuthBlock state for key derivation.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kUserDataAuthNoAuthBlockStateInDeriveKeyBlobs),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Determine the auth block type to use.
  AuthBlockType auth_block_type =
      auth_block_utility.GetAuthBlockTypeFromState(auth_state);
  if (auth_block_type == AuthBlockType::kMaxValue) {
    LOG(ERROR) << "Error in determining AuthBlock type from AuthBlock state "
                  "for key derivation.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kUserDataAuthInvalidAuthBlockTypeInDeriveKeyBlobs),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  CryptoStatus err = auth_block_utility.DeriveKeyBlobsWithAuthBlock(
      auth_block_type, credentials, auth_state, out_key_blobs);
  if (!err.ok()) {
    LOG(ERROR) << "Error in key derivation with AuthBlock.";
    return err;
  }
  return OkStatus<CryptohomeCryptoError>();
}

// Returns a vector of all the VaultKeyset labels in |out_labels| if the
// |credentials| has an empty label and the key type is KEY_TYPE_PASSWORD, and
// not PIN. Otherwise |credentials|'s label is pushed to |out_labels|. Returns
// false if there are no VaultKeysets on the disk, otherwise returns true.
bool GetKeyLabels(const KeysetManagement& keyset_management,
                  const Credentials& credentials,
                  std::vector<std::string>& out_labels) {
  std::vector<std::string> key_labels;
  // Don't get LE labels because LE credentials are discluded from wildcard and
  // we don't want unnecessary wrong authentication attempts on LE credentials.
  if (!keyset_management.GetVaultKeysetLabels(
          credentials.GetObfuscatedUsername(), /*include_le_labels*/ false,
          &key_labels)) {
    return false;
  }

  out_labels.clear();
  if (credentials.key_data().label() == "" &&
      credentials.key_data().type() == KeyData::KEY_TYPE_PASSWORD &&
      !credentials.key_data().policy().low_entropy_credential()) {
    // Use the |key_labels| from the GetVaultKeysetLabels() if the empty string,
    // i.e. wildcard, is received for the label and credentials type is either
    // password or smartunlock.
    out_labels = std::move(key_labels);
    return true;
  }
  // If the label received with the |credentials| is a specific label rather
  // than an empty string |out_label| should have only this specific label.
  out_labels.push_back(credentials.key_data().label());
  return true;
}

template <typename AuthenticateReply>
void ReplyWithAuthenticationResult(
    const AuthSession* auth_session,
    base::OnceCallback<void(const AuthenticateReply&)> on_done,
    CryptohomeStatus status) {
  DCHECK(auth_session);
  DCHECK(!on_done.is_null());
  AuthenticateReply reply;
  reply.set_authenticated(auth_session->GetStatus() ==
                          AuthStatus::kAuthStatusAuthenticated);
  ReplyWithError(std::move(on_done), std::move(reply), status);
}

}  // namespace

UserDataAuth::UserDataAuth()
    : origin_thread_id_(base::PlatformThread::CurrentId()),
      mount_thread_(nullptr),
      system_salt_(),
      hwsec_(nullptr),
      pinweaver_(nullptr),
      default_cryptohome_keys_manager_(nullptr),
      cryptohome_keys_manager_(nullptr),
      tpm_manager_util_(nullptr),
      default_platform_(new Platform()),
      platform_(default_platform_.get()),
      default_crypto_(nullptr),
      crypto_(nullptr),
      default_chaps_client_(new chaps::TokenManagerClient()),
      chaps_client_(default_chaps_client_.get()),
      default_pkcs11_init_(new Pkcs11Init()),
      pkcs11_init_(default_pkcs11_init_.get()),
      default_pkcs11_token_factory_(new RealPkcs11TokenFactory()),
      pkcs11_token_factory_(default_pkcs11_token_factory_.get()),
      firmware_management_parameters_(nullptr),
      default_fingerprint_manager_(),
      fingerprint_manager_(nullptr),
      ownership_callback_has_run_(false),
      default_install_attrs_(nullptr),
      install_attrs_(nullptr),
      enterprise_owned_(false),
      reported_pkcs11_init_fail_(false),
      default_user_activity_timestamp_manager_(
          new UserOldestActivityTimestampManager(platform_)),
      user_activity_timestamp_manager_(
          default_user_activity_timestamp_manager_.get()),
      default_homedirs_(nullptr),
      homedirs_(nullptr),
      default_keyset_management_(nullptr),
      keyset_management_(nullptr),
      auth_block_utility_(nullptr),
      default_auth_session_manager_(nullptr),
      auth_session_manager_(nullptr),
      default_low_disk_space_handler_(nullptr),
      low_disk_space_handler_(nullptr),
      disk_cleanup_threshold_(kFreeSpaceThresholdToTriggerCleanup),
      disk_cleanup_aggressive_threshold_(
          kFreeSpaceThresholdToTriggerAggressiveCleanup),
      disk_cleanup_critical_threshold_(
          kFreeSpaceThresholdToTriggerCriticalCleanup),
      disk_cleanup_target_free_space_(kTargetFreeSpaceAfterCleanup),
      default_user_session_factory_(nullptr),
      user_session_factory_(nullptr),
      public_mount_salt_(),
      guest_user_(brillo::cryptohome::home::kGuestUserName),
      force_ecryptfs_(true),
      fscrypt_v2_(false),
      legacy_mount_(true),
      bind_mount_downloads_(true),
      default_arc_disk_quota_(nullptr),
      arc_disk_quota_(nullptr) {}

UserDataAuth::~UserDataAuth() {
  if (low_disk_space_handler_) {
    low_disk_space_handler_->Stop();
  }
  if (mount_thread_) {
    mount_thread_->Stop();
  }
}

bool UserDataAuth::Initialize() {
  AssertOnOriginThread();

  // Note that we check to see if |origin_task_runner_| and |mount_task_runner_|
  // are available here because they may have been set to an overridden value
  // during unit testing before Initialize() is called.
  if (!origin_task_runner_) {
    origin_task_runner_ = base::ThreadTaskRunnerHandle::Get();
  }
  if (!mount_task_runner_) {
    mount_thread_ = std::make_unique<MountThread>(kMountThreadName, this);
  }

  if (!hwsec_) {
    // TODO(b/174816474): Get rid of the TPM object after we remove all useages
    // of it.
    Tpm* tpm = Tpm::GetSingleton();
    CHECK(tpm);
    hwsec_ = tpm->GetHwsec();
    CHECK(hwsec_);
  }

  if (!pinweaver_) {
    // TODO(b/174816474): Get rid of the TPM object after we remove all useages
    // of it.
    Tpm* tpm = Tpm::GetSingleton();
    CHECK(tpm);
    pinweaver_ = tpm->GetPinWeaver();
    CHECK(pinweaver_);
  }

  // Note that we check to see if |cryptohome_keys_manager_| is available here
  // because it may have been set to an overridden value during unit testing
  // before Initialize() is called.
  if (!cryptohome_keys_manager_) {
    default_cryptohome_keys_manager_.reset(
        new CryptohomeKeysManager(hwsec_, platform_));
    cryptohome_keys_manager_ = default_cryptohome_keys_manager_.get();
  }

  // Initialize Firmware Management Parameters
  if (!firmware_management_parameters_) {
    default_firmware_management_params_ =
        FirmwareManagementParameters::CreateInstance(hwsec_);
    firmware_management_parameters_ = default_firmware_management_params_.get();
  }

  if (!install_attrs_) {
    default_install_attrs_ = std::make_unique<InstallAttributes>(hwsec_);
    install_attrs_ = default_install_attrs_.get();
  }

  if (!crypto_) {
    // TODO(b/174816474): Get rid of the TPM object after we remove all useages
    // of it.
    Tpm* tpm = Tpm::GetSingleton();
    CHECK(tpm);

    default_crypto_ =
        std::make_unique<Crypto>(hwsec_, pinweaver_, cryptohome_keys_manager_,
                                 tpm->GetRecoveryCryptoBackend());
    crypto_ = default_crypto_.get();
  }

  if (!crypto_->Init()) {
    LOG(ERROR) << "Failed to initialize crypto.";
    return false;
  }

  if (!InitializeFilesystemLayout(platform_, &system_salt_)) {
    LOG(ERROR) << "Failed to initialize filesystem layout.";
    return false;
  }

  if (!keyset_management_) {
    default_keyset_management_ = std::make_unique<KeysetManagement>(
        platform_, crypto_, std::make_unique<VaultKeysetFactory>());
    keyset_management_ = default_keyset_management_.get();
  }

  if (!auth_block_utility_) {
    default_auth_block_utility_ = std::make_unique<AuthBlockUtilityImpl>(
        keyset_management_, crypto_, platform_);
    auth_block_utility_ = default_auth_block_utility_.get();
  }

  if (!auth_factor_manager_) {
    default_auth_factor_manager_ =
        std::make_unique<AuthFactorManager>(platform_);
    auth_factor_manager_ = default_auth_factor_manager_.get();
  }

  if (!user_secret_stash_storage_) {
    default_user_secret_stash_storage_ =
        std::make_unique<UserSecretStashStorage>(platform_);
    user_secret_stash_storage_ = default_user_secret_stash_storage_.get();
  }

  if (!auth_session_manager_) {
    default_auth_session_manager_ = std::make_unique<AuthSessionManager>(
        crypto_, platform_, keyset_management_, auth_block_utility_,
        auth_factor_manager_, user_secret_stash_storage_);
    auth_session_manager_ = default_auth_session_manager_.get();
  }

  if (!homedirs_) {
    auto container_factory =
        std::make_unique<EncryptedContainerFactory>(platform_);
    container_factory->set_allow_fscrypt_v2(fscrypt_v2_);
    auto vault_factory = std::make_unique<CryptohomeVaultFactory>(
        platform_, std::move(container_factory));
    vault_factory->set_enable_application_containers(
        enable_application_containers_);

    if (platform_->IsStatefulLogicalVolumeSupported()) {
      base::FilePath stateful_device = platform_->GetStatefulDevice();
      brillo::LogicalVolumeManager* lvm = platform_->GetLogicalVolumeManager();
      brillo::PhysicalVolume pv(stateful_device,
                                std::make_shared<brillo::LvmCommandRunner>());

      std::optional<brillo::VolumeGroup> vg;
      std::optional<brillo::Thinpool> thinpool;

      vg = lvm->GetVolumeGroup(pv);
      if (vg && vg->IsValid()) {
        thinpool = lvm->GetThinpool(*vg, "thinpool");
      }

      if (thinpool && vg) {
        vault_factory->CacheLogicalVolumeObjects(vg, thinpool);
      }
    }

    // This callback runs in HomeDirs::Remove on |this.homedirs_|. Since
    // |this.keyset_management_| won't be destroyed upon call of Remove(),
    // base::Unretained(keyset_management_) will be valid when the callback
    // runs.
    HomeDirs::RemoveCallback remove_callback =
        base::BindRepeating(&KeysetManagement::RemoveLECredentials,
                            base::Unretained(keyset_management_));
    default_homedirs_ = std::make_unique<HomeDirs>(
        platform_, std::make_unique<policy::PolicyProvider>(), remove_callback,
        std::move(vault_factory));
    homedirs_ = default_homedirs_.get();
  }

  auto homedirs = homedirs_->GetHomeDirs();
  for (const auto& dir : homedirs) {
    // TODO(b/205759690, dlunev): can be changed after a stepping stone release
    //  to `user_activity_timestamp_manager_->LoadTimestamp(dir.obfuscated);`
    base::Time legacy_timestamp =
        keyset_management_->GetKeysetBoundTimestamp(dir.obfuscated);
    user_activity_timestamp_manager_->LoadTimestampWithLegacy(dir.obfuscated,
                                                              legacy_timestamp);
    keyset_management_->CleanupPerIndexTimestampFiles(dir.obfuscated);
  }

  if (!user_session_factory_) {
    default_user_session_factory_ = std::make_unique<RealUserSessionFactory>(
        std::make_unique<MountFactory>(), platform_, homedirs_,
        keyset_management_, user_activity_timestamp_manager_,
        pkcs11_token_factory_);
    user_session_factory_ = default_user_session_factory_.get();
  }

  if (!low_disk_space_handler_) {
    default_low_disk_space_handler_ = std::make_unique<LowDiskSpaceHandler>(
        homedirs_, platform_, user_activity_timestamp_manager_);
    low_disk_space_handler_ = default_low_disk_space_handler_.get();
  }
  low_disk_space_handler_->disk_cleanup()->set_cleanup_threshold(
      disk_cleanup_threshold_);
  low_disk_space_handler_->disk_cleanup()->set_aggressive_cleanup_threshold(
      disk_cleanup_aggressive_threshold_);
  low_disk_space_handler_->disk_cleanup()->set_critical_cleanup_threshold(
      disk_cleanup_critical_threshold_);
  low_disk_space_handler_->disk_cleanup()->set_target_free_space(
      disk_cleanup_target_free_space_);

  if (!arc_disk_quota_) {
    default_arc_disk_quota_ = std::make_unique<ArcDiskQuota>(
        homedirs_, platform_, base::FilePath(kArcDiskHome));
    arc_disk_quota_ = default_arc_disk_quota_.get();
  }
  // Initialize ARC Disk Quota Service.
  arc_disk_quota_->Initialize();

  if (!mount_task_runner_) {
    base::Thread::Options options;
    options.message_pump_type = base::MessagePumpType::IO;
    mount_thread_->StartWithOptions(std::move(options));
    mount_task_runner_ = mount_thread_->task_runner();
  }

  if (platform_->FileExists(base::FilePath(kNotFirstBootFilePath))) {
    // Clean up any unreferenced mountpoints at startup.
    PostTaskToMountThread(FROM_HERE,
                          base::BindOnce(
                              [](UserDataAuth* userdataauth) {
                                userdataauth->CleanUpStaleMounts(false);
                              },
                              base::Unretained(this)));
  } else {
    platform_->TouchFileDurable(base::FilePath(kNotFirstBootFilePath));
  }

  low_disk_space_handler_->SetUpdateUserActivityTimestampCallback(
      base::BindRepeating(
          base::IgnoreResult(&UserDataAuth::UpdateCurrentUserActivityTimestamp),
          base::Unretained(this), 0));

  low_disk_space_handler_->SetLowDiskSpaceCallback(
      base::BindRepeating([](uint64_t) {}));

  if (!low_disk_space_handler_->Init(base::BindRepeating(
          &UserDataAuth::PostTaskToMountThread, base::Unretained(this))))
    return false;

  return true;
}

void UserDataAuth::CreateMountThreadDBus() {
  AssertOnMountThread();
  if (!mount_thread_bus_) {
    // Setup the D-Bus.
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mount_thread_bus_ = base::MakeRefCounted<dbus::Bus>(options);
    CHECK(mount_thread_bus_->Connect())
        << "Failed to connect to system D-Bus on mount thread";
  }
}

void UserDataAuth::ShutdownTask() {
  default_challenge_credentials_helper_.reset();
  if (mount_thread_bus_) {
    mount_thread_bus_->ShutdownAndBlock();
    mount_thread_bus_.reset();
  }
}

bool UserDataAuth::PostDBusInitialize() {
  AssertOnOriginThread();
  CHECK(bus_);

  if (!tpm_manager_util_) {
    tpm_manager_util_ = tpm_manager::TpmManagerUtility::GetSingleton();
  }

  if (tpm_manager_util_) {
    tpm_manager_util_->AddOwnershipCallback(base::BindRepeating(
        &UserDataAuth::OnOwnershipTakenSignal, base::Unretained(this)));
  } else {
    LOG(ERROR) << __func__ << ": Failed to get TpmManagerUtility singleton!";
  }

  // Create a dbus connection on mount thread.
  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::CreateMountThreadDBus,
                                       base::Unretained(this)));

  // If the TPM is unowned or doesn't exist, it's safe for
  // this function to be called again. However, it shouldn't
  // be called across multiple threads in parallel.

  PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuth::InitializeInstallAttributes,
                                base::Unretained(this)));

  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::CreateFingerprintManager,
                                       base::Unretained(this)));

  PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuth::CreateUssExperimentConfigFetcher,
                                base::Unretained(this)));

  return true;
}

void UserDataAuth::CreateUssExperimentConfigFetcher() {
  AssertOnMountThread();
  if (!uss_experiment_config_fetcher_) {
    if (!default_uss_experiment_config_fetcher_) {
      default_uss_experiment_config_fetcher_ =
          UssExperimentConfigFetcher::Create(mount_thread_bus_);
    }
    uss_experiment_config_fetcher_ =
        default_uss_experiment_config_fetcher_.get();
  }
}

void UserDataAuth::CreateFingerprintManager() {
  AssertOnMountThread();
  if (!fingerprint_manager_) {
    if (!default_fingerprint_manager_) {
      default_fingerprint_manager_ = FingerprintManager::Create(
          mount_thread_bus_,
          dbus::ObjectPath(std::string(biod::kBiodServicePath)
                               .append(kCrosFpBiometricsManagerRelativePath)));
    }
    fingerprint_manager_ = default_fingerprint_manager_.get();
  }
}

void UserDataAuth::OnOwnershipTakenSignal() {
  PostTaskToMountThread(FROM_HERE,
                        base::BindOnce(&UserDataAuth::OwnershipCallback,
                                       base::Unretained(this), true, true));
}

bool UserDataAuth::PostTaskToOriginThread(const base::Location& from_here,
                                          base::OnceClosure task,
                                          const base::TimeDelta& delay) {
  if (delay.is_zero()) {
    return origin_task_runner_->PostTask(from_here, std::move(task));
  }
  return origin_task_runner_->PostDelayedTask(from_here, std::move(task),
                                              delay);
}

bool UserDataAuth::PostTaskToMountThread(const base::Location& from_here,
                                         base::OnceClosure task,
                                         const base::TimeDelta& delay) {
  CHECK(mount_task_runner_);
  if (delay.is_zero()) {
    // Increase and report the parallel task count.
    parallel_task_count_ += 1;
    if (parallel_task_count_ > 1) {
      ReportParallelTasks(parallel_task_count_);
    }

    // Reduce the parallel task count after finished the task.
    auto full_task = base::BindOnce(
        [](base::OnceClosure task, std::atomic<int>* task_count) {
          std::move(task).Run();
          *task_count -= 1;
        },
        std::move(task), base::Unretained(&parallel_task_count_));

    return mount_task_runner_->PostTask(from_here, std::move(full_task));
  }
  return mount_task_runner_->PostDelayedTask(from_here, std::move(task), delay);
}

bool UserDataAuth::IsMounted(const std::string& username,
                             bool* is_ephemeral_out) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  bool is_mounted = false;
  bool is_ephemeral = false;
  if (username.empty()) {
    // No username is specified, so we consider "the cryptohome" to be mounted
    // if any existing cryptohome is mounted.
    for (const auto& session_pair : sessions_) {
      if (session_pair.second->IsActive()) {
        is_mounted = true;
        is_ephemeral |= session_pair.second->IsEphemeral();
      }
    }
  } else {
    // A username is specified, check the associated mount object.
    scoped_refptr<UserSession> session = GetUserSession(username);

    if (session.get()) {
      is_mounted = session->IsActive();
      is_ephemeral = is_mounted && session->IsEphemeral();
    }
  }

  if (is_ephemeral_out) {
    *is_ephemeral_out = is_ephemeral;
  }

  return is_mounted;
}

scoped_refptr<UserSession> UserDataAuth::GetUserSession(
    const std::string& username) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  scoped_refptr<UserSession> session = nullptr;
  if (sessions_.count(username) == 1) {
    session = sessions_[username];
  }
  return session;
}

bool UserDataAuth::RemoveAllMounts() {
  AssertOnMountThread();

  bool success = true;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    scoped_refptr<UserSession> session = it->second;
    if (session->IsActive() && !session->Unmount()) {
      success = false;
    }
    sessions_.erase(it++);
  }
  return success;
}

bool UserDataAuth::FilterActiveMounts(
    std::multimap<const FilePath, const FilePath>* mounts,
    std::multimap<const FilePath, const FilePath>* active_mounts,
    bool include_busy_mount) {
  // Note: This can only run in mount_thread_
  AssertOnMountThread();

  bool skipped = false;
  std::set<const FilePath> children_to_preserve;

  for (auto match = mounts->begin(); match != mounts->end();) {
    // curr->first is the source device of the group that we are processing in
    // this outer loop.
    auto curr = match;
    bool keep = false;

    // Note that we organize the set of mounts with the same source, then
    // process them together. That is, say there's /dev/mmcblk0p1 mounted on
    // /home/user/xxx and /home/chronos/u-xxx/MyFiles/Downloads. They are both
    // from the same source (/dev/mmcblk0p1, or match->first). In this case,
    // we'll decide the fate of all mounts with the same source together. For
    // each such group, the outer loop will run once. The inner loop will
    // iterate through every mount in the group with |match| variable, looking
    // to see if it's owned by any active mounts. If it is, the entire group is
    // kept. Otherwise, (and assuming no open files), the entire group is
    // discarded, as in, not moved into the active_mounts multimap.

    // Walk each set of sources as one group since multimaps are key ordered.
    for (; match != mounts->end() && match->first == curr->first; ++match) {
      // Ignore known mounts.
      for (const auto& session_pair : sessions_) {
        if (session_pair.second->OwnsMountPoint(match->second)) {
          keep = true;
          // If !include_busy_mount, other mount points not owned scanned after
          // should be preserved as well.
          if (include_busy_mount)
            break;
        }
      }

      // Ignore mounts pointing to children of used mounts.
      if (!include_busy_mount) {
        if (children_to_preserve.find(match->second) !=
            children_to_preserve.end()) {
          keep = true;
          skipped = true;
          LOG(WARNING) << "Stale mount " << match->second.value() << " from "
                       << match->first.value() << " is a just a child.";
        }
      }

      // Optionally, ignore mounts with open files.
      if (!keep && !include_busy_mount) {
        // Mark the mount points that are not in use as 'expired'. Add the mount
        // points to the |active_mounts| list if they are not expired.
        ExpireMountResult expire_mount_result =
            platform_->ExpireMount(match->second);
        if (expire_mount_result == ExpireMountResult::kBusy) {
          LOG(WARNING) << "Stale mount " << match->second.value() << " from "
                       << match->first.value() << " has active holders.";
          keep = true;
          skipped = true;
        } else if (expire_mount_result == ExpireMountResult::kError) {
          // To avoid unloading any pkcs11 token that is in use, add mount point
          // to the |active_mounts| if it is failed to be expired.
          LOG(ERROR) << "Stale mount " << match->second.value() << " from "
                     << match->first.value()
                     << " failed to be removed from active mounts list.";
          keep = true;
          skipped = true;
        }
      }
    }
    if (keep) {
      std::multimap<const FilePath, const FilePath> children;
      LOG(WARNING) << "Looking for children of " << curr->first;
      platform_->GetMountsBySourcePrefix(curr->first, &children);
      for (const auto& child : children) {
        children_to_preserve.insert(child.second);
      }

      active_mounts->insert(curr, match);
      mounts->erase(curr, match);
    }
  }
  return skipped;
}

void UserDataAuth::GetEphemeralLoopDevicesMounts(
    std::multimap<const FilePath, const FilePath>* mounts) {
  AssertOnMountThread();
  std::multimap<const FilePath, const FilePath> loop_mounts;
  platform_->GetLoopDeviceMounts(&loop_mounts);

  const FilePath sparse_path =
      FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir);
  for (const auto& device : platform_->GetAttachedLoopDevices()) {
    // Ephemeral mounts are mounts from a loop device with ephemeral sparse
    // backing file.
    if (sparse_path.IsParent(device.backing_file)) {
      auto range = loop_mounts.equal_range(device.device);
      mounts->insert(range.first, range.second);
    }
  }
}

bool UserDataAuth::UnloadPkcs11Tokens(const std::vector<FilePath>& exclude) {
  AssertOnMountThread();

  SecureBlob isolate =
      chaps::IsolateCredentialManager::GetDefaultIsolateCredential();
  std::vector<std::string> tokens;
  if (!chaps_client_->GetTokenList(isolate, &tokens))
    return false;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] != chaps::kSystemTokenPath &&
        !PrefixPresent(exclude, tokens[i])) {
      // It's not a system token and is not under one of the excluded path.
      LOG(INFO) << "Unloading up PKCS #11 token: " << tokens[i];
      chaps_client_->UnloadToken(isolate, FilePath(tokens[i]));
    }
  }
  return true;
}

bool UserDataAuth::CleanUpStaleMounts(bool force) {
  AssertOnMountThread();

  // This function is meant to aid in a clean recovery from a crashed or
  // manually restarted cryptohomed.  Cryptohomed may restart:
  // 1. Before any mounts occur
  // 2. While mounts are active
  // 3. During an unmount
  // In case #1, there should be no special work to be done.
  // The best way to disambiguate #2 and #3 is to determine if there are
  // any active open files on any stale mounts.  If there are open files,
  // then we've likely(*) resumed an active session. If there are not,
  // the last cryptohome should have been unmounted.
  // It's worth noting that a restart during active use doesn't impair
  // other user session behavior, like CheckKey, because it doesn't rely
  // exclusively on mount state.
  //
  // In the future, it may make sense to attempt to keep the MountMap
  // persisted to disk which would make resumption much easier.
  //
  // (*) Relies on the expectation that all processes have been killed off.

  // TODO(b:225769250, dlunev): figure out cleanup for non-mounted application
  // containers.

  // Stale shadow and ephemeral mounts.
  std::multimap<const FilePath, const FilePath> shadow_mounts;
  std::multimap<const FilePath, const FilePath> ephemeral_mounts;
  std::multimap<const FilePath, const FilePath> dmcrypt_mounts,
      grouped_dmcrypt_mounts;

  // Active mounts that we don't intend to unmount.
  std::multimap<const FilePath, const FilePath> active_mounts;

  // Retrieve all the mounts that's currently mounted by the kernel and concerns
  // us
  platform_->GetMountsBySourcePrefix(ShadowRoot(), &shadow_mounts);
  platform_->GetMountsByDevicePrefix(kDeviceMapperDevicePrefix,
                                     &dmcrypt_mounts);
  GroupDmcryptDeviceMounts(&dmcrypt_mounts, &grouped_dmcrypt_mounts);
  GetEphemeralLoopDevicesMounts(&ephemeral_mounts);

  // Remove mounts that we've a record of or have open files on them
  bool skipped =
      FilterActiveMounts(&shadow_mounts, &active_mounts, force) ||
      FilterActiveMounts(&ephemeral_mounts, &active_mounts, force) ||
      FilterActiveMounts(&grouped_dmcrypt_mounts, &active_mounts, force);

  // Unload PKCS#11 tokens on any mount that we're going to unmount.
  std::vector<FilePath> excluded_mount_points;
  for (const auto& mount : active_mounts) {
    excluded_mount_points.push_back(mount.second);
  }
  UnloadPkcs11Tokens(excluded_mount_points);

  // Unmount anything left.
  for (const auto& match : grouped_dmcrypt_mounts) {
    LOG(WARNING) << "Lazily unmounting stale dmcrypt mount: "
                 << match.second.value() << " for " << match.first.value();
    // true for lazy unmount, nullptr for us not needing to know if it's really
    // unmounted.
    platform_->Unmount(match.second, true, nullptr);
  }

  for (const auto& match : shadow_mounts) {
    LOG(WARNING) << "Lazily unmounting stale shadow mount: "
                 << match.second.value() << " from " << match.first.value();
    // true for lazy unmount, nullptr for us not needing to know if it's really
    // unmounted.
    platform_->Unmount(match.second, true, nullptr);
  }

  // Attempt to clear the encryption key for the shadow directories once
  // the mount has been unmounted. The encryption key needs to be cleared
  // after all the unmounts are done to ensure that none of the existing
  // submounts becomes inaccessible.
  if (force && !shadow_mounts.empty()) {
    // Attempt to clear fscrypt encryption keys for the shadow mounts.
    for (const auto& match : shadow_mounts) {
      if (!platform_->InvalidateDirCryptoKey(dircrypto::KeyReference(),
                                             match.first)) {
        LOG(WARNING) << "Failed to clear fscrypt keys for stale mount: "
                     << match.first;
      }
    }

    // Clear all keys in the user keyring for ecryptfs mounts.
    if (!platform_->ClearUserKeyring()) {
      LOG(WARNING) << "Failed to clear stale user keys.";
    }
  }
  for (const auto& match : ephemeral_mounts) {
    LOG(WARNING) << "Lazily unmounting stale ephemeral mount: "
                 << match.second.value() << " from " << match.first.value();
    // true for lazy unmount, nullptr for us not needing to know if it's really
    // unmounted.
    platform_->Unmount(match.second, true, nullptr);
    // Clean up destination directory for ephemeral mounts under ephemeral
    // cryptohome dir.
    if (base::StartsWith(match.first.value(), kLoopPrefix,
                         base::CompareCase::SENSITIVE) &&
        FilePath(kEphemeralCryptohomeDir).IsParent(match.second)) {
      platform_->DeletePathRecursively(match.second);
    }
  }

  // Clean up all stale sparse files, this is comprised of two stages:
  // 1. Clean up stale loop devices.
  // 2. Clean up stale sparse files.
  // Note that some mounts are backed by loop devices, and loop devices are
  // backed by sparse files.

  std::vector<Platform::LoopDevice> loop_devices =
      platform_->GetAttachedLoopDevices();
  const FilePath sparse_dir =
      FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir);
  std::vector<FilePath> stale_sparse_files;
  platform_->EnumerateDirectoryEntries(sparse_dir, false /* is_recursive */,
                                       &stale_sparse_files);

  // We'll go through all loop devices, and for every of them, we'll see if we
  // can remove it. Also in the process, we'll get to keep track of which sparse
  // files are actually used by active loop devices.
  for (const auto& device : loop_devices) {
    // Check whether the loop device is created from an ephemeral sparse file.
    if (!sparse_dir.IsParent(device.backing_file)) {
      // Nah, it's this loop device is not backed by an ephemeral sparse file
      // created by cryptohome, so we'll leave it alone.
      continue;
    }

    // Check if any of our active mounts are backed by this loop device.
    if (active_mounts.count(device.device) == 0) {
      // Nope, this loop device have nothing to do with our active mounts.
      LOG(WARNING) << "Detaching stale loop device: " << device.device.value();
      if (!platform_->DetachLoop(device.device)) {
        ReportCryptohomeError(kEphemeralCleanUpFailed);
        PLOG(ERROR) << "Can't detach stale loop: " << device.device.value();
      }
    } else {
      // This loop device backs one of our active_mounts, so we can't count it
      // as stale. Thus removing from the stale_sparse_files list.
      stale_sparse_files.erase(
          std::remove(stale_sparse_files.begin(), stale_sparse_files.end(),
                      device.backing_file),
          stale_sparse_files.end());
    }
  }

  // Now we clean up the stale sparse files.
  for (const auto& file : stale_sparse_files) {
    LOG(WARNING) << "Deleting stale ephemeral backing sparse file: "
                 << file.value();
    if (!platform_->DeleteFile(file)) {
      ReportCryptohomeError(kEphemeralCleanUpFailed);
      PLOG(ERROR) << "Failed to clean up ephemeral sparse file: "
                  << file.value();
    }
  }

  // |force| and |skipped| cannot be true at the same time. If |force| is true,
  // then we'll not skip over any stale mount because there are open files, so
  // |skipped| must be false.
  DCHECK(!(force && skipped));

  return skipped;
}

user_data_auth::UnmountReply UserDataAuth::Unmount() {
  AssertOnMountThread();

  bool unmount_ok = RemoveAllMounts();

  // If there are any unexpected mounts lingering from a crash/restart,
  // clean them up now.
  // Note that we do not care about the return value of CleanUpStaleMounts()
  // because it doesn't matter if any mount is skipped due to open files, and
  // additionally, since we've specified force=true, it'll not skip over mounts
  // with open files.
  CleanUpStaleMounts(true);

  if (homedirs_->AreEphemeralUsersEnabled()) {
    homedirs_->RemoveNonOwnerCryptohomes();
  }

  CryptohomeStatus result;
  if (!unmount_ok) {
    result = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthRemoveAllMountsFailedInUnmount),
        ErrorActionSet({ErrorAction::kReboot}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }
  user_data_auth::UnmountReply reply;
  PopulateReplyWithError(result, &reply);
  return reply;
}

void UserDataAuth::InitializePkcs11(UserSession* session) {
  AssertOnMountThread();

  // We should not pass nullptr to this method.
  DCHECK(session);

  bool still_mounted = false;

  // The mount has to be mounted, that is, still tracked by cryptohome.
  // Otherwise there's no point in initializing PKCS#11 for it. The reason for
  // this check is because it might be possible for Unmount() to be called after
  // mounting and before getting here.
  for (const auto& session_pair : sessions_) {
    if (session_pair.second.get() == session && session->IsActive()) {
      still_mounted = true;
      break;
    }
  }

  if (!still_mounted) {
    LOG(WARNING)
        << "PKCS#11 initialization requested but cryptohome is not mounted.";
    return;
  }

  // Note that the timer stops in the Mount class' method.
  ReportTimerStart(kPkcs11InitTimer);

  if (session->GetPkcs11Token()) {
    session->GetPkcs11Token()->Insert();
  }

  ReportTimerStop(kPkcs11InitTimer);

  LOG(INFO) << "PKCS#11 initialization succeeded.";
}

void UserDataAuth::Pkcs11RestoreTpmTokens() {
  AssertOnMountThread();

  for (auto& session_pair : sessions_) {
    scoped_refptr<UserSession> session = session_pair.second;
    InitializePkcs11(session.get());
  }
}

void UserDataAuth::EnsureCryptohomeKeys() {
  if (!IsOnMountThread()) {
    // We are not on mount thread, but to be safe, we'll only access Mount
    // objects on mount thread, so let's post ourself there.
    PostTaskToMountThread(FROM_HERE,
                          base::BindOnce(&UserDataAuth::EnsureCryptohomeKeys,
                                         base::Unretained(this)));
    return;
  }

  AssertOnMountThread();

  if (!cryptohome_keys_manager_->HasAnyCryptohomeKey()) {
    cryptohome_keys_manager_->Init();
  }
}

void UserDataAuth::set_cleanup_threshold(uint64_t cleanup_threshold) {
  disk_cleanup_threshold_ = cleanup_threshold;
}

void UserDataAuth::set_aggressive_cleanup_threshold(
    uint64_t aggressive_cleanup_threshold) {
  disk_cleanup_aggressive_threshold_ = aggressive_cleanup_threshold;
}

void UserDataAuth::set_critical_cleanup_threshold(
    uint64_t critical_cleanup_threshold) {
  disk_cleanup_critical_threshold_ = critical_cleanup_threshold;
}

void UserDataAuth::set_target_free_space(uint64_t target_free_space) {
  disk_cleanup_target_free_space_ = target_free_space;
}

void UserDataAuth::SetLowDiskSpaceCallback(
    const base::RepeatingCallback<void(uint64_t)>& callback) {
  low_disk_space_handler_->SetLowDiskSpaceCallback(callback);
}

void UserDataAuth::OwnershipCallback(bool status, bool took_ownership) {
  AssertOnMountThread();

  // Note that this function should only be called once during the lifetime of
  // this process, extra calls will be dropped.
  if (ownership_callback_has_run_) {
    LOG(WARNING) << "Duplicated call to OwnershipCallback.";
    return;
  }
  ownership_callback_has_run_ = true;

  if (took_ownership) {
    // Make sure cryptohome keys are loaded and ready for every mount.
    EnsureCryptohomeKeys();

    // Initialize the install-time locked attributes since we can't do it prior
    // to ownership.
    InitializeInstallAttributes();
  }
}

void UserDataAuth::SetEnterpriseOwned(bool enterprise_owned) {
  AssertOnMountThread();

  enterprise_owned_ = enterprise_owned;
  homedirs_->set_enterprise_owned(enterprise_owned);
}

void UserDataAuth::DetectEnterpriseOwnership() {
  AssertOnMountThread();

  static const std::string true_str = "true";
  brillo::Blob true_value(true_str.begin(), true_str.end());
  true_value.push_back(0);

  brillo::Blob value;
  if (install_attrs_->Get("enterprise.owned", &value) && value == true_value) {
    // Update any active mounts with the state, have to be done on mount thread.
    SetEnterpriseOwned(true);
  }
  // Note: Right now there's no way to convert an enterprise owned machine to a
  // non-enterprise owned machine without clearing the TPM, so we don't try
  // calling SetEnterpriseOwned() with false.
}

void UserDataAuth::InitializeInstallAttributes() {
  AssertOnMountThread();

  // Don't reinitialize when install attributes are valid or first install.
  if (install_attrs_->status() == InstallAttributes::Status::kValid ||
      install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    return;
  }

  // The TPM owning instance may have changed since initialization.
  // InstallAttributes can handle a NULL or !IsEnabled Tpm object.
  install_attrs_->Init();

  // Check if the machine is enterprise owned and report to mount_ then.
  DetectEnterpriseOwnership();
}

CryptohomeStatusOr<bool> UserDataAuth::GetShouldMountAsEphemeral(
    const std::string& account_id,
    bool is_ephemeral_mount_requested,
    bool has_create_request) const {
  AssertOnMountThread();
  const bool is_or_will_be_owner = homedirs_->IsOrWillBeOwner(account_id);
  if (is_ephemeral_mount_requested && is_or_will_be_owner) {
    LOG(ERROR) << "An ephemeral cryptohome can only be mounted when the user "
                  "is not the owner.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoEphemeralMountForOwner),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }
  bool is_ephemeral =
      !is_or_will_be_owner &&
      (homedirs_->AreEphemeralUsersEnabled() || is_ephemeral_mount_requested);
  if (is_ephemeral && !has_create_request) {
    LOG(ERROR) << "An ephemeral cryptohome can only be mounted when its "
                  "creation on-the-fly is allowed.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthEphemeralMountWithoutCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
  }
  return is_ephemeral;
}

void UserDataAuth::EnsureBootLockboxFinalized() {
  AssertOnMountThread();

  // Lock NVRamBootLockbox
  auto nvram_boot_lockbox_client = BootLockboxClient::CreateBootLockboxClient();
  if (!nvram_boot_lockbox_client) {
    LOG(WARNING) << "Failed to create nvram_boot_lockbox_client";
    return;
  }

  if (!nvram_boot_lockbox_client->Finalize()) {
    LOG(WARNING) << "Failed to finalize nvram lockbox.";
  }
}

// TODO(b/172344610, dlunev): abstract user_session through factory/manager.
scoped_refptr<UserSession> UserDataAuth::GetOrCreateUserSession(
    const std::string& username) {
  // This method touches the |sessions_| object so it needs to run on
  // |mount_thread_|
  AssertOnMountThread();
  if (sessions_.count(username) == 0U) {
    // We don't have a mount associated with |username|, let's create one.
    EnsureBootLockboxFinalized();
    sessions_[username] = user_session_factory_->New(username, legacy_mount_,
                                                     bind_mount_downloads_);
  }
  return sessions_[username];
}

void UserDataAuth::MountGuest(
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();

  if (sessions_.size() != 0) {
    LOG(WARNING) << "Guest mount requested with other sessions active.";
  }
  // Rather than make it safe to check the size, then clean up, just always
  // clean up.
  bool ok = RemoveAllMounts();
  user_data_auth::MountReply reply;
  // Provide an authoritative filesystem-sanitized username.
  reply.set_sanitized_username(SanitizeUserName(guest_user_));
  if (!ok) {
    LOG(ERROR) << "Could not unmount cryptohomes for Guest use";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountGuestMountPointBusy),
            ErrorActionSet({ErrorAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
    return;
  }
  ReportTimerStart(kMountGuestExTimer);

  CryptohomeStatus status;

  // Create a ref-counted guest mount for async use and then throw it away.
  scoped_refptr<UserSession> guest_session =
      GetOrCreateUserSession(guest_user_);
  if (!guest_session) {
    LOG(ERROR) << "Failed to create guest session.";
    // This should not happen.
    status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountGuestNoGuestSession),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  } else {
    MountStatus guest_status = guest_session->MountGuest();
    if (!guest_status.ok()) {
      LOG(ERROR) << "Could not initialize guest session: " << guest_status;
      status =
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountGuestSessionMountFailed),
              ErrorActionSet({ErrorAction::kReboot}),
              user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL)
              .Wrap(std::move(guest_status));
    }
  }

  if (!status.ok()) {
    // We only report the guest mount time for successful cases.
    ReportTimerStop(kMountGuestExTimer);
  }

  // TODO(b/137073669): Cleanup guest_mount if mount failed.
  ReplyWithError(std::move(on_done), reply, std::move(status));
}

void UserDataAuth::DoMount(
    user_data_auth::MountRequest request,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Received a mount request.";

  // DoMount current supports guest login/mount, normal plaintext password login
  // and challenge response login. For guest mount, a special process
  // (MountGuest()) is used. Meanwhile, for normal plaintext password login and
  // challenge response login, both will flow through this method. This method
  // generally does some parameter validity checking, then pass the request onto
  // ContinueMountWithCredentials() for plaintext password login and
  // DoChallengeResponseMount() for challenge response login.
  // DoChallengeResponseMount() will contact a dbus service and transmit the
  // challenge, and once the response is received and checked with the TPM,
  // it'll pass the request to ContinueMountWithCredentials(), which is the same
  // as password login case, and in ContinueMountWithCredentials(), the mount is
  // actually mounted through system call.

  // Check for guest mount case.
  if (request.guest_mount()) {
    MountGuest(std::move(on_done));
    return;
  }

  user_data_auth::MountReply reply;

  // At present, we only enforce non-empty email addresses.
  // In the future, we may wish to canonicalize if we don't move
  // to requiring a IdP-unique identifier.
  const std::string& account_id = GetAccountId(request.account());

  // AuthSession associated with this request's auth_session_id. Can be empty
  // in case auth_session_id is not supplied.
  AuthSession* auth_session = nullptr;

  if (!request.auth_session_id().empty()) {
    auth_session =
        auth_session_manager_->FindAuthSession(request.auth_session_id());
    if (!auth_session) {
      LOG(ERROR) << "Invalid AuthSession token provided.";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountAuthSessionNotFound),
              ErrorActionSet({ErrorAction::kReboot}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));
      return;
    }
    if (auth_session->GetStatus() != AuthStatus::kAuthStatusAuthenticated) {
      reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
      LOG(ERROR) << "AuthSession is not authenticated";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountAuthSessionNotAuthed),
              ErrorActionSet({ErrorAction::kReboot,
                              ErrorAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return;
    }
  }

  // Check for empty account ID
  if (account_id.empty() && !auth_session) {
    LOG(ERROR) << "No email supplied";
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountNoAccountID),
                       ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Key generation is not needed if there is a valid AuthSession as part of the
  // request. Key generation is handled in AuthSession.
  if (request.public_mount() && !auth_session) {
    // Public mount have a set of passkey/password that is generated directly
    // from the username (and a local system salt.)
    brillo::SecureBlob public_mount_passkey =
        keyset_management_->GetPublicMountPassKey(account_id);
    if (public_mount_passkey.empty()) {
      LOG(ERROR) << "Could not get public mount passkey.";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountCantGetPublicMountSalt),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
      return;
    }

    // Set the secret as the key for cryptohome authorization/creation.
    request.mutable_authorization()->mutable_key()->set_secret(
        public_mount_passkey.to_string());
    if (request.has_create()) {
      request.mutable_create()->mutable_keys(0)->set_secret(
          public_mount_passkey.to_string());
    }
  }

  // We do not allow empty password, except for challenge response type login.
  if (request.authorization().key().secret().empty() &&
      request.authorization().key().data().type() !=
          KeyData::KEY_TYPE_CHALLENGE_RESPONSE &&
      !auth_session) {
    LOG(ERROR) << "No key secret supplied";
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountNoKeySecret),
                       ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  if (request.has_create() && !auth_session) {
    // copy_authorization_key in CreateRequest means that we'll copy the
    // authorization request's key and use it as if it's the key specified in
    // CreateRequest.
    if (request.create().copy_authorization_key()) {
      Key* auth_key = request.mutable_create()->add_keys();
      *auth_key = request.authorization().key();
    }

    // Validity check for |request.create.keys|.
    int keys_size = request.create().keys_size();
    if (keys_size == 0) {
      LOG(ERROR) << "CreateRequest supplied with no keys";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountCreateNoKey),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return;
    } else if (keys_size > 1) {
      LOG(ERROR) << "MountEx: unimplemented CreateRequest with multiple keys";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountCreateMultipleKey),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
      return;
    } else {
      const Key key = request.create().keys(0);
      // TODO(wad) Ensure the labels are all unique.
      if (!key.has_data() || key.data().label().empty() ||
          (key.secret().empty() &&
           key.data().type() != KeyData::KEY_TYPE_CHALLENGE_RESPONSE)) {
        LOG(ERROR) << "CreateRequest Keys are not fully specified";
        ReplyWithError(
            std::move(on_done), reply,
            MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(kLocUserDataAuthMountCreateKeyNotSpecified),
                ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
                user_data_auth::CryptohomeErrorCode::
                    CRYPTOHOME_ERROR_INVALID_ARGUMENT));
        return;
      }
    }
  }

  // Determine whether the mount should be ephemeral.
  bool is_ephemeral = false;
  bool require_ephemeral =
      request.require_ephemeral() ||
      (auth_session ? auth_session->ephemeral_user() : false);

  CryptohomeStatusOr<bool> should_mount_as_ephemeral_status =
      GetShouldMountAsEphemeral(account_id, require_ephemeral,
                                (request.has_create() || auth_session));
  if (!should_mount_as_ephemeral_status.ok()) {
    ReplyWithError(std::move(on_done), reply,
                   std::move(should_mount_as_ephemeral_status.status()));
    return;
  }
  is_ephemeral = should_mount_as_ephemeral_status.value();

  // TODO(b/230069013): We want to collect metrics about USS experiment status
  // before we launch USS. Metrics are reported when we checked the USS
  // experiment flag, but it's currently only checked in AuthSession when new
  // user is created, which is not called by Chrome yet. This place roughly
  // represents the moment when crypthome creates a new user vault (if
  // request.has_create() is true), so check the USS experiment flag and report
  // the metrics here.
  if (request.has_create()) {
    IsUserSecretStashExperimentEnabled();
  }

  // MountArgs is a set of parameters that we'll be passing around to
  // ContinueMountWithCredentials() and DoChallengeResponseMount().
  UserDataAuth::MountArgs mount_args;

  // request.has_create() represents a CreateRequest, telling the API to
  // create a user with the credentials in CreateRequest. create_if_missing
  // creates a user mount should one not exist. In the legacy use case,
  // CreateRequest needs to requested in the Mount call API for user creation.
  // When AuthSessions are fully functional with mount call, we would not be
  // creating user directories in mount call, instead we'd use
  // CreateEphemeral. But for now, code paths such as ephemeral mounts
  // require create_if_missing to be set to true to continue mounting as
  // Ephemeral user directories are created here.
  // Therefore, if a valid and an authenticated AuthSession is passed we
  // can temporarily bypass create_if_missing as a first step to prevent
  // credentials from flowing to mount call. Later, this would be replaced by
  // CreateEphemeral, CreatePersistent calls.
  mount_args.create_if_missing = (request.has_create() || auth_session);
  mount_args.is_ephemeral = is_ephemeral;
  mount_args.create_as_ecryptfs =
      force_ecryptfs_ ||
      (request.has_create() && request.create().force_ecryptfs());
  mount_args.to_migrate_from_ecryptfs = request.to_migrate_from_ecryptfs();
  // Force_ecryptfs_ wins.
  mount_args.force_dircrypto =
      !force_ecryptfs_ && request.force_dircrypto_if_available();

  // Process challenge-response credentials asynchronously.
  if ((request.authorization().key().data().type() ==
       KeyData::KEY_TYPE_CHALLENGE_RESPONSE) &&
      !auth_session) {
    DoChallengeResponseMount(request, mount_args, std::move(on_done));
    return;
  }

  auto credentials = std::make_unique<Credentials>(
      account_id, SecureBlob(request.authorization().key().secret()));
  // Everything else can be the default.
  credentials->set_key_data(request.authorization().key().data());

  std::optional<base::UnguessableToken> token = std::nullopt;
  if (auth_session) {
    token = auth_session->token();
  }

  ContinueMountWithCredentials(request, std::move(credentials), token,
                               mount_args, std::move(on_done));
  LOG(INFO) << "Finished mount request process";
}

CryptohomeStatus UserDataAuth::InitForChallengeResponseAuth() {
  AssertOnMountThread();
  if (challenge_credentials_helper_) {
    // Already successfully initialized.
    return OkStatus<CryptohomeError>();
  }

  hwsec::StatusOr<bool> is_ready = hwsec_->IsReady();
  if (!is_ready.ok()) {
    LOG(ERROR) << "Failed to get the hwsec ready state: " << is_ready.status();
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthHwsecNotReadyInInitChalRespAuth),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kFatal}),
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  if (!is_ready.value()) {
    LOG(ERROR) << "HWSec must be initialized in order to do challenge-response "
                  "authentication";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthTPMNotReadyInInitChalRespAuth),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  // Fail if the security chip is known to be vulnerable and we're not in a test
  // image.
  hwsec::StatusOr<bool> is_srk_roca_vulnerable = hwsec_->IsSrkRocaVulnerable();
  if (!is_srk_roca_vulnerable.ok()) {
    LOG(ERROR) << "Failed to get the hwsec SRK ROCA vulnerable status: "
               << is_srk_roca_vulnerable.status();
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthCantQueryROCAVulnInInitChalRespAuth),
        ErrorActionSet({ErrorAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  if (is_srk_roca_vulnerable.value()) {
    if (!IsOsTestImage()) {
      LOG(ERROR)
          << "Cannot do challenge-response mount: HWSec is ROCA vulnerable";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocUserDataAuthROCAVulnerableInInitChalRespAuth),
          ErrorActionSet({ErrorAction::kTpmUpdateRequired}),
          user_data_auth::CRYPTOHOME_ERROR_TPM_UPDATE_REQUIRED);
    }
    LOG(WARNING) << "HWSec is ROCA vulnerable; ignoring this for "
                    "challenge-response mount due to running in test image";
  }

  if (!mount_thread_bus_) {
    LOG(ERROR) << "Cannot do challenge-response mount without system D-Bus bus";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoDBusInInitChalRespAuth),
        ErrorActionSet(
            {ErrorAction::kReboot, ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  // Lazily create the helper object that manages generation/decryption of
  // credentials for challenge-protected vaults.

  default_challenge_credentials_helper_ =
      std::make_unique<ChallengeCredentialsHelperImpl>(hwsec_);
  challenge_credentials_helper_ = default_challenge_credentials_helper_.get();
  auth_block_utility_->InitializeForChallengeCredentials(
      challenge_credentials_helper_);
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus UserDataAuth::InitAuthBlockUtilityForChallengeResponse(
    const AuthorizationRequest& authorization, const std::string& username) {
  // challenge_credential_helper_ must initialized to process
  // AuthBlockType::kChallengeCredential.
  // Update AuthBlockUtility with challenge_credentials_helper_.
  CryptohomeStatus status = InitForChallengeResponseAuth();
  if (!status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthInitFailedInInitAuthBlockUtilChalResp))
        .Wrap(std::move(status));
  }

  if (!authorization.has_key_delegate() ||
      !authorization.key_delegate().has_dbus_service_name()) {
    LOG(ERROR) << "Cannot do challenge-response authentication without key "
                  "delegate information";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthNoDelegateInInitAuthBlockUtilChalResp),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }
  if (!authorization.key().data().challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthNokeyInfoInInitAuthBlockUtilChalResp),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }
  if (authorization.key().data().challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthMultipleKeysInInitAuthBlockUtilChalResp),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  // KeyChallengeService is tasked with contacting the challenge response
  // D-Bus service that'll provide the response once we send the challenge.
  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          mount_thread_bus_, authorization.key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthCreateFailedInInitAuthBlockUtilChalResp),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }
  auth_block_utility_->SetSingleUseKeyChallengeService(
      std::move(key_challenge_service), username);
  return OkStatus<CryptohomeError>();
}

void UserDataAuth::DoChallengeResponseMount(
    const user_data_auth::MountRequest& request,
    const UserDataAuth::MountArgs& mount_args,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();
  DCHECK_EQ(request.authorization().key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  // Setup a reply for use during error handling.
  user_data_auth::MountReply reply;

  CryptohomeStatus status = InitForChallengeResponseAuth();
  if (!status.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthInitChalRespAuthFailedInDoChalRespMount))
            .Wrap(std::move(status)));
    return;
  }

  const std::string& account_id = GetAccountId(request.account());
  const std::string obfuscated_username = SanitizeUserName(account_id);
  const KeyData key_data = request.authorization().key().data();

  if (!key_data.challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthNoChalRespKeyInfoInDoChalRespMount),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL));
    return;
  }

  if (key_data.challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthMultipleKeysInDoChalRespMount),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL));
    return;
  }

  const ChallengePublicKeyInfo& public_key_info =
      key_data.challenge_response_key(0);

  if (!request.authorization().has_key_delegate() ||
      !request.authorization().key_delegate().has_dbus_service_name()) {
    LOG(ERROR) << "Cannot do challenge-response mount without key delegate "
                  "information";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoDelegateInDoChalRespMount),
            ErrorActionSet({ErrorAction::kPowerwash, ErrorAction::kAuth}),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL));
    return;
  }

  // KeyChallengeService is tasked with contacting the challenge response D-Bus
  // service that'll provide the response once we send the challenge.
  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          mount_thread_bus_,
          request.authorization().key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthNoChalRespServiceInDoChalRespMount),
            ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth,
                            ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL));
    return;
  }

  if (!homedirs_->Exists(obfuscated_username) &&
      !mount_args.create_if_missing) {
    LOG(ERROR) << "Cannot do challenge-response mount. Account not found.";
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthAccountNotFoundInDoChalRespMount),
                       ErrorActionSet({ErrorAction::kCreateRequired}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND));
    return;
  }

  std::unique_ptr<VaultKeyset> vault_keyset(keyset_management_->GetVaultKeyset(
      obfuscated_username, request.authorization().key().data().label()));
  const bool use_existing_credentials =
      vault_keyset && !mount_args.is_ephemeral;
  // If the home directory already exist (and thus the corresponding encrypted
  // VaultKeyset exists) and the mount is not ephemeral, then we'll use the
  // ChallengeCredentialsHelper (which handles challenge response
  // authentication) to decrypt the VaultKeyset.
  if (use_existing_credentials && vault_keyset->HasSignatureChallengeInfo()) {
    // Home directory already exist and we are not doing ephemeral mount, so
    // we'll decrypt existing VaultKeyset.

    // Note: We don't need the |signature_challenge_info| when we are decrypting
    // the challenge credential, because the keyset managerment doesn't need to
    // read the |signature_challenge_info| from the credentials in this case.
    // This behavior would eventually be replaced by the asynchronous challenge
    // credential auth block, we can get rid of the |signature_challenge_info|
    // from the credentials after we move it into the auth block state.
    challenge_credentials_helper_->Decrypt(
        account_id, proto::FromProto(public_key_info),
        proto::FromProto(vault_keyset->GetSignatureChallengeInfo()),
        std::move(key_challenge_service),
        base::BindOnce(
            &UserDataAuth::OnChallengeResponseMountCredentialsObtained,
            base::Unretained(this), request, mount_args, std::move(on_done)));
  } else {
    // We'll create a new VaultKeyset that accepts challenge response
    // authentication.
    if (!mount_args.create_if_missing) {
      LOG(ERROR) << "No existing challenge-response vault keyset found";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoChalRespVKInDoChalRespMount),
              ErrorActionSet(
                  {ErrorAction::kAuth, ErrorAction::kDevCheckUnexpectedState}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_MOUNT_FATAL));
      return;
    }

    challenge_credentials_helper_->GenerateNew(
        account_id, proto::FromProto(public_key_info), obfuscated_username,
        std::move(key_challenge_service),
        base::BindOnce(
            &UserDataAuth::OnChallengeResponseMountCredentialsObtained,
            base::Unretained(this), request, mount_args, std::move(on_done)));
  }
}

void UserDataAuth::OnChallengeResponseMountCredentialsObtained(
    const user_data_auth::MountRequest& request,
    const UserDataAuth::MountArgs mount_args,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done,
    TPMStatusOr<ChallengeCredentialsHelper::GenerateNewOrDecryptResult>
        result) {
  AssertOnMountThread();
  // If we get here, that means the ChallengeCredentialsHelper have finished the
  // process of doing challenge response authentication, either successful or
  // otherwise.

  // Setup a reply for use during error handling.
  user_data_auth::MountReply reply;

  DCHECK_EQ(request.authorization().key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  if (!result.ok()) {
    // Challenge response authentication have failed.
    LOG(ERROR) << "Could not mount due to failure to obtain challenge-response "
                  "credentials";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthChalCredFailedInChalRespMount))
            .Wrap(std::move(result).status()));
    return;
  }

  ChallengeCredentialsHelper::GenerateNewOrDecryptResult result_val =
      std::move(result).value();
  std::unique_ptr<brillo::SecureBlob> passkey = result_val.passkey();
  std::unique_ptr<structure::SignatureChallengeInfo> signature_challenge_info =
      result_val.info();

  const std::string& account_id = GetAccountId(request.account());
  auto credentials = std::make_unique<Credentials>(account_id, *passkey);
  credentials->set_key_data(request.authorization().key().data());

  if (signature_challenge_info != nullptr) {
    credentials->set_challenge_credentials_keyset_info(
        proto::ToProto(*signature_challenge_info));
  }

  DCHECK_EQ(credentials->key_data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  ContinueMountWithCredentials(request, std::move(credentials), std::nullopt,
                               mount_args, std::move(on_done));
}

void UserDataAuth::ContinueMountWithCredentials(
    const user_data_auth::MountRequest& request,
    std::unique_ptr<Credentials> credentials,
    std::optional<base::UnguessableToken> token,
    const UserDataAuth::MountArgs& mount_args,
    base::OnceCallback<void(const user_data_auth::MountReply&)> on_done) {
  AssertOnMountThread();

  AuthSession* auth_session =
      token.has_value() ? auth_session_manager_->FindAuthSession(token.value())
                        : nullptr;

  // Setup a reply for use during error handling.
  user_data_auth::MountReply reply;
  std::string obfuscated_username = credentials->GetObfuscatedUsername();
  // This is safe even if cryptohomed restarts during a multi-mount
  // session and a new mount is added because cleanup is not forced.
  // An existing process will keep the mount alive.  On the next
  // Unmount() it'll be forcibly cleaned up.  In the case that
  // cryptohomed crashes and misses the Unmount call, the stale
  // mountpoints should still be cleaned up on the next daemon
  // interaction.
  //
  // As we introduce multiple mounts, we can consider API changes to
  // make it clearer what the UI expectations are (AddMount, etc).
  bool other_sessions_active = true;
  if (sessions_.size() == 0) {
    other_sessions_active = CleanUpStaleMounts(false);
    // This could run on every interaction to catch any unused mounts.
  }

  // If the home directory for our user doesn't exist and we aren't instructed
  // to create the home directory, and reply with the error.
  if (!request.has_create() && !homedirs_->Exists(obfuscated_username) &&
      !token.has_value()) {
    LOG(ERROR) << "Account not found when mounting with credentials.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthAccountNotFoundInContinueMountWithCred),
            ErrorActionSet({ErrorAction::kCreateRequired}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND));
    return;
  }

  std::string account_id =
      auth_session ? auth_session->username() : GetAccountId(request.account());
  // Provide an authoritative filesystem-sanitized username.
  reply.set_sanitized_username(
      brillo::cryptohome::home::SanitizeUserName(account_id));

  // Check if the guest user is mounted, if it is, we can't proceed.
  scoped_refptr<UserSession> guest_session = GetUserSession(guest_user_);
  bool guest_mounted = guest_session.get() && guest_session->IsActive();
  // TODO(wad,ellyjones) Change this behavior to return failure even
  // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
  if (guest_mounted && !guest_session->Unmount()) {
    LOG(ERROR) << "Could not unmount cryptohome from Guest session";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthGuestMountPointBusyInContinueMountWithCred),
            ErrorActionSet({ErrorAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
    return;
  }

  scoped_refptr<UserSession> user_session = GetOrCreateUserSession(account_id);

  if (!user_session) {
    LOG(ERROR) << "Could not initialize user session.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthCantCreateSessionInContinueMountWithCred),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL));
    return;
  }

  // For public mount, don't proceed if there is any existing mount or stale
  // mount. Exceptionally, it is normal and ok to have a failed previous mount
  // attempt for the same user.
  const bool only_self_unmounted_attempt =
      sessions_.size() == 1 && !user_session->IsActive();
  if (request.public_mount() && other_sessions_active &&
      !only_self_unmounted_attempt) {
    LOG(ERROR) << "Public mount requested with other sessions active.";
    if (!request.auth_session_id().empty()) {
      std::string obfuscated = SanitizeUserName(account_id);
      if (!homedirs_->Remove(obfuscated)) {
        LOG(ERROR) << "Failed to remove vault for kiosk user.";
      }
    }
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthPublicMountPointBusyInContinueMountWithCred),
            ErrorActionSet({ErrorAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
    return;
  }

  // Don't overlay an ephemeral mount over a file-backed one.
  if (mount_args.is_ephemeral && user_session->IsActive() &&
      !user_session->IsEphemeral()) {
    // TODO(wad,ellyjones) Change this behavior to return failure even
    // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
    if (!user_session->Unmount()) {
      LOG(ERROR) << "Could not unmount vault before an ephemeral mount.";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocUserDataAuthEpheMountPointBusyInContinueMountWithCred),
              ErrorActionSet({ErrorAction::kReboot}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
      return;
    }
  }

  if (mount_args.is_ephemeral && !mount_args.create_if_missing) {
    LOG(ERROR) << "An ephemeral cryptohome can only be mounted when its "
                  "creation on-the-fly is allowed.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthNoCreateForEphemeralInContinueMountWithCred),
            ErrorActionSet({ErrorAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // If a user's home directory is already mounted, then we'll just recheck its
  // credential with what's cached in memory. This is much faster than going to
  // the TPM.
  if (user_session->IsActive()) {
    // Attempt a short-circuited credential test.
    if (user_session->VerifyCredentials(*credentials)) {
      ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
      keyset_management_->ResetLECredentials(*credentials, obfuscated_username);
      return;
    }
    // If the Mount has invalid credentials (repopulated from system state)
    // this will ensure a user can still sign-in with the right ones.
    // TODO(wad) Should we unmount on a failed re-mount attempt?
    if (!user_session->VerifyCredentials(*credentials) &&
        !keyset_management_->AreCredentialsValid(*credentials)) {
      LOG(ERROR) << "Credentials are invalid";
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocUserDataAuthCredVerifyFailedInContinueMountWithCred),
              ErrorActionSet({ErrorAction::kIncorrectAuth}),
              user_data_auth::CryptohomeErrorCode::
                  CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));
    } else {
      keyset_management_->ResetLECredentials(*credentials, obfuscated_username);
      ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
    }
    return;
  }

  // Any non-guest mount attempt triggers InstallAttributes finalization.
  // The return value is ignored as it is possible we're pre-ownership.
  // The next login will assure finalization if possible.
  if (install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    install_attrs_->Finalize();
  }

  // As per the other timers, this really only tracks time spent in
  // MountCryptohome() not in the other areas prior.
  ReportTimerStart(kMountExTimer);

  // Remove all existing cryptohomes, except for the owner's one, if the
  // ephemeral users policy is on.
  // Note that a fresh policy value is read here, which in theory can conflict
  // with the one used for calculation of |mount_args.is_ephemeral|. However,
  // this inconsistency (whose probability is anyway pretty low in practice)
  // should only lead to insignificant transient glitches, like an attempt to
  // mount a non existing anymore cryptohome.
  if (homedirs_->AreEphemeralUsersEnabled())
    homedirs_->RemoveNonOwnerCryptohomes();

  MountStatus code;
  if (auth_session) {
    code = AttemptUserMount(auth_session, mount_args, user_session);
  } else {
    code = AttemptUserMount(*credentials, mount_args, user_session);
  }

  if (!code.ok() && code->mount_error() == MOUNT_ERROR_VAULT_UNRECOVERABLE) {
    LOG(ERROR) << "Unrecoverable vault, removing.";
    if (!homedirs_->Remove(obfuscated_username)) {
      LOG(ERROR) << "Failed to remove unrecoverable vault.";
      code = MakeStatus<CryptohomeMountError>(
          CRYPTOHOME_ERR_LOC(
              kLocUserDataAuthRemoveUnrecoverableFailedInContinueMount),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kPowerwash}),
          MOUNT_ERROR_REMOVE_INVALID_USER_FAILED);
    }
  }

  // Mark the timer as done.
  ReportTimerStop(kMountExTimer);

  if (!code.ok()) {
    // Mount returned a non-OK status.
    LOG(ERROR) << "Failed to mount cryptohome, error = " << code;
    ResetDictionaryAttackMitigation();
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthMountFailedInContinueMountWithCred))
                       .Wrap(std::move(code)));
    return;
  }

  keyset_management_->ResetLECredentials(*credentials, obfuscated_username);
  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());

  InitializePkcs11(user_session.get());

  // Step to record metrics for a user's existing VaultKeysets.
  std::string obfuscated = SanitizeUserName(account_id);
  keyset_management_->RecordAllVaultKeysetMetrics(obfuscated);
}

MountStatusOr<std::unique_ptr<VaultKeyset>> UserDataAuth::LoadVaultKeyset(
    const Credentials& credentials, bool is_new_user) {
  AuthBlockState out_state;
  std::string obfuscated_username = credentials.GetObfuscatedUsername();

  // 1. Handle initial user case.
  if (is_new_user) {
    // Although there isn't any real use case of having LE credential as an
    // initial credential, some cryptohome tast tests add LE credential first.
    // For that we need to keep this check here until the tast test is changed.
    bool is_le_credential = false;
    bool is_challenge_credential =
        credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE;
    KeyBlobs key_blobs;
    CryptoStatus err = CreateKeyBlobs(*auth_block_utility_, *keyset_management_,
                                      is_le_credential, is_challenge_credential,
                                      credentials, key_blobs, out_state);
    if (!err.ok()) {
      LOG(ERROR) << "Error in creating key blobs to add initial keyset: "
                 << err;
      return MakeStatus<CryptohomeMountError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocUserDataAuthCreateKeyBlobsFailedInLoadVK),
                 ErrorActionSet({ErrorAction::kReboot,
                                 ErrorAction::kDevCheckUnexpectedState}),
                 MountError::MOUNT_ERROR_KEY_FAILURE)
          .Wrap(std::move(err));
    }
    std::unique_ptr<AuthBlockState> auth_state =
        std::make_unique<AuthBlockState>(out_state);
    CryptohomeStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
        keyset_management_->AddInitialKeysetWithKeyBlobs(
            obfuscated_username, credentials.key_data(),
            credentials.challenge_credentials_keyset_info(),
            FileSystemKeyset::CreateRandom(), std::move(key_blobs),
            std::move(auth_state));
    if (!vk_status.ok()) {
      LOG(ERROR) << "Error in adding initial keyset.";
      return MakeStatus<CryptohomeMountError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocUserDataAuthAddInitialKeysetFailedInLoadVK),
                 MountError::MOUNT_ERROR_KEY_FAILURE)
          .Wrap(std::move(vk_status).status());
    }
  }

  // 2. Load decrypted VaultKeyset.
  // Empty labels are regarded as wild-card. If the label is empty, try
  // authentication with each of the VaultKeysets on the disk until
  // authentication succeeds.
  std::vector<std::string> key_labels;
  // If |credentials.label| is empty and the key type is KEY_TYPE_PASSWORD get
  // label list of all the VaultKeysets on the disk. Otherwise the label
  // received from |credentials| will be used. GetKeyLabels() fails only if
  // there is no VaultKeyset found on the disk, which is not an expected
  // situation at this point.
  if (!GetKeyLabels(*keyset_management_, credentials, key_labels /*Out*/)) {
    LOG(ERROR) << "Error in LoadVaultKeyset getting the key data of the "
                  "existing keysets.";
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthGetKeyLabelsFailedInLoadVK),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kDeleteVault}),
        MountError::MOUNT_ERROR_VAULT_UNRECOVERABLE);
  }

  // Assign each label from the existing vault keysets one by one to try
  // authentication against each vault keyset.
  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
      MakeStatus<CryptohomeMountError>(
          CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoKeyLabelUsableInLoadVK),
          ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth,
                          ErrorAction::kDeleteVault}),
          MountError::MOUNT_ERROR_KEY_FAILURE);
  Credentials temp_credential(credentials.username(), credentials.passkey());
  KeyData key_data = credentials.key_data();
  for (auto label : key_labels) {
    // There is no manipulation with the credential, only the label is
    // changed (if empty) temporarily to run the wildcard.
    key_data.set_label(label);
    temp_credential.set_key_data(key_data);
    KeyBlobs key_blobs;
    CryptoStatus err = DeriveKeyBlobs(*auth_block_utility_, temp_credential,
                                      key_blobs /*Out*/);
    if (!err.ok()) {
      vk_status = std::move(err);
      continue;
    }
    vk_status = keyset_management_->GetValidKeysetWithKeyBlobs(
        obfuscated_username, std::move(key_blobs), label);
    if (vk_status.ok()) {
      LOG(INFO) << "Authenticated VaultKeyset with label: " << label;
      break;
    }
  }
  if (!vk_status.ok()) {
    return vk_status;
  }

  // 3. Check whether an update is needed for the VaultKeyset. Reencrypt keyset
  // with a TPM backed key if user logged in while TPM ownership was taken. If
  // this is not the case, fill in missing fields in the keyset, and resave.
  VaultKeyset updated_vault_keyset = *(vk_status.value().get());
  if (!keyset_management_->ShouldReSaveKeyset(&updated_vault_keyset)) {
    return vk_status;
  }
  // KeyBlobs needs to be re-created since there maybe a change in the
  // AuthBlock type with the change in TPM state. Don't abort on failure.
  KeyBlobs key_blobs;
  CryptoStatus create_err =
      CreateKeyBlobs(*auth_block_utility_, *keyset_management_,
                     /*is_le_credential*/ false,
                     /* is_challenge_credential */ false, credentials,
                     key_blobs /*out*/, out_state);
  if (!create_err.ok()) {
    LOG(ERROR) << "Error in key creation to resave the keyset. Old vault "
                  "keyset will be used. Error: "
               << create_err;
    return vk_status;
  }
  std::unique_ptr<AuthBlockState> auth_state =
      std::make_unique<AuthBlockState>(out_state);

  CryptohomeStatus status = keyset_management_->ReSaveKeysetWithKeyBlobs(
      updated_vault_keyset, std::move(key_blobs), std::move(auth_state));
  if (!status.ok()) {
    LOG(ERROR) << "Error in resaving updated vault keyset. Old vault keyset "
                  "will be used: "
               << status;
    return vk_status;
  }
  return std::make_unique<VaultKeyset>(updated_vault_keyset);
}

MountStatus UserDataAuth::AttemptUserMount(
    const Credentials& credentials,
    const UserDataAuth::MountArgs& mount_args,
    scoped_refptr<UserSession> user_session) {
  if (user_session->IsActive()) {
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionActiveInAttemptUserMountCred),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kRetry, ErrorAction::kReboot}),
        MOUNT_ERROR_MOUNT_POINT_BUSY);
  }

  if (mount_args.is_ephemeral) {
    user_session->SetCredentials(credentials);
    MountStatus err = user_session->MountEphemeral(credentials.username());
    if (err.ok())
      return OkStatus<CryptohomeMountError>();
    return MakeStatus<CryptohomeMountError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthEphemeralFailedInAttemptUserMountCred))
        .Wrap(std::move(err));
  }

  const std::string obfuscated_username = credentials.GetObfuscatedUsername();
  bool created = false;
  auto exists_or = homedirs_->CryptohomeExists(obfuscated_username);

  if (!exists_or.ok()) {
    LOG(ERROR) << "Failed to check cryptohome existence for : "
               << obfuscated_username
               << " error = " << exists_or.status()->error();
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthCheckExistenceFailedInAttemptUserMountCred),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kRetry, ErrorAction::kReboot,
                        ErrorAction::kDeleteVault}),
        exists_or.status()->error());
  }

  if (!exists_or.value()) {
    if (!mount_args.create_if_missing) {
      LOG(ERROR) << "Asked to mount nonexistent user";
      return MakeStatus<CryptohomeMountError>(
          CRYPTOHOME_ERR_LOC(
              kLocUserDataAuthAccountMissingInAttemptUserMountCred),
          ErrorActionSet({ErrorAction::kCreateRequired}),
          MOUNT_ERROR_USER_DOES_NOT_EXIST);
    }
    if (!homedirs_->Create(credentials.username())) {
      LOG(ERROR) << "Error creating cryptohome.";
      return MakeStatus<CryptohomeMountError>(
          CRYPTOHOME_ERR_LOC(
              kLocUserDataAuthCreateFailedInAttemptUserMountCred),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                          ErrorAction::kRetry, ErrorAction::kReboot,
                          ErrorAction::kPowerwash}),
          MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED);
    }
    created = true;
  }

  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
      LoadVaultKeyset(credentials, created);
  if (!vk_status.ok()) {
    return MakeStatus<CryptohomeMountError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthLoadVKFailedInAttemptUserMountCred))
        .Wrap(std::move(vk_status).status());
  }
  std::unique_ptr<VaultKeyset> vk = std::move(vk_status).value();

  low_disk_space_handler_->disk_cleanup()->FreeDiskSpaceDuringLogin(
      obfuscated_username);
  MountStatus mount_status = user_session->MountVault(
      credentials.username(), FileSystemKeyset(*vk.get()),
      MountArgsToVaultOptions(mount_args));
  if (mount_status.ok()) {
    // Store the credentials in the cache to use on session unlock.
    user_session->SetCredentials(credentials);
    return OkStatus<CryptohomeMountError>();
  }
  return MakeStatus<CryptohomeMountError>(
             CRYPTOHOME_ERR_LOC(
                 kLocUserDataAuthMountVaultFailedInAttemptUserMountCred))
      .Wrap(std::move(mount_status));
}

MountStatus UserDataAuth::AttemptUserMount(
    AuthSession* auth_session,
    const UserDataAuth::MountArgs& mount_args,
    scoped_refptr<UserSession> user_session) {
  if (user_session->IsActive()) {
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionActiveInAttemptUserMountAS),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kRetry, ErrorAction::kReboot}),
        MOUNT_ERROR_MOUNT_POINT_BUSY);
  }
  // Mount ephemerally using authsession
  if (mount_args.is_ephemeral) {
    // Store the credentials in the cache to use on session unlock.
    user_session->SetCredentials(auth_session);
    MountStatus err = user_session->MountEphemeral(auth_session->username());
    return MakeStatus<CryptohomeMountError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthEphemeralFailedInAttemptUserMountAS))
        .Wrap(std::move(err));
  }

  // Cannot proceed with mount if the AuthSession is not authenticated yet.
  if (auth_session->GetStatus() != AuthStatus::kAuthStatusAuthenticated) {
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthNotAuthedInAttemptUserMountAS),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        MOUNT_ERROR_FATAL);
  }

  MountStatus mount_status = user_session->MountVault(
      auth_session->username(), auth_session->file_system_keyset(),
      MountArgsToVaultOptions(mount_args));

  if (mount_status.ok()) {
    // Store the credentials in the cache to use on session unlock.
    user_session->SetCredentials(auth_session);
    return OkStatus<CryptohomeMountError>();
  }
  return MakeStatus<CryptohomeMountError>(
             CRYPTOHOME_ERR_LOC(
                 kLocUserDataAuthMountVaultFailedInAttemptUserMountAS))
      .Wrap(std::move(mount_status));
}

bool UserDataAuth::MigrateVaultKeyset(const Credentials& existing_credentials,
                                      const Credentials& new_credentials) {
  DCHECK_EQ(existing_credentials.username(), new_credentials.username());
  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
      keyset_management_->GetValidKeyset(existing_credentials);
  if (!vk_status.ok()) {
    return false;
  }

  if (!keyset_management_->Migrate(*vk_status.value().get(), new_credentials)) {
    return false;
  }
  return true;
}

CryptohomeErrorCode UserDataAuth::AddVaultKeyset(
    const Credentials& existing_credentials,
    const Credentials& new_credentials,
    bool clobber) {
  DCHECK_EQ(existing_credentials.username(), new_credentials.username());
  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
      keyset_management_->GetValidKeyset(existing_credentials);

  if (!vk_status.ok()) {
    // Differentiate between failure and non-existent.
    if (!existing_credentials.key_data().label().empty()) {
      vk_status = keyset_management_->GetVaultKeyset(
          existing_credentials.GetObfuscatedUsername(),
          existing_credentials.key_data().label());
      if (!vk_status.ok()) {
        LOG(WARNING) << "Key not found for AddKey operation.";
        return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND;
      }
    }
    LOG(WARNING) << "Invalid authentication provided for AddKey operation.";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }

  // If the newly added credential is an LE credential and reset seed is
  // missing in the vault keyset it needs to be added. We don't know whether
  // it is LE credential yet. So add reset_seed in anycase and resave.
  VaultKeyset* vault_keyset = vk_status.value().get();
  CryptohomeErrorCode crypto_error =
      keyset_management_->AddWrappedResetSeedIfMissing(vault_keyset,
                                                       existing_credentials);
  // Add the new key data to the user vault_keyset.
  if (crypto_error == CRYPTOHOME_ERROR_NOT_SET) {
    crypto_error =
        keyset_management_->AddKeyset(new_credentials, *vault_keyset, clobber);
  }
  return crypto_error;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::AddKey(
    const user_data_auth::AddKeyRequest& request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "AddKeyRequest must have account_id and authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "AddKeyRequest must have vaid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.
  if (request.authorization_request().key().secret().empty()) {
    LOG(ERROR) << "No key secret in AddKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (request.key().secret().empty()) {
    LOG(ERROR) << "No new key in AddKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (request.key().data().label().empty()) {
    LOG(ERROR) << "No new key label in AddKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  const std::string& auth_key_secret =
      request.authorization_request().key().secret();
  Credentials credentials(account_id, SecureBlob(auth_key_secret));

  credentials.set_key_data(request.authorization_request().key().data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername())) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  const std::string& new_key_secret = request.key().secret();
  Credentials new_credentials(account_id, SecureBlob(new_key_secret));

  new_credentials.set_key_data(request.key().data());
  CryptohomeErrorCode result;
  result =
      AddVaultKeyset(credentials, new_credentials, request.clobber_if_exists());

  // Note that cryptohome::CryptohomeErrorCode and
  // user_data_auth::CryptohomeErrorCode are same in content, and it'll remain
  // so until the end of the refactor, so we can safely cast from one to
  // another. This is enforced in our unit test.
  return static_cast<user_data_auth::CryptohomeErrorCode>(result);
}

void UserDataAuth::CheckKey(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "CheckKeyRequest must have account_id and authorization_request.";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "CheckKeyRequest must have valid account_id.";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return;
  }

  // Process challenge-response credentials asynchronously.
  if (request.authorization_request().key().data().type() ==
      KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    DoChallengeResponseCheckKey(request, std::move(on_done));
    return;
  }

  // Process fingerprint credentials asynchronously.
  if (request.authorization_request().key().data().type() ==
      KeyData::KEY_TYPE_FINGERPRINT) {
    if (!fingerprint_manager_) {
      // Fingerprint manager failed to initialize, or the device may not
      // support fingerprint auth at all.
      std::move(on_done).Run(user_data_auth::CryptohomeErrorCode::
                                 CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
      return;
    }
    if (!fingerprint_manager_->HasAuthSessionForUser(
            SanitizeUserName(account_id))) {
      std::move(on_done).Run(user_data_auth::CryptohomeErrorCode::
                                 CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
      return;
    }
    fingerprint_manager_->SetAuthScanDoneCallback(base::BindRepeating(
        &UserDataAuth::CompleteFingerprintCheckKey, base::Unretained(this),
        base::Passed(std::move(on_done))));
    return;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.

  const std::string& auth_secret =
      request.authorization_request().key().secret();
  if (auth_secret.empty()) {
    LOG(ERROR) << "No key secret in CheckKeyRequest.";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return;
  }

  Credentials credentials(account_id, SecureBlob(auth_secret));
  credentials.set_key_data(request.authorization_request().key().data());

  const std::string obfuscated_username = credentials.GetObfuscatedUsername();

  bool found_valid_credentials = false;
  if (sessions_.count(account_id) != 0U) {
    if (sessions_[account_id]->VerifyCredentials(credentials)) {
      found_valid_credentials = true;
    } else if (sessions_[account_id]->IsEphemeral()) {
      std::move(on_done).Run(
          user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
      return;
    }
  }

  if (found_valid_credentials) {
    MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
        keyset_management_->GetValidKeyset(credentials);
    std::unique_ptr<VaultKeyset> vk;
    if (!vk_status.ok()) {
      // The operation may fail for ephemeral user.
      LOG(WARNING) << "Failed to get valid keyset in CheckKey: << "
                   << std::move(vk_status).status();
    } else {
      vk = std::move(vk_status).value();
    }

    if (vk) {
      // Entered the right creds, so reset LE credentials.
      keyset_management_->ResetLECredentialsWithValidatedVK(
          *vk, obfuscated_username);
    }

    if (request.unlock_webauthn_secret()) {
      if (vk == nullptr) {
        std::move(on_done).Run(
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
        return;
      }
      if (!PrepareWebAuthnSecret(account_id, *vk)) {
        // Failed to prepare WebAuthn secret means there's no active user
        // session for the account id.
        std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
        return;
      }
    }

    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
    return;
  }

  // Cover different keys for the same user with homedirs.
  if (!homedirs_->Exists(obfuscated_username)) {
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    return;
  }

  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
      keyset_management_->GetValidKeyset(credentials);
  if (!vk_status.ok()) {
    // TODO(wad) Should this pass along KEY_NOT_FOUND too?
    std::move(on_done).Run(
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    ResetDictionaryAttackMitigation();
    return;
  }

  keyset_management_->ResetLECredentialsWithValidatedVK(*vk_status.value(),
                                                        obfuscated_username);

  if (request.unlock_webauthn_secret()) {
    if (!PrepareWebAuthnSecret(account_id, *vk_status.value().get())) {
      // Failed to prepare WebAuthn secret means there's no active user
      // session for the account id.
      std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
      return;
    }
  }
  std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  return;
}

bool UserDataAuth::PrepareWebAuthnSecret(const std::string& account_id,
                                         const VaultKeyset& vk) {
  scoped_refptr<UserSession> session = GetUserSession(account_id);
  if (!session) {
    return false;
  }
  FileSystemKeyset fs_keyset(vk);
  session->PrepareWebAuthnSecret(fs_keyset.Key().fek, fs_keyset.Key().fnek);
  return true;
}

void UserDataAuth::CompleteFingerprintCheckKey(
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
    FingerprintScanStatus status) {
  AssertOnMountThread();
  if (status == FingerprintScanStatus::FAILED_RETRY_ALLOWED) {
    std::move(on_done).Run(user_data_auth::CryptohomeErrorCode::
                               CRYPTOHOME_ERROR_FINGERPRINT_RETRY_REQUIRED);
    return;
  } else if (status == FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED) {
    std::move(on_done).Run(user_data_auth::CryptohomeErrorCode::
                               CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
    return;
  }

  std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

void UserDataAuth::DoChallengeResponseCheckKey(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done) {
  AssertOnMountThread();

  const auto& authorization = request.authorization_request();
  DCHECK_EQ(authorization.key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  CryptohomeStatus status = InitForChallengeResponseAuth();
  if (!status.ok()) {
    std::move(on_done).Run(LegacyErrorCodeFromStack(status));
    return;
  }

  if (!authorization.has_key_delegate() ||
      !authorization.key_delegate().has_dbus_service_name()) {
    LOG(ERROR) << "Cannot do challenge-response authentication without key "
                  "delegate information";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }
  if (!authorization.key().data().challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }
  if (authorization.key().data().challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  // Begin from attempting a lightweight check that doesn't use the vault keyset
  // or heavy TPM operations, and therefore is faster than the full check and
  // also works in case the mount is ephemeral.
  TryLightweightChallengeResponseCheckKey(request, std::move(on_done));
}

void UserDataAuth::TryLightweightChallengeResponseCheckKey(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done) {
  AssertOnMountThread();

  const auto& authorization = request.authorization_request();
  const auto& identifier = request.account_id();

  DCHECK_EQ(authorization.key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  DCHECK(challenge_credentials_helper_);

  const std::string& account_id = GetAccountId(identifier);
  const std::string obfuscated_username = SanitizeUserName(account_id);

  std::optional<KeyData> found_session_key_data;
  for (const auto& session_pair : sessions_) {
    const scoped_refptr<UserSession>& session = session_pair.second;
    if (session->VerifyUser(obfuscated_username) &&
        KeyMatchesForLightweightChallengeResponseCheck(
            authorization.key().data(), *session)) {
      found_session_key_data = session->key_data();
      break;
    }
  }
  if (!found_session_key_data) {
    // No matching user session found, so fall back to the full check.
    OnLightweightChallengeResponseCheckKeyDone(
        request, std::move(on_done),
        MakeStatus<CryptohomeTPMError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthNoSessionInTryLiteChalRespCheckKey),
            ErrorActionSet({ErrorAction::kReboot}), TPMRetryAction::kReboot));
    return;
  }

  // KeyChallengeService is tasked with contacting the challenge response D-Bus
  // service that'll provide the response once we send the challenge.
  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          mount_thread_bus_, authorization.key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    OnLightweightChallengeResponseCheckKeyDone(
        request, std::move(on_done),
        MakeStatus<CryptohomeTPMError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthNoServiceInTryLiteChalRespCheckKey),
            ErrorActionSet({ErrorAction::kReboot, ErrorAction::kAuth}),
            TPMRetryAction::kReboot));
    return;
  }

  if (!found_session_key_data->challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    OnLightweightChallengeResponseCheckKeyDone(
        request, std::move(on_done),
        MakeStatus<CryptohomeTPMError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthNoKeyInfoInTryLiteChalRespCheckKey),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            TPMRetryAction::kNoRetry));
    return;
  }

  if (found_session_key_data->challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    OnLightweightChallengeResponseCheckKeyDone(
        request, std::move(on_done),
        MakeStatus<CryptohomeTPMError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthMultipleKeyInTryLiteChalRespCheckKey),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            TPMRetryAction::kNoRetry));
    return;
  }

  const ChallengePublicKeyInfo& public_key_info =
      found_session_key_data->challenge_response_key(0);

  // Attempt the lightweight check against the found user session.
  challenge_credentials_helper_->VerifyKey(
      account_id, proto::FromProto(public_key_info),
      std::move(key_challenge_service),
      base::BindOnce(&UserDataAuth::OnLightweightChallengeResponseCheckKeyDone,
                     base::Unretained(this), request, std::move(on_done)));
}

void UserDataAuth::OnLightweightChallengeResponseCheckKeyDone(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
    TPMStatus status) {
  AssertOnMountThread();
  if (!status.ok()) {
    DoFullChallengeResponseCheckKey(request, std::move(on_done));
    return;
  }

  // Note that the LE credentials are not reset here, since we don't have the
  // full credentials after the lightweight check.
  std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

void UserDataAuth::DoFullChallengeResponseCheckKey(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done) {
  AssertOnMountThread();

  const auto& authorization = request.authorization_request();
  const auto& identifier = request.account_id();

  DCHECK_EQ(authorization.key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  DCHECK(challenge_credentials_helper_);

  const std::string& account_id = GetAccountId(identifier);
  const std::string obfuscated_username = SanitizeUserName(account_id);

  // KeyChallengeService is tasked with contacting the challenge response D-Bus
  // service that'll provide the response once we send the challenge.
  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          mount_thread_bus_, authorization.key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  if (!homedirs_->Exists(obfuscated_username)) {
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    return;
  }

  std::unique_ptr<VaultKeyset> vault_keyset(keyset_management_->GetVaultKeyset(
      obfuscated_username, authorization.key().data().label()));
  if (!vault_keyset) {
    LOG(ERROR) << "No existing challenge-response vault keyset found";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  if (!authorization.key().data().challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  if (authorization.key().data().challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  const ChallengePublicKeyInfo& public_key_info =
      authorization.key().data().challenge_response_key(0);

  challenge_credentials_helper_->Decrypt(
      account_id, proto::FromProto(public_key_info),
      proto::FromProto(vault_keyset->GetSignatureChallengeInfo()),
      std::move(key_challenge_service),
      base::BindOnce(&UserDataAuth::OnFullChallengeResponseCheckKeyDone,
                     base::Unretained(this), request, std::move(on_done)));
}

void UserDataAuth::OnFullChallengeResponseCheckKeyDone(
    const user_data_auth::CheckKeyRequest& request,
    base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
    TPMStatusOr<ChallengeCredentialsHelper::GenerateNewOrDecryptResult>
        result) {
  AssertOnMountThread();
  if (!result.ok()) {
    LOG(ERROR) << "Key checking failed due to failure to obtain "
                  "challenge-response credentials";
    std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
    return;
  }

  ChallengeCredentialsHelper::GenerateNewOrDecryptResult result_val =
      std::move(result).value();
  std::unique_ptr<brillo::SecureBlob> passkey = result_val.passkey();

  const auto& authorization = request.authorization_request();
  const auto& identifier = request.account_id();
  const std::string& account_id = GetAccountId(identifier);

  auto credentials = std::make_unique<Credentials>(account_id, *passkey);
  credentials->set_key_data(authorization.key().data());

  // Entered the right creds, so reset LE credentials.
  keyset_management_->ResetLECredentials(*credentials,
                                         credentials->GetObfuscatedUsername());

  std::move(on_done).Run(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::RemoveKey(
    const user_data_auth::RemoveKeyRequest request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "RemoveKeyRequest must have account_id and authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "RemoveKeyRequest must have vaid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.

  const std::string& auth_secret =
      request.authorization_request().key().secret();
  if (auth_secret.empty()) {
    LOG(ERROR) << "No key secret in RemoveKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (request.key().data().label().empty()) {
    LOG(ERROR) << "No new key label in RemoveKeyRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  Credentials credentials(account_id, SecureBlob(auth_secret));

  credentials.set_key_data(request.authorization_request().key().data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername())) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  CryptohomeStatus result =
      keyset_management_->RemoveKeyset(credentials, request.key().data());

  if (result.ok()) {
    scoped_refptr<UserSession> session = GetUserSession(account_id);
    if (session.get()) {
      session->RemoveCredentialVerifierForKeyLabel(
          request.key().data().label());
    }
    return user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET;
  }
  return result->local_legacy_error().value();
}

user_data_auth::CryptohomeErrorCode UserDataAuth::MassRemoveKeys(
    const user_data_auth::MassRemoveKeysRequest request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR) << "MassRemoveKeysRequest must have account_id and "
                  "authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "MassRemoveKeysRequest must have vaid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  // Note that there's no check for empty AuthorizationRequest key label because
  // such a key will test against all VaultKeysets of a compatible
  // key().data().type(), and thus is valid.

  const std::string& auth_secret =
      request.authorization_request().key().secret();
  if (auth_secret.empty()) {
    LOG(ERROR) << "No key secret in MassRemoveKeysRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  Credentials credentials(account_id, SecureBlob(auth_secret));

  credentials.set_key_data(request.authorization_request().key().data());

  const std::string obfuscated_username = credentials.GetObfuscatedUsername();
  if (!homedirs_->Exists(obfuscated_username)) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  if (!keyset_management_->AreCredentialsValid(credentials)) {
    return user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }

  // get all labels under the username
  std::vector<std::string> labels;
  if (!keyset_management_->GetVaultKeysetLabels(
          obfuscated_username, /*include_le_labels*/ true, &labels)) {
    return user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }

  // get all exempt labels from |request|
  std::unordered_set<std::string> exempt_labels;
  for (int i = 0; i < request.exempt_key_data_size(); i++) {
    exempt_labels.insert(request.exempt_key_data(i).label());
  }
  for (std::string label : labels) {
    if (exempt_labels.find(label) == exempt_labels.end()) {
      // non-exempt label, should be removed
      std::unique_ptr<VaultKeyset> remove_vk(
          keyset_management_->GetVaultKeyset(obfuscated_username, label));
      if (CryptohomeStatus status = keyset_management_->ForceRemoveKeyset(
              obfuscated_username, remove_vk->GetLegacyIndex());
          !status.ok()) {
        LOG(ERROR) << "MassRemoveKeys: failed to remove keyset " << label
                   << ": " << status;
        return user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
      }
    }
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::ListKeysReply UserDataAuth::ListKeys(
    const user_data_auth::ListKeysRequest& request) {
  AssertOnMountThread();
  user_data_auth::ListKeysReply reply;

  if (!request.has_account_id()) {
    // ListKeysRequest must have account_id.
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoIDInListKeys),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  const std::string& account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    // ListKeysRequest must have valid account_id.
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthInvalidIDInListKeys),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  const std::string obfuscated_username = SanitizeUserName(account_id);
  if (!homedirs_->Exists(obfuscated_username)) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUserNonexistentInListKeys),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND),
        &reply);
    return reply;
  }

  std::vector<std::string> labels_out;
  if (!keyset_management_->GetVaultKeysetLabels(
          obfuscated_username, /*include_le_labels*/ true, &labels_out)) {
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthListFailedInListKeys),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND),
        &reply);
    return reply;
  }
  *reply.mutable_labels() = {labels_out.begin(), labels_out.end()};
  PopulateReplyWithError(OkStatus<CryptohomeError>(), &reply);
  return reply;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::GetKeyData(
    const user_data_auth::GetKeyDataRequest& request,
    cryptohome::KeyData* data_out,
    bool* found) {
  AssertOnMountThread();

  if (!request.has_account_id()) {
    // Note that authorization request is currently not required.
    LOG(ERROR) << "GetKeyDataRequest must have account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "GetKeyDataRequest must have vaid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  if (!request.has_key()) {
    LOG(ERROR) << "No key attributes provided in GetKeyDataRequest.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  const std::string obfuscated_username = SanitizeUserName(account_id);
  if (!homedirs_->Exists(obfuscated_username)) {
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  // Requests only support using the key label at present.
  std::unique_ptr<VaultKeyset> vk(keyset_management_->GetVaultKeyset(
      obfuscated_username, request.key().data().label()));
  *found = (vk != nullptr);
  if (*found) {
    *data_out = vk->GetKeyDataOrDefault();
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode UserDataAuth::MigrateKey(
    const user_data_auth::MigrateKeyRequest& request) {
  AssertOnMountThread();

  if (!request.has_account_id() || !request.has_authorization_request()) {
    LOG(ERROR)
        << "MigrateKeyRequest must have account_id and authorization_request.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "MigrateKeyRequest must have valid account_id.";
    return user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT;
  }

  Credentials credentials(account_id, SecureBlob(request.secret()));

  Credentials old_credentials(
      account_id, SecureBlob(request.authorization_request().key().secret()));
  if (!MigrateVaultKeyset(old_credentials, credentials)) {
    ResetDictionaryAttackMitigation();
    return user_data_auth::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED;
  }

  scoped_refptr<UserSession> session = GetUserSession(account_id);
  if (session.get()) {
    session->SetCredentials(credentials);
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::RemoveReply UserDataAuth::Remove(
    const user_data_auth::RemoveRequest& request) {
  AssertOnMountThread();

  user_data_auth::RemoveReply reply;
  if (!request.has_identifier() && request.auth_session_id().empty()) {
    // RemoveRequest must have identifier or an AuthSession Id
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoIDInRemove),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  AuthSession* auth_session = nullptr;
  if (!request.auth_session_id().empty()) {
    auth_session =
        auth_session_manager_->FindAuthSession(request.auth_session_id());
    if (!auth_session) {
      PopulateReplyWithError(
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocUserDataAuthInvalidAuthSessionInRemove),
              ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                              ErrorAction::kReboot}),
              user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN),
          &reply);
      return reply;
    }
  }

  std::string account_id = auth_session ? auth_session->username()
                                        : GetAccountId(request.identifier());
  if (account_id.empty()) {
    // RemoveRequest must have valid account_id.
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocUserDataAuthNoAccountIdWithAuthSessionInRemove),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        &reply);
    return reply;
  }

  std::string obfuscated = SanitizeUserName(account_id);

  scoped_refptr<UserSession> session = GetUserSession(account_id);
  if (session.get() && session->IsActive()) {
    // Can't remove active user
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthUserActiveInRemove),
            ErrorActionSet({ErrorAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY),
        &reply);
    return reply;
  }

  if (!homedirs_->Remove(obfuscated)) {
    // User vault removal failed.
    PopulateReplyWithError(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthRemoveFailedInRemove),
            ErrorActionSet({ErrorAction::kPowerwash, ErrorAction::kReboot}),
            user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED),
        &reply);
    return reply;
  }

  // Since the user is now removed, any further operations require a fresh
  // AuthSession.
  if (auth_session) {
    if (!auth_session_manager_->RemoveAuthSession(request.auth_session_id())) {
      NOTREACHED() << "Failed to remove AuthSession when removing user.";
    }
  }

  PopulateReplyWithError(OkStatus<CryptohomeError>(), &reply);
  return reply;
}

void UserDataAuth::StartMigrateToDircrypto(
    const user_data_auth::StartMigrateToDircryptoRequest& request,
    base::RepeatingCallback<void(
        const user_data_auth::DircryptoMigrationProgress&)> progress_callback) {
  AssertOnMountThread();

  MigrationType migration_type = request.minimal_migration()
                                     ? MigrationType::MINIMAL
                                     : MigrationType::FULL;

  // Note that total_bytes and current_bytes field in |progress| is discarded by
  // client whenever |progress.status| is not DIRCRYPTO_MIGRATION_IN_PROGRESS,
  // this is why they are left with the default value of 0 here. Please see
  // MigrationHelper::ProgressCallback for more details.
  user_data_auth::DircryptoMigrationProgress progress;
  AuthSession* auth_session = nullptr;
  if (!request.auth_session_id().empty()) {
    CryptohomeStatusOr<AuthSession*> auth_session_status =
        GetAuthenticatedAuthSession(request.auth_session_id());
    if (!auth_session_status.ok()) {
      LOG(ERROR) << "StartMigrateToDircrypto: Invalid auth_session_id.";
      progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
      progress_callback.Run(progress);
      return;
    }
    auth_session = auth_session_status.value();
  }

  std::string account_id = auth_session ? auth_session->username()
                                        : GetAccountId(request.account_id());
  scoped_refptr<UserSession> session = GetUserSession(account_id);
  if (!session.get()) {
    LOG(ERROR) << "StartMigrateToDircrypto: Failed to get session.";
    progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
    progress_callback.Run(progress);
    return;
  }
  LOG(INFO) << "StartMigrateToDircrypto: Migrating to dircrypto.";
  if (!session->MigrateVault(progress_callback, migration_type)) {
    LOG(ERROR) << "StartMigrateToDircrypto: Failed to migrate.";
    progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
    progress_callback.Run(progress);
    return;
  }
  LOG(INFO) << "StartMigrateToDircrypto: Migration done.";
  progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_SUCCESS);
  progress_callback.Run(progress);
}

user_data_auth::CryptohomeErrorCode UserDataAuth::NeedsDircryptoMigration(
    const cryptohome::AccountIdentifier& account, bool* result) {
  AssertOnMountThread();
  const std::string obfuscated_username =
      SanitizeUserName(GetAccountId(account));
  if (!homedirs_->Exists(obfuscated_username)) {
    LOG(ERROR) << "Unknown user in NeedsDircryptoMigration.";
    return user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
  }

  *result = !force_ecryptfs_ &&
            homedirs_->NeedsDircryptoMigration(obfuscated_username);
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

bool UserDataAuth::IsLowEntropyCredentialSupported() {
  AssertOnOriginThread();
  hwsec::StatusOr<bool> is_enabled = hwsec_->IsPinWeaverEnabled();
  if (!is_enabled.ok()) {
    LOG(ERROR) << "Failed to get pinweaver status";
    return false;
  }
  return is_enabled.value();
}

int64_t UserDataAuth::GetAccountDiskUsage(
    const cryptohome::AccountIdentifier& account) {
  AssertOnMountThread();
  // Note that if the given |account| is invalid or non-existent, then HomeDirs'
  // implementation of ComputeDiskUsage is specified to return 0.
  return homedirs_->ComputeDiskUsage(GetAccountId(account));
}

bool UserDataAuth::IsArcQuotaSupported() {
  AssertOnOriginThread();
  return arc_disk_quota_->IsQuotaSupported();
}

int64_t UserDataAuth::GetCurrentSpaceForArcUid(uid_t android_uid) {
  AssertOnOriginThread();
  return arc_disk_quota_->GetCurrentSpaceForUid(android_uid);
}

int64_t UserDataAuth::GetCurrentSpaceForArcGid(uid_t android_gid) {
  AssertOnOriginThread();
  return arc_disk_quota_->GetCurrentSpaceForGid(android_gid);
}

int64_t UserDataAuth::GetCurrentSpaceForArcProjectId(int project_id) {
  AssertOnOriginThread();
  return arc_disk_quota_->GetCurrentSpaceForProjectId(project_id);
}

bool UserDataAuth::SetMediaRWDataFileProjectId(int project_id,
                                               int fd,
                                               int* out_error) {
  AssertOnOriginThread();
  return arc_disk_quota_->SetMediaRWDataFileProjectId(project_id, fd,
                                                      out_error);
}

bool UserDataAuth::SetMediaRWDataFileProjectInheritanceFlag(bool enable,
                                                            int fd,
                                                            int* out_error) {
  AssertOnOriginThread();
  return arc_disk_quota_->SetMediaRWDataFileProjectInheritanceFlag(enable, fd,
                                                                   out_error);
}

bool UserDataAuth::Pkcs11IsTpmTokenReady() {
  AssertOnMountThread();
  // We touched the sessions_ object, so we need to be on mount thread.

  for (const auto& session_pair : sessions_) {
    UserSession* session = session_pair.second.get();
    if (!session->GetPkcs11Token() || !session->GetPkcs11Token()->IsReady()) {
      return false;
    }
  }

  return true;
}

user_data_auth::TpmTokenInfo UserDataAuth::Pkcs11GetTpmTokenInfo(
    const std::string& username) {
  AssertOnOriginThread();
  user_data_auth::TpmTokenInfo result;
  std::string label, pin;
  CK_SLOT_ID slot;
  FilePath token_path;
  if (username.empty()) {
    // We want to get the system token.

    // Get the label and pin for system token.
    pkcs11_init_->GetTpmTokenInfo(&label, &pin);

    token_path = FilePath(chaps::kSystemTokenPath);
  } else {
    // We want to get the user token.

    // Get the label and pin for user token.
    pkcs11_init_->GetTpmTokenInfoForUser(username, &label, &pin);

    token_path = homedirs_->GetChapsTokenDir(username);
  }

  result.set_label(label);
  result.set_user_pin(pin);

  if (!pkcs11_init_->GetTpmTokenSlotForPath(token_path, &slot)) {
    // Failed to get the slot, let's use -1 for default.
    slot = -1;
  }
  result.set_slot(slot);

  return result;
}

void UserDataAuth::Pkcs11Terminate() {
  AssertOnMountThread();
  // We are touching the |sessions_| object so we need to be on mount thread.

  for (const auto& session_pair : sessions_) {
    if (session_pair.second->GetPkcs11Token()) {
      session_pair.second->GetPkcs11Token()->Remove();
    }
  }
}

bool UserDataAuth::InstallAttributesGet(const std::string& name,
                                        std::vector<uint8_t>* data_out) {
  AssertOnMountThread();
  return install_attrs_->Get(name, data_out);
}

bool UserDataAuth::InstallAttributesSet(const std::string& name,
                                        const std::vector<uint8_t>& data) {
  AssertOnMountThread();
  return install_attrs_->Set(name, data);
}

bool UserDataAuth::InstallAttributesFinalize() {
  AssertOnMountThread();
  bool result = install_attrs_->Finalize();
  DetectEnterpriseOwnership();
  return result;
}

int UserDataAuth::InstallAttributesCount() {
  AssertOnMountThread();
  return install_attrs_->Count();
}

bool UserDataAuth::InstallAttributesIsSecure() {
  AssertOnMountThread();
  return install_attrs_->IsSecure();
}

InstallAttributes::Status UserDataAuth::InstallAttributesGetStatus() {
  AssertOnMountThread();
  return install_attrs_->status();
}

// static
user_data_auth::InstallAttributesState
UserDataAuth::InstallAttributesStatusToProtoEnum(
    InstallAttributes::Status status) {
  static const std::unordered_map<InstallAttributes::Status,
                                  user_data_auth::InstallAttributesState>
      state_map = {{InstallAttributes::Status::kUnknown,
                    user_data_auth::InstallAttributesState::UNKNOWN},
                   {InstallAttributes::Status::kTpmNotOwned,
                    user_data_auth::InstallAttributesState::TPM_NOT_OWNED},
                   {InstallAttributes::Status::kFirstInstall,
                    user_data_auth::InstallAttributesState::FIRST_INSTALL},
                   {InstallAttributes::Status::kValid,
                    user_data_auth::InstallAttributesState::VALID},
                   {InstallAttributes::Status::kInvalid,
                    user_data_auth::InstallAttributesState::INVALID}};
  if (state_map.count(status) != 0) {
    return state_map.at(status);
  }

  NOTREACHED();
  // Return is added so compiler doesn't complain.
  return user_data_auth::InstallAttributesState::INVALID;
}

void UserDataAuth::OnFingerprintStartAuthSessionResp(
    base::OnceCallback<
        void(const user_data_auth::StartFingerprintAuthSessionReply&)> on_done,
    bool success) {
  AssertOnMountThread();
  VLOG(1) << "Start fingerprint auth session result: " << success;
  user_data_auth::StartFingerprintAuthSessionReply reply;
  if (!success) {
    reply.set_error(user_data_auth::CryptohomeErrorCode::
                        CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
  }
  std::move(on_done).Run(reply);
}

void UserDataAuth::StartFingerprintAuthSession(
    const user_data_auth::StartFingerprintAuthSessionRequest& request,
    base::OnceCallback<void(
        const user_data_auth::StartFingerprintAuthSessionReply&)> on_done) {
  AssertOnMountThread();
  user_data_auth::StartFingerprintAuthSessionReply reply;

  if (!request.has_account_id()) {
    LOG(ERROR) << "StartFingerprintAuthSessionRequest must have account_id";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR)
        << "StartFingerprintAuthSessionRequest must have vaid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    std::move(on_done).Run(reply);
    return;
  }

  const std::string obfuscated_username = SanitizeUserName(account_id);
  if (!homedirs_->Exists(obfuscated_username)) {
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    std::move(on_done).Run(reply);
    return;
  }

  fingerprint_manager_->StartAuthSessionAsyncForUser(
      obfuscated_username,
      base::BindOnce(&UserDataAuth::OnFingerprintStartAuthSessionResp,
                     base::Unretained(this), std::move(on_done)));
}

void UserDataAuth::EndFingerprintAuthSession() {
  AssertOnMountThread();
  fingerprint_manager_->EndAuthSession();
}

user_data_auth::GetWebAuthnSecretReply UserDataAuth::GetWebAuthnSecret(
    const user_data_auth::GetWebAuthnSecretRequest& request) {
  AssertOnMountThread();
  user_data_auth::GetWebAuthnSecretReply reply;

  if (!request.has_account_id()) {
    LOG(ERROR) << "GetWebAuthnSecretRequest must have account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "GetWebAuthnSecretRequest must have valid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  scoped_refptr<UserSession> session = GetUserSession(account_id);
  std::unique_ptr<brillo::SecureBlob> secret;
  if (session) {
    secret = session->GetWebAuthnSecret();
  }
  if (!secret) {
    LOG(ERROR) << "Failed to get WebAuthn secret.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    return reply;
  }

  reply.set_webauthn_secret(secret->to_string());
  return reply;
}

user_data_auth::GetWebAuthnSecretHashReply UserDataAuth::GetWebAuthnSecretHash(
    const user_data_auth::GetWebAuthnSecretHashRequest& request) {
  AssertOnMountThread();
  user_data_auth::GetWebAuthnSecretHashReply reply;

  if (!request.has_account_id()) {
    LOG(ERROR) << "GetWebAuthnSecretHashRequest must have account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "GetWebAuthnSecretHashRequest must have valid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  scoped_refptr<UserSession> session = GetUserSession(account_id);
  brillo::SecureBlob secret_hash;
  if (session) {
    secret_hash = session->GetWebAuthnSecretHash();
  }
  if (secret_hash.empty()) {
    LOG(ERROR) << "Failed to get WebAuthn secret hash.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    return reply;
  }

  reply.set_webauthn_secret_hash(secret_hash.to_string());
  return reply;
}

user_data_auth::GetHibernateSecretReply UserDataAuth::GetHibernateSecret(
    const user_data_auth::GetHibernateSecretRequest& request) {
  AssertOnMountThread();
  user_data_auth::GetHibernateSecretReply reply;

  // If there's an auth_session_id, use that to create the hibernate
  // secret on demand (otherwise it's not available until later).
  if (!request.auth_session_id().empty()) {
    CryptohomeStatusOr<AuthSession*> auth_session_status =
        GetAuthenticatedAuthSession(request.auth_session_id());
    if (!auth_session_status.ok()) {
      LOG(ERROR) << "Invalid AuthSession for HibernateSecret.";
      reply.set_error(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
      return reply;
    }

    std::unique_ptr<brillo::SecureBlob> secret =
        auth_session_status.value()->GetHibernateSecret();

    reply.set_hibernate_secret(secret->to_string());
    return reply;
  }

  LOG(INFO) << "Getting the hibernate secret via legacy account_id";
  if (!request.has_account_id()) {
    LOG(ERROR) << "GetHibernateSecretRequest must have account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  std::string account_id = GetAccountId(request.account_id());
  if (account_id.empty()) {
    LOG(ERROR) << "GetHibernateSecretRequest must have valid account_id.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
    return reply;
  }

  scoped_refptr<UserSession> session = GetUserSession(account_id);
  std::unique_ptr<brillo::SecureBlob> secret;
  if (session) {
    secret = session->GetHibernateSecret();
  }
  if (!secret) {
    LOG(ERROR) << "Failed to get hibernate secret hash.";
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    return reply;
  }

  reply.set_hibernate_secret(secret->to_string());
  return reply;
}

user_data_auth::CryptohomeErrorCode
UserDataAuth::GetFirmwareManagementParameters(
    user_data_auth::FirmwareManagementParameters* fwmp) {
  AssertOnOriginThread();
  if (!firmware_management_parameters_->Load()) {
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  uint32_t flags;
  if (firmware_management_parameters_->GetFlags(&flags)) {
    fwmp->set_flags(flags);
  } else {
    LOG(WARNING)
        << "Failed to GetFlags() for GetFirmwareManagementParameters().";
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  std::vector<uint8_t> hash;
  if (firmware_management_parameters_->GetDeveloperKeyHash(&hash)) {
    *fwmp->mutable_developer_key_hash() = {hash.begin(), hash.end()};
  } else {
    LOG(WARNING) << "Failed to GetDeveloperKeyHash() for "
                    "GetFirmwareManagementParameters().";
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode
UserDataAuth::SetFirmwareManagementParameters(
    const user_data_auth::FirmwareManagementParameters& fwmp) {
  AssertOnOriginThread();

  if (!firmware_management_parameters_->Create()) {
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  uint32_t flags = fwmp.flags();
  std::unique_ptr<std::vector<uint8_t>> hash;

  if (!fwmp.developer_key_hash().empty()) {
    hash.reset(new std::vector<uint8_t>(fwmp.developer_key_hash().begin(),
                                        fwmp.developer_key_hash().end()));
  }

  if (!firmware_management_parameters_->Store(flags, hash.get())) {
    return user_data_auth::
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

bool UserDataAuth::RemoveFirmwareManagementParameters() {
  AssertOnOriginThread();
  return firmware_management_parameters_->Destroy();
}

const brillo::SecureBlob& UserDataAuth::GetSystemSalt() {
  AssertOnOriginThread();
  DCHECK_NE(system_salt_.size(), 0)
      << "Cannot call GetSystemSalt before initialization";
  return system_salt_;
}

bool UserDataAuth::UpdateCurrentUserActivityTimestamp(int time_shift_sec) {
  AssertOnMountThread();
  // We are touching the sessions object, so we'll need to be on mount thread.

  bool success = true;
  for (const auto& session_pair : sessions_) {
    scoped_refptr<UserSession> session = session_pair.second;
    const std::string obfuscated_username =
        SanitizeUserName(session_pair.first);
    // Inactive session is not current and ephemerals should not have ts since
    // they do not affect disk space use and do not participate in disk
    // cleaning.
    if (!session->IsActive() || session->IsEphemeral()) {
      continue;
    }
    success &= user_activity_timestamp_manager_->UpdateTimestamp(
        obfuscated_username, base::Seconds(time_shift_sec));
  }

  return success;
}

bool UserDataAuth::GetRsuDeviceId(std::string* rsu_device_id) {
  AssertOnOriginThread();

  hwsec::StatusOr<brillo::Blob> rsu = hwsec_->GetRsuDeviceId();
  if (!rsu.ok()) {
    LOG(INFO) << "Failed to get RSU device ID: " << rsu.status();
    return false;
  }

  *rsu_device_id = brillo::BlobToString(rsu.value());
  return true;
}

bool UserDataAuth::RequiresPowerwash() {
  AssertOnOriginThread();
  const bool is_powerwash_required = !crypto_->CanUnsealWithUserAuth();
  return is_powerwash_required;
}

user_data_auth::CryptohomeErrorCode
UserDataAuth::LockToSingleUserMountUntilReboot(
    const cryptohome::AccountIdentifier& account_id) {
  AssertOnOriginThread();
  const std::string obfuscated_username =
      SanitizeUserName(GetAccountId(account_id));

  homedirs_->SetLockedToSingleUser();
  brillo::Blob pcr_value;

  hwsec::StatusOr<bool> is_current_user_set = hwsec_->IsCurrentUserSet();
  if (!is_current_user_set.ok()) {
    LOG(ERROR) << "Failed to get current user status for "
                  "LockToSingleUserMountUntilReboot(): "
               << is_current_user_set.status();
    return user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_READ_PCR;
  }

  if (is_current_user_set.value()) {
    return user_data_auth::CRYPTOHOME_ERROR_PCR_ALREADY_EXTENDED;
  }

  if (hwsec::Status status = hwsec_->SetCurrentUser(obfuscated_username);
      !status.ok()) {
    LOG(ERROR)
        << "Failed to set current user for LockToSingleUserMountUntilReboot(): "
        << status;
    return user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_EXTEND_PCR;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

bool UserDataAuth::OwnerUserExists() {
  AssertOnOriginThread();
  std::string owner;
  return homedirs_->GetPlainOwner(&owner);
}

std::string UserDataAuth::GetStatusString() {
  AssertOnMountThread();

  base::Value mounts(base::Value::Type::LIST);
  for (const auto& session_pair : sessions_) {
    mounts.Append(session_pair.second->GetStatus());
  }

  base::Value dv(base::Value::Type::DICTIONARY);
  dv.SetKey("mounts", std::move(mounts));
  std::string json;
  base::JSONWriter::WriteWithOptions(dv, base::JSONWriter::OPTIONS_PRETTY_PRINT,
                                     &json);
  return json;
}

void UserDataAuth::ResetDictionaryAttackMitigation() {
  AssertOnMountThread();

  if (hwsec::Status status = hwsec_->MitigateDACounter(); !status.ok()) {
    LOG(WARNING) << "Failed to mitigate DA counter: " << status;
  }
}

void UserDataAuth::StartAuthSession(
    user_data_auth::StartAuthSessionRequest request,
    base::OnceCallback<void(const user_data_auth::StartAuthSessionReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::StartAuthSessionReply reply;

  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      request.account_id().account_id(), request.flags());
  if (!auth_session) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthCreateFailedInStartAuthSession),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL));
    return;
  }

  reply.set_auth_session_id(auth_session->serialized_token());
  reply.set_user_exists(auth_session->user_exists());

  if (!auth_session->user_has_configured_credential() &&
      !auth_session->user_has_configured_auth_factor() &&
      auth_session->user_exists()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNotConfiguredInStartAuthSession),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                            ErrorAction::kDeleteVault, ErrorAction::kAuth}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_UNUSABLE_VAULT));
    return;
  }

  google::protobuf::Map<std::string, cryptohome::KeyData> proto_key_map(
      auth_session->key_label_data().begin(),
      auth_session->key_label_data().end());
  *(reply.mutable_key_label_data()) = proto_key_map;
  for (const auto& label_and_factor : auth_session->label_to_auth_factor()) {
    const std::unique_ptr<AuthFactor>& auth_factor = label_and_factor.second;
    std::optional<user_data_auth::AuthFactor> proto_factor = GetAuthFactorProto(
        auth_factor->metadata(), auth_factor->type(), auth_factor->label());
    if (proto_factor.has_value()) {
      *reply.add_auth_factors() = std::move(proto_factor.value());
    }
  }

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

user_data_auth::CryptohomeErrorCode
UserDataAuth::HandleAddCredentialForEphemeralVault(
    AuthorizationRequest request, const AuthSession* auth_session) {
  scoped_refptr<UserSession> session =
      GetOrCreateUserSession(auth_session->username());
  // Check the user is already mounted and the session is ephemeral.
  if (!session->IsActive()) {
    LOG(ERROR) << "AddCredential failed as ephemeral user is not mounted: "
               << auth_session->obfuscated_username();
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }
  if (!session->IsEphemeral()) {
    LOG(ERROR) << "AddCredential failed as user Session is not ephemeral: "
               << auth_session->obfuscated_username();
    return user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }

  auto credentials = std::make_unique<Credentials>(
      auth_session->username(), SecureBlob(request.key().secret()));
  // Everything else can be the default.
  credentials->set_key_data(request.key().data());
  session->SetCredentials(*credentials);
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

void UserDataAuth::AddCredentials(
    user_data_auth::AddCredentialsRequest request,
    base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::AddCredentialsReply reply;

  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(request.auth_session_id());
  if (!auth_session) {
    reply.set_error(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
    std::move(on_done).Run(reply);
    return;
  }

  if (request.authorization().key().data().type() ==
      KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    CryptohomeStatus status = InitAuthBlockUtilityForChallengeResponse(
        request.authorization(), auth_session->username());
    if (!status.ok()) {
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocUserDataAuthInitChalRespFailedInAddCredentials))
              .Wrap(std::move(status)));
      return;
    }
  }

  // Additional check if the user wants to add new credentials for an existing
  // user.
  if (request.add_more_credentials() && !auth_session->user_exists()) {
    reply.set_error(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED);
    std::move(on_done).Run(reply);
    return;
  }

  if (auth_session->ephemeral_user()) {
    reply.set_error(HandleAddCredentialForEphemeralVault(
        request.authorization(), auth_session));
    std::move(on_done).Run(reply);
  } else {
    // Add credentials using data in AuthorizationRequest and
    // auth_session_token.
    auto on_add_credential = base::BindOnce(
        &UserDataAuth::OnAddCredentialFinished<
            user_data_auth::AddCredentialsReply>,
        base::Unretained(this), auth_session, std::move(on_done));
    auth_session->AddCredentials(request, std::move(on_add_credential));
  }
}

void UserDataAuth::SetCredentialVerifierForUserSession(
    AuthSession* auth_session, bool override_existing_credential_verifier) {
  DCHECK(auth_session);

  scoped_refptr<UserSession> session = GetUserSession(auth_session->username());
  // Ensure valid session.
  if (!session) {
    LOG(WARNING) << "SetCredential failed as user session does not exist";
    return;
  }

  // Check the user is already mounted.
  if (!session->IsActive()) {
    LOG(WARNING) << "SetCredential failed as user session is not active.";
    return;
  }

  // Check if both UserSession and AuthSession match.
  if (session->IsEphemeral() != auth_session->ephemeral_user()) {
    LOG(WARNING) << "SetCredential failed as user session does not match "
                    "auth_session ephemeral status user: "
                 << auth_session->obfuscated_username();
    return;
  }

  // Ensure AuthSession is authenticated.
  if (auth_session->GetStatus() != AuthStatus::kAuthStatusAuthenticated) {
    LOG(WARNING) << "SetCredential failed as auth session is not authenticated "
                    "for user: "
                 << auth_session->obfuscated_username();
    return;
  }

  if (!session->HasCredentialVerifier() ||
      override_existing_credential_verifier) {
    session->SetCredentials(auth_session);
  }
}

template <typename AddKeyReply>
void UserDataAuth::OnAddCredentialFinished(
    AuthSession* auth_session,
    base::OnceCallback<void(const AddKeyReply&)> on_done,
    const AddKeyReply& reply) {
  if (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    SetCredentialVerifierForUserSession(
        auth_session, /*override_existing_credential_verifier=*/false);
  }
  std::move(on_done).Run(reply);
}

void UserDataAuth::OnUpdateCredentialFinished(
    AuthSession* auth_session,
    base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
        on_done,
    const user_data_auth::UpdateCredentialReply& reply) {
  if (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    SetCredentialVerifierForUserSession(
        auth_session, /*override_existing_credential_verifier=*/true);
  }
  std::move(on_done).Run(reply);
}

void UserDataAuth::UpdateCredential(
    user_data_auth::UpdateCredentialRequest request,
    base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::UpdateCredentialReply reply;
  CryptohomeStatusOr<AuthSession*> auth_session_status =
      GetAuthenticatedAuthSession(request.auth_session_id());
  if (!auth_session_status.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoAuthSessionInUpdateCredential))
            .Wrap(std::move(auth_session_status).status()));
    return;
  }
  // Update credentials using data in AuthorizationRequest and
  // auth_session_token.
  auto on_update_credential = base::BindOnce(
      &UserDataAuth::OnUpdateCredentialFinished, base::Unretained(this),
      auth_session_status.value(), std::move(on_done));

  auth_session_status.value()->UpdateCredential(
      request, std::move(on_update_credential));
  return;
}

void UserDataAuth::AuthenticateAuthSession(
    user_data_auth::AuthenticateAuthSessionRequest request,
    base::OnceCallback<
        void(const user_data_auth::AuthenticateAuthSessionReply&)> on_done) {
  AssertOnMountThread();

  user_data_auth::AuthenticateAuthSessionReply reply;

  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(request.auth_session_id());
  if (!auth_session) {
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthSessionNotFoundInAuthAuthSession),
                       ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                                       ErrorAction::kReboot}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));
    return;
  }

  if (request.authorization().key().data().type() ==
      KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    CryptohomeStatus status = InitAuthBlockUtilityForChallengeResponse(
        request.authorization(), auth_session->username());
    if (!status.ok()) {
      ReplyWithError(
          std::move(on_done), reply,
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(
                  kLocUserDataAuthAuthBlockUtilityNotValidForChallenge))
              .Wrap(std::move(status)));
      return;
    }
  }

  // Perform authentication using data in AuthorizationRequest and
  // auth_session_token.
  auth_session->Authenticate(
      request.authorization(),
      base::BindOnce(&ReplyWithAuthenticationResult<
                         user_data_auth::AuthenticateAuthSessionReply>,
                     auth_session, std::move(on_done)));
}

void UserDataAuth::InvalidateAuthSession(
    user_data_auth::InvalidateAuthSessionRequest request,
    base::OnceCallback<void(const user_data_auth::InvalidateAuthSessionReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::InvalidateAuthSessionReply reply;
  if (auth_session_manager_->RemoveAuthSession(request.auth_session_id())) {
    LOG(INFO) << "AuthSession: invalidated.";
  }

  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void UserDataAuth::ExtendAuthSession(
    user_data_auth::ExtendAuthSessionRequest request,
    base::OnceCallback<void(const user_data_auth::ExtendAuthSessionReply&)>
        on_done) {
  AssertOnMountThread();

  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(request.auth_session_id());
  user_data_auth::ExtendAuthSessionReply reply;
  if (!auth_session) {
    // Token lookup failed.
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthSessionNotFoundInExtendAuthSession),
                       ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                                       ErrorAction::kReboot}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));
    return;
  }

  // Extend specified AuthSession.
  auto timer_extension = base::Seconds(request.extension_duration());
  CryptohomeStatus ret = auth_session->ExtendTimeoutTimer(timer_extension);

  CryptohomeStatus err = OkStatus<CryptohomeError>();
  if (!ret.ok()) {
    // TODO(b/229688435): Wrap the error after AuthSession is migrated to use
    // CryptohomeError.
    err =
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthExtendFailedInExtendAuthSession))
            .Wrap(std::move(ret));
  }
  ReplyWithError(std::move(on_done), reply, std::move(err));
}

CryptohomeStatusOr<AuthSession*> UserDataAuth::GetAuthenticatedAuthSession(
    const std::string& auth_session_id) {
  AssertOnMountThread();

  // Check if the token refers to a valid AuthSession.
  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(auth_session_id);
  if (!auth_session) {
    LOG(ERROR) << "AuthSession not found.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInGetAuthedAS),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  }

  // Check if the AuthSession is properly authenticated.
  if (auth_session->GetStatus() != AuthStatus::kAuthStatusAuthenticated) {
    LOG(ERROR) << "AuthSession is not authenticated.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotAuthedInGetAuthedAS),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  return auth_session;
}

std::string UserDataAuth::SanitizedUserNameForSession(
    const std::string& auth_session_id) {
  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(auth_session_id);
  if (!auth_session) {
    return "";
  }
  return auth_session->obfuscated_username();
}

CryptohomeStatusOr<scoped_refptr<UserSession>>
UserDataAuth::GetMountableUserSession(AuthSession* auth_session) {
  AssertOnMountThread();

  const std::string& obfuscated_username = auth_session->obfuscated_username();

  // Check no guest is mounted.
  scoped_refptr<UserSession> guest_session = GetUserSession(guest_user_);
  if (guest_session && guest_session->IsActive()) {
    LOG(ERROR) << "Can not mount non-anonymous while guest session is active.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthGuestAlreadyMountedInGetMountableUS),
        ErrorActionSet({ErrorAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  }

  // Check the user is not already mounted.
  scoped_refptr<UserSession> session =
      GetOrCreateUserSession(auth_session->username());
  if (session->IsActive()) {
    LOG(ERROR) << "User is already mounted: " << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthSessionAlreadyMountedInGetMountableUS),
        ErrorActionSet({ErrorAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  }

  return session;
}

void UserDataAuth::PreMountHook(const std::string& obfuscated_username) {
  AssertOnMountThread();

  LOG(INFO) << "Started mounting for: " << obfuscated_username;

  // Any non-guest mount attempt triggers InstallAttributes finalization.
  // The return value is ignored as it is possible we're pre-ownership.
  // The next login will assure finalization if possible.
  if (install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    install_attrs_->Finalize();
  }
  // Remove all existing cryptohomes, except for the owner's one, if the
  // ephemeral users policy is on.
  // Note that a fresh policy value is read here, which in theory can conflict
  // with the one used for calculation of |mount_args.is_ephemeral|. However,
  // this inconsistency (whose probability is anyway pretty low in practice)
  // should only lead to insignificant transient glitches, like an attempt to
  // mount a non existing anymore cryptohome.
  if (homedirs_->AreEphemeralUsersEnabled()) {
    homedirs_->RemoveNonOwnerCryptohomes();
  }
}

void UserDataAuth::PostMountHook(scoped_refptr<UserSession> user_session,
                                 const MountStatus& status) {
  AssertOnMountThread();

  if (!status.ok()) {
    LOG(ERROR) << "Finished mounting with status code: " << status;
    return;
  }
  LOG(INFO) << "Mount succeeded.";
  InitializePkcs11(user_session.get());
}

EncryptedContainerType UserDataAuth::DbusEncryptionTypeToContainerType(
    user_data_auth::VaultEncryptionType type) {
  switch (type) {
    case user_data_auth::VaultEncryptionType::CRYPTOHOME_VAULT_ENCRYPTION_ANY:
      return EncryptedContainerType::kUnknown;
    case user_data_auth::VaultEncryptionType::
        CRYPTOHOME_VAULT_ENCRYPTION_ECRYPTFS:
      return EncryptedContainerType::kEcryptfs;
    case user_data_auth::VaultEncryptionType::
        CRYPTOHOME_VAULT_ENCRYPTION_FSCRYPT:
      return EncryptedContainerType::kFscrypt;
    case user_data_auth::VaultEncryptionType::
        CRYPTOHOME_VAULT_ENCRYPTION_DMCRYPT:
      return EncryptedContainerType::kDmcrypt;
    default:
      // Default cuz proto3 enum sentinels, that's why -_-
      return EncryptedContainerType::kUnknown;
  }
}

void UserDataAuth::PrepareGuestVault(
    user_data_auth::PrepareGuestVaultRequest request,
    base::OnceCallback<void(const user_data_auth::PrepareGuestVaultReply&)>
        on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Preparing guest vault";
  user_data_auth::PrepareGuestVaultReply reply;
  CryptohomeStatus status = PrepareGuestVaultImpl();
  reply.set_sanitized_username(SanitizeUserName(guest_user_));
  ReplyWithError(std::move(on_done), reply, status);
  return;
}

void UserDataAuth::PrepareEphemeralVault(
    user_data_auth::PrepareEphemeralVaultRequest request,
    base::OnceCallback<void(const user_data_auth::PrepareEphemeralVaultReply&)>
        on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Preparing ephemeral vault";
  user_data_auth::PrepareEphemeralVaultReply reply;
  CryptohomeStatus status =
      PrepareEphemeralVaultImpl(request.auth_session_id());
  reply.set_sanitized_username(
      SanitizedUserNameForSession(request.auth_session_id()));
  ReplyWithError(std::move(on_done), reply, status);
}

void UserDataAuth::PreparePersistentVault(
    user_data_auth::PreparePersistentVaultRequest request,
    base::OnceCallback<void(const user_data_auth::PreparePersistentVaultReply&)>
        on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Preparing persistent vault";
  CryptohomeVault::Options options = {
      .force_type =
          DbusEncryptionTypeToContainerType(request.encryption_type()),
      .block_ecryptfs = request.block_ecryptfs(),
  };
  CryptohomeStatus status =
      PreparePersistentVaultImpl(request.auth_session_id(), options);

  const std::string obfuscated_username =
      SanitizedUserNameForSession(request.auth_session_id());
  if (status.ok() && !obfuscated_username.empty()) {
    // Send UMA with VK stats once per successful mount operation.
    keyset_management_->RecordAllVaultKeysetMetrics(obfuscated_username);
  }
  user_data_auth::PreparePersistentVaultReply reply;
  reply.set_sanitized_username(obfuscated_username);
  ReplyWithError(std::move(on_done), reply, status);
}

void UserDataAuth::PrepareVaultForMigration(
    user_data_auth::PrepareVaultForMigrationRequest request,
    base::OnceCallback<
        void(const user_data_auth::PrepareVaultForMigrationReply&)> on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Preparing vault for migration";
  CryptohomeVault::Options options = {
      .migrate = true,
  };
  user_data_auth::PrepareVaultForMigrationReply reply;
  CryptohomeStatus status =
      PreparePersistentVaultImpl(request.auth_session_id(), options);
  reply.set_sanitized_username(
      SanitizedUserNameForSession(request.auth_session_id()));
  ReplyWithError(std::move(on_done), reply, status);
}

void UserDataAuth::CreatePersistentUser(
    user_data_auth::CreatePersistentUserRequest request,
    base::OnceCallback<void(const user_data_auth::CreatePersistentUserReply&)>
        on_done) {
  AssertOnMountThread();

  LOG(INFO) << "Creating persistent user";
  user_data_auth::CreatePersistentUserReply reply;

  // Record current time for timing how long CreatePersistentUserImpl will
  // take.
  auto start_time = base::TimeTicks::Now();

  StatusChain<CryptohomeError> ret =
      CreatePersistentUserImpl(request.auth_session_id());

  ReportTimerDuration(kCreatePersistentUserTimer, start_time, "");

  reply.set_sanitized_username(
      SanitizedUserNameForSession(request.auth_session_id()));
  ReplyWithError(std::move(on_done), reply, ret);
}

CryptohomeStatus UserDataAuth::PrepareGuestVaultImpl() {
  AssertOnMountThread();

  if (sessions_.size() != 0) {
    LOG(ERROR) << "Can not mount guest while other sessions are active.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthOtherSessionActiveInPrepareGuestVault),
        ErrorActionSet({ErrorAction::kReboot}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  scoped_refptr<UserSession> session = GetOrCreateUserSession(guest_user_);

  LOG(INFO) << "Started mounting for guest";
  ReportTimerStart(kMountGuestExTimer);
  MountStatus status = session->MountGuest();
  ReportTimerStop(kMountGuestExTimer);
  if (!status.ok()) {
    DCHECK(status->mount_error() != MOUNT_ERROR_NONE);
    LOG(ERROR) << "Finished mounting with status code: "
               << status->mount_error();
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthMountFailedInPrepareGuestVault))
        .Wrap(std::move(status));
  }
  LOG(INFO) << "Mount succeeded.";
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus UserDataAuth::PrepareEphemeralVaultImpl(
    const std::string& auth_session_id) {
  AssertOnMountThread();

  CryptohomeStatusOr<AuthSession*> auth_session_status =
      GetAuthenticatedAuthSession(auth_session_id);
  if (!auth_session_status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthNoAuthSessionInPrepareEphemeralVault))
        .Wrap(std::move(auth_session_status).status());
  }
  CryptohomeStatusOr<scoped_refptr<UserSession>> session_status =
      GetMountableUserSession(auth_session_status.value());
  if (!session_status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthGetSessionFailedInPrepareEphemeralVault))
        .Wrap(std::move(session_status).status());
  }

  const std::string& obfuscated_username =
      auth_session_status.value()->obfuscated_username();
  PreMountHook(obfuscated_username);
  ReportTimerStart(kMountExTimer);
  MountStatus mount_status = session_status.value()->MountEphemeral(
      auth_session_status.value()->username());
  ReportTimerStop(kMountExTimer);
  PostMountHook(session_status.value(), mount_status);
  if (!mount_status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthMountFailedInPrepareEphemeralVault))
        .Wrap(std::move(mount_status).status());
  }
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus UserDataAuth::PreparePersistentVaultImpl(
    const std::string& auth_session_id,
    const CryptohomeVault::Options& vault_options) {
  AssertOnMountThread();

  CryptohomeStatusOr<AuthSession*> auth_session_status =
      GetAuthenticatedAuthSession(auth_session_id);
  if (!auth_session_status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthNoAuthSessionInPreparePersistentVault))
        .Wrap(std::move(auth_session_status).status());
  }

  const std::string& obfuscated_username =
      auth_session_status.value()->obfuscated_username();
  if (!homedirs_->Exists(obfuscated_username)) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthNonExistentInPreparePersistentVault),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kDeleteVault, ErrorAction::kReboot,
                        ErrorAction::kPowerwash}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
  }

  CryptohomeStatusOr<scoped_refptr<UserSession>> session_status =
      GetMountableUserSession(auth_session_status.value());
  if (!session_status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthGetSessionFailedInPreparePersistentVault))
        .Wrap(std::move(session_status).status());
  }

  PreMountHook(obfuscated_username);
  ReportTimerStart(kMountExTimer);
  MountStatus mount_status = session_status.value()->MountVault(
      auth_session_status.value()->username(),
      auth_session_status.value()->file_system_keyset(), vault_options);
  ReportTimerStop(kMountExTimer);
  PostMountHook(session_status.value(), mount_status);
  if (!mount_status.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthMountFailedInPreparePersistentVault))
        .Wrap(std::move(mount_status).status());
  }

  SetCredentialVerifierForUserSession(
      auth_session_status.value(),
      /*override_existing_credential_verifier=*/false);
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus UserDataAuth::CreatePersistentUserImpl(
    const std::string& auth_session_id) {
  AssertOnMountThread();

  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(auth_session_id);
  if (!auth_session) {
    LOG(ERROR) << "AuthSession not found.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthSessionNotFoundInCreatePersistentUser),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  }

  const std::string& obfuscated_username = auth_session->obfuscated_username();

  // This checks presence of the actual encrypted vault. We fail if Create is
  // called while actual persistent vault is present.
  auto exists_or = homedirs_->CryptohomeExists(obfuscated_username);
  if (exists_or.ok() && exists_or.value()) {
    LOG(ERROR) << "User already exists: " << obfuscated_username;
    // TODO(b/208898186, dlunev): replace with a more appropriate error
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthUserExistsInCreatePersistentUser),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  }

  if (!exists_or.ok()) {
    MountError mount_error = exists_or.status()->error();
    LOG(ERROR) << "Failed to query vault existance for: " << obfuscated_username
               << ", code: " << mount_error;
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserDataAuthCheckExistsFailedInCreatePersistentUser),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        mount_error, MountErrorToCryptohomeError(mount_error));
  }

  // This checks and creates if missing the user's directory in shadow root.
  // We need to disambiguate with vault presence, because it is possible that
  // we have an empty shadow root directory for the user left behind after
  // removing a profile (due to a bug or for some other reasons). To avoid weird
  // failures in the case, just let the creation succeed, since the user is
  // effectively not there. Eventually |Exists| will check for the presence of
  // the USS/auth factors to determine if the user is intended to be there.
  // This call will not create the actual volume (for efficiency, idempotency,
  // and because that would require going the full sequence of mount and unmount
  // because of ecryptfs possibility).
  if (!homedirs_->Exists(obfuscated_username) &&
      !homedirs_->Create(auth_session->username())) {
    LOG(ERROR) << "Failed to create shadow directory for: "
               << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserDataAuthCreateFailedInCreatePersistentUser),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kPowerwash}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  // Let the auth session perform any finalization operations for a newly
  // created user.
  CryptohomeStatus ret = auth_session->OnUserCreated();
  if (!ret.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUserDataAuthFinalizeFailedInCreatePersistentUser))
        .Wrap(std::move(ret));
  }
  return OkStatus<CryptohomeError>();
}

void UserDataAuth::AddAuthFactor(
    user_data_auth::AddAuthFactorRequest request,
    base::OnceCallback<void(const user_data_auth::AddAuthFactorReply&)>
        on_done) {
  AssertOnMountThread();
  // TODO(b/3319388): Implement AddAuthFactor.
  user_data_auth::AddAuthFactorReply reply;
  CryptohomeStatusOr<AuthSession*> auth_session_status =
      GetAuthenticatedAuthSession(request.auth_session_id());
  if (!auth_session_status.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoAuthSessionInAddAuthFactor))
            .Wrap(std::move(auth_session_status).status()));
    return;
  }

  auth_session_status.value()->AddAuthFactor(request, std::move(on_done));
}

void UserDataAuth::AuthenticateAuthFactor(
    user_data_auth::AuthenticateAuthFactorRequest request,
    base::OnceCallback<void(const user_data_auth::AuthenticateAuthFactorReply&)>
        on_done) {
  AssertOnMountThread();
  user_data_auth::AuthenticateAuthFactorReply reply;

  AuthSession* const auth_session =
      auth_session_manager_->FindAuthSession(request.auth_session_id());
  if (!auth_session) {
    LOG(ERROR) << "Invalid AuthSession token provided.";
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthSessionNotFoundInAuthAuthFactor),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));
    return;
  }

  auth_session->AuthenticateAuthFactor(
      request, base::BindOnce(&ReplyWithAuthenticationResult<
                                  user_data_auth::AuthenticateAuthFactorReply>,
                              auth_session, std::move(on_done)));
}

void UserDataAuth::UpdateAuthFactor(
    user_data_auth::UpdateAuthFactorRequest request,
    base::OnceCallback<void(const user_data_auth::UpdateAuthFactorReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::UpdateAuthFactorReply reply;

  CryptohomeStatusOr<AuthSession*> auth_session_status =
      GetAuthenticatedAuthSession(request.auth_session_id());
  if (!auth_session_status.ok()) {
    ReplyWithError(
        std::move(on_done), reply,
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocUserDataAuthNoAuthSessionInUpdateAuthFactor))
            .Wrap(std::move(auth_session_status).status()));
    return;
  }

  auth_session_status.value()->UpdateAuthFactor(request, std::move(on_done));
}

void UserDataAuth::RemoveAuthFactor(
    user_data_auth::RemoveAuthFactorRequest request,
    base::OnceCallback<void(const user_data_auth::RemoveAuthFactorReply&)>
        on_done) {
  AssertOnMountThread();
  user_data_auth::RemoveAuthFactorReply reply;

  CryptohomeStatusOr<AuthSession*> auth_session_status =
      GetAuthenticatedAuthSession(request.auth_session_id());
  if (!auth_session_status.ok()) {
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthSessionNotFoundInRemoveAuthFactor))
                       .Wrap(std::move(auth_session_status).status()));
    return;
  }

  auth_session_status.value()->RemoveAuthFactor(request, std::move(on_done));
}

void UserDataAuth::ListAuthFactors(
    user_data_auth::ListAuthFactorsRequest request,
    base::OnceCallback<void(const user_data_auth::ListAuthFactorsReply&)>
        on_done) {
  AssertOnMountThread();
  user_data_auth::ListAuthFactorsReply reply;

  // Compute the raw and sanitized user name from the request.
  const std::string& username = request.account_id().account_id();
  std::string obfuscated_username = SanitizeUserName(username);

  // If the user does not exist, we cannot return auth factors for it.
  if (!keyset_management_->UserExists(obfuscated_username)) {
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthUserNonexistentInListAuthFactors),
                       ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Populate the response with all of the auth factors we can find. For
  // compatibility we assume that if the user somehow has both USS and vault
  // keysets, that the VKs should take priority.
  AuthFactorStorageType storage_type = AuthFactorStorageType::kVaultKeyset;
  AuthFactorVaultKeysetConverter converter(keyset_management_);
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  converter.VaultKeysetsToAuthFactors(username, auth_factor_map);
  for (const auto& [unused, auth_factor] : auth_factor_map) {
    auto auth_factor_proto = GetAuthFactorProto(
        auth_factor->metadata(), auth_factor->type(), auth_factor->label());
    if (auth_factor_proto) {
      *reply.add_configured_auth_factors() = std::move(*auth_factor_proto);
    }
  }
  // If the auth factor map is empty then there were no VK keys, try USS.
  if (auth_factor_map.empty()) {
    LoadUserAuthFactorProtos(auth_factor_manager_, obfuscated_username,
                             reply.mutable_configured_auth_factors());
    // We assume USS is available either if there are already auth factors in
    // USS, or if there are no auth factors but the experiment is enabled.
    if (!reply.configured_auth_factors().empty() ||
        IsUserSecretStashExperimentEnabled()) {
      storage_type = AuthFactorStorageType::kUserSecretStash;
    }
  }

  // Turn the list of configured types into a set that we can use for computing
  // the list of supported factors.
  std::set<AuthFactorType> configured_types;
  for (const auto& configured_factor : reply.configured_auth_factors()) {
    if (auto type = AuthFactorTypeFromProto(configured_factor.type())) {
      configured_types.insert(*type);
    }
  }

  // Determine what auth factors are supported by going through the entire set
  // of auth factor types and checking each one.
  for (int raw_type = user_data_auth::AuthFactorType_MIN;
       raw_type <= user_data_auth::AuthFactorType_MAX; ++raw_type) {
    if (!user_data_auth::AuthFactorType_IsValid(raw_type)) {
      continue;
    }
    auto proto_type = static_cast<user_data_auth::AuthFactorType>(raw_type);
    std::optional<AuthFactorType> type = AuthFactorTypeFromProto(proto_type);
    if (!type) {
      continue;
    }
    if (auth_block_utility_->IsAuthFactorSupported(*type, storage_type,
                                                   configured_types)) {
      reply.add_supported_auth_factors(proto_type);
    }
  }

  // Successfully completed, send the response with OK.
  ReplyWithError(std::move(on_done), reply, OkStatus<CryptohomeError>());
}

void UserDataAuth::GetAuthSessionStatus(
    user_data_auth::GetAuthSessionStatusRequest request,
    base::OnceCallback<void(const user_data_auth::GetAuthSessionStatusReply&)>
        on_done) {
  AssertOnMountThread();
  user_data_auth::GetAuthSessionStatusReply reply;

  AuthSession* auth_session =
      auth_session_manager_->FindAuthSession(request.auth_session_id());
  if (!auth_session) {
    reply.set_error(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
    LOG(ERROR) << "GetAuthSessionStatus: AuthSession not found.";
    return;
  }
  GetAuthSessionStatusImpl(auth_session, reply);
}

void UserDataAuth::GetAuthSessionStatusImpl(
    AuthSession* auth_session,
    user_data_auth::GetAuthSessionStatusReply& reply) {
  DCHECK(auth_session);
  // Default is invalid unless there is evidence otherwise.
  reply.set_status(user_data_auth::AUTH_SESSION_STATUS_INVALID_AUTH_SESSION);

  if (auth_session->GetStatus() ==
      AuthStatus::kAuthStatusFurtherFactorRequired) {
    reply.set_status(
        user_data_auth::AUTH_SESSION_STATUS_FURTHER_FACTOR_REQUIRED);
  } else if (auth_session->GetStatus() ==
             AuthStatus::kAuthStatusAuthenticated) {
    reply.set_time_left(auth_session->GetRemainingTime().InSeconds());
    reply.set_status(user_data_auth::AUTH_SESSION_STATUS_AUTHENTICATED);
  }
}

bool UserDataAuth::GetRecoveryRequest(
    user_data_auth::GetRecoveryRequestRequest request,
    base::OnceCallback<void(const user_data_auth::GetRecoveryRequestReply&)>
        on_done) {
  AssertOnMountThread();

  user_data_auth::GetRecoveryRequestReply reply;
  AuthSession* const auth_session =
      auth_session_manager_->FindAuthSession(request.auth_session_id());
  if (!auth_session) {
    LOG(ERROR) << "Invalid AuthSession token provided.";
    ReplyWithError(std::move(on_done), reply,
                   MakeStatus<CryptohomeError>(
                       CRYPTOHOME_ERR_LOC(
                           kLocUserDataAuthSessionNotFoundInGetRecoveryRequest),
                       ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                                       ErrorAction::kReboot}),
                       user_data_auth::CryptohomeErrorCode::
                           CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));
    return false;
  }
  return auth_session->GetRecoveryRequest(request, std::move(on_done));
}

}  // namespace cryptohome
