// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/device_user.h"
#include "secagentd/plugins.h"
#include "secagentd/proto/security_xdr_events.pb.h"

namespace secagentd {
namespace pb = cros_xdr::reporting;

FilePlugin::FilePlugin(
    scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<ProcessCacheInterface> process_cache,
    scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
    scoped_refptr<DeviceUserInterface> device_user,
    uint32_t batch_interval_s)
    : weak_ptr_factory_(this),
      process_cache_(process_cache),
      policies_features_broker_(policies_features_broker),
      device_user_(device_user),
      batch_sender_(std::make_unique<BatchSender<std::string,
                                                 pb::XdrFileEvent,
                                                 pb::FileEventAtomicVariant>>(
          base::BindRepeating(
              [](const cros_xdr::reporting::FileEventAtomicVariant&)
                  -> std::string {
                // TODO(b:282814056): Make hashing function optional
                //  for batch_sender then drop this. Not all users
                //  of batch_sender need the visit functionality.
                return "";
              }),
          message_sender,
          reporting::Destination::CROS_SECURITY_FILE,
          batch_interval_s)),
      bpf_skeleton_helper_(
          std::make_unique<BpfSkeletonHelper<Types::BpfSkeleton::kFile>>(
              bpf_skeleton_factory, batch_interval_s)) {
  CHECK(message_sender != nullptr);
  CHECK(process_cache != nullptr);
  CHECK(bpf_skeleton_factory);
}

absl::Status FilePlugin::Activate() {
  struct BpfCallbacks callbacks;
  callbacks.ring_buffer_event_callback = base::BindRepeating(
      &FilePlugin::HandleRingBufferEvent, weak_ptr_factory_.GetWeakPtr());

  absl::Status status = bpf_skeleton_helper_->LoadAndAttach(callbacks);
  if (status == absl::OkStatus()) {
    batch_sender_->Start();
  }
  return status;
}

absl::Status FilePlugin::Deactivate() {
  return bpf_skeleton_helper_->DetachAndUnload();
}

bool FilePlugin::IsActive() const {
  return bpf_skeleton_helper_->IsAttached();
}

std::string FilePlugin::GetName() const {
  return "File";
}

void FilePlugin::HandleRingBufferEvent(const bpf::cros_event& bpf_event) {
  auto atomic_event = std::make_unique<pb::FileEventAtomicVariant>();
  if (bpf_event.type != bpf::kFileEvent) {
    LOG(ERROR) << "Unexpected BPF event type.";
    return;
  }

  // const bpf::cros_file_event& fe = bpf_event.data.file_event;
  // TODO(princya): convert to proto

  device_user_->GetDeviceUserAsync(
      base::BindOnce(&FilePlugin::OnDeviceUserRetrieved,
                     weak_ptr_factory_.GetWeakPtr(), std::move(atomic_event)));
}

void FilePlugin::EnqueueBatchedEvent(
    std::unique_ptr<pb::FileEventAtomicVariant> atomic_event) {
  batch_sender_->Enqueue(std::move(atomic_event));
}

void FilePlugin::OnDeviceUserRetrieved(
    std::unique_ptr<pb::FileEventAtomicVariant> atomic_event,
    const std::string& device_user) {
  atomic_event->mutable_common()->set_device_user(device_user);
  EnqueueBatchedEvent(std::move(atomic_event));
}

std::unique_ptr<cros_xdr::reporting::FileReadEvent> FilePlugin::MakeReadEvent(
    const secagentd::bpf::cros_file_event& close_event) const {
  auto read_proto = std::make_unique<pb::FileReadEvent>();
  return read_proto;
}

std::unique_ptr<cros_xdr::reporting::FileModifyEvent>
FilePlugin::MakeModifyEvent(
    const secagentd::bpf::cros_file_event& close_event) const {
  auto modify_proto = std::make_unique<pb::FileModifyEvent>();
  return modify_proto;
}

std::unique_ptr<cros_xdr::reporting::FileModifyEvent>
FilePlugin::MakeAttributeModifyEvent(
    const secagentd::bpf::cros_file_event& attribute_modify_event) const {
  auto modify_proto = std::make_unique<pb::FileModifyEvent>();
  return modify_proto;
}

}  // namespace secagentd
