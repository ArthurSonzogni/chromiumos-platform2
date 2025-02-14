// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_INSTALL_PLAN_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_INSTALL_PLAN_H_

#include <string>
#include <vector>

#include <brillo/secure_blob.h>

#include "update_engine/client_library/include/update_engine/update_status.h"
#include "update_engine/common/action.h"
#include "update_engine/common/boot_control_interface.h"

// InstallPlan is a simple struct that contains relevant info for many
// parts of the update system about the install that should happen.
namespace chromeos_update_engine {

enum class InstallPayloadType {
  kUnknown,
  kFull,
  kDelta,
};

std::string InstallPayloadTypeToString(InstallPayloadType type);

enum class DeferUpdateAction {
  kOff,
  kHold,
  kApplyAndReboot,
  kApplyAndShutdown,
};

struct InstallPlan {
  bool operator==(const InstallPlan& that) const;
  bool operator!=(const InstallPlan& that) const;

  void Dump() const;
  std::string ToString() const;

  // Loads the |source_path| and |target_path| of all |partitions| based on the
  // |source_slot| and |target_slot| if available. Returns whether it succeeded
  // to load all the partitions for the valid slots.
  bool LoadPartitionsFromSlots(BootControlInterface* boot_control);

  bool is_resume{false};
  std::string download_url;  // url to download from
  std::string version;       // version we are installing.

  struct Payload {
    std::vector<std::string> payload_urls;  // URLs to download the payload
    uint64_t size = 0;                      // size of the payload
    uint64_t metadata_size = 0;             // size of the metadata
    std::string metadata_signature;  // signature of the metadata in base64
    brillo::Blob hash;               // SHA256 hash of the payload
    InstallPayloadType type{InstallPayloadType::kUnknown};
    std::string fp;      // fingerprint value unique to the payload
    std::string app_id;  // App ID of the payload
    // Only download manifest and fill in partitions in install plan without
    // apply the payload if true. Will be set by DownloadAction when resuming
    // multi-payload.
    bool already_applied = false;

    bool operator==(const Payload& that) const {
      return payload_urls == that.payload_urls && size == that.size &&
             metadata_size == that.metadata_size &&
             metadata_signature == that.metadata_signature &&
             hash == that.hash && type == that.type &&
             already_applied == that.already_applied && fp == that.fp &&
             app_id == that.app_id;
    }
  };
  std::vector<Payload> payloads;

  // The partition slots used for the update.
  BootControlInterface::Slot source_slot{BootControlInterface::kInvalidSlot};
  BootControlInterface::Slot target_slot{BootControlInterface::kInvalidSlot};
  BootControlInterface::Slot minios_target_slot{
      BootControlInterface::kInvalidSlot};
  BootControlInterface::Slot minios_src_slot{
      BootControlInterface::kInvalidSlot};

  // The vector below is used for partition verification. The flow is:
  //
  // 1. DownloadAction fills in the expected source and target partition sizes
  // and hashes based on the manifest.
  //
  // 2. FilesystemVerifierAction computes and verifies the partition sizes and
  // hashes against the expected values.
  struct Partition {
    bool operator==(const Partition& that) const;

    // The name of the partition.
    std::string name;

    std::string source_path;
    uint64_t source_size{0};
    brillo::Blob source_hash;

    std::string target_path;
    uint64_t target_size{0};
    brillo::Blob target_hash;
    uint32_t block_size{0};

    // Whether we should run the postinstall script from this partition and the
    // postinstall parameters.
    bool run_postinstall{false};
    std::string postinstall_path;
    std::string filesystem_type;
    bool postinstall_optional{false};

    // Verity hash tree and FEC config. See update_metadata.proto for details.
    // All offsets and sizes are in bytes.
    uint64_t hash_tree_data_offset{0};
    uint64_t hash_tree_data_size{0};
    uint64_t hash_tree_offset{0};
    uint64_t hash_tree_size{0};
    std::string hash_tree_algorithm;
    brillo::Blob hash_tree_salt;

    uint64_t fec_data_offset{0};
    uint64_t fec_data_size{0};
    uint64_t fec_offset{0};
    uint64_t fec_size{0};
    uint32_t fec_roots{0};
  };
  std::vector<Partition> partitions;

  // True if payload hash checks are mandatory based on the system state and
  // the Omaha response.
  bool hash_checks_mandatory{true};

  // True if the payload signature checks are mandatory based on the type of the
  // image installed, e.g. official images should have this ON.
  bool signature_checks_mandatory{true};

  // True if Powerwash is required on reboot after applying the payload.
  // False otherwise.
  bool powerwash_required{false};

  // True if the updated slot should be marked active on success.
  // False otherwise.
  bool switch_slot_on_reboot{true};

  // True if MiniOS is being updated and the active slot is changing.
  bool switch_minios_slot{false};

  // True if the update should run its post-install step.
  // False otherwise.
  bool run_post_install{true};

  // True if this update is a rollback.
  bool is_rollback{false};

  // True if this rollback should preserve some system data.
  bool rollback_data_save_requested{false};

  // True if the update should write verity.
  // False otherwise.
  bool write_verity{true};

  // If not blank, a base-64 encoded representation of the PEM-encoded
  // public key in the response.
  std::string public_key_rsa;

  // The name of dynamic partitions not included in the payload. Only used
  // for partial updates.
  std::vector<std::string> untouched_dynamic_partitions;

  // True if download can be canceled due to restricted time interval.
  bool can_download_be_canceled{false};

  // Indicates the type of update.
  update_engine::UpdateUrgencyInternal update_urgency{
      update_engine::UpdateUrgencyInternal::REGULAR};

  // The defer update action to perform during post installation.
  DeferUpdateAction defer_update_action{DeferUpdateAction::kOff};
};

class InstallPlanAction;

template <>
class ActionTraits<InstallPlanAction> {
 public:
  // Takes the install plan as input
  typedef InstallPlan InputObjectType;
  // Passes the install plan as output
  typedef InstallPlan OutputObjectType;
};

// Basic action that only receives and sends Install Plans.
// Can be used to construct an Install Plan to send to any other Action that
// accept an InstallPlan.
class InstallPlanAction : public Action<InstallPlanAction> {
 public:
  InstallPlanAction() {}
  explicit InstallPlanAction(const InstallPlan& install_plan)
      : install_plan_(install_plan) {}
  InstallPlanAction(const InstallPlanAction&) = delete;
  InstallPlanAction& operator=(const InstallPlanAction&) = delete;

  void PerformAction() override {
    if (HasOutputPipe()) {
      SetOutputObject(install_plan_);
    }
    processor_->ActionComplete(this, ErrorCode::kSuccess);
  }

  InstallPlan* install_plan() { return &install_plan_; }

  static std::string StaticType() { return "InstallPlanAction"; }
  std::string Type() const override { return StaticType(); }

  typedef ActionTraits<InstallPlanAction>::InputObjectType InputObjectType;
  typedef ActionTraits<InstallPlanAction>::OutputObjectType OutputObjectType;

 protected:
  InstallPlan install_plan_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_INSTALL_PLAN_H_
