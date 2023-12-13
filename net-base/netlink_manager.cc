// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/netlink_manager.h"

#include <errno.h>
#include <sys/select.h>

#include <memory>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>

#include "net-base/attribute_list.h"
#include "net-base/generic_netlink_message.h"
#include "net-base/netlink_message.h"
#include "net-base/netlink_packet.h"

namespace net_base {

namespace {
base::LazyInstance<NetlinkManager>::DestructorAtExit g_netlink_manager =
    LAZY_INSTANCE_INITIALIZER;
}  // namespace

const int NetlinkManager::kMaxNlMessageRetries = 1;

NetlinkManager::NetlinkResponseHandler::NetlinkResponseHandler(
    const NetlinkManager::NetlinkAckHandler& ack_handler,
    const NetlinkManager::NetlinkAuxiliaryMessageHandler& error_handler)
    : ack_handler_(ack_handler), error_handler_(error_handler) {}

NetlinkManager::NetlinkResponseHandler::~NetlinkResponseHandler() = default;

void NetlinkManager::NetlinkResponseHandler::HandleError(
    AuxiliaryMessageType type, const NetlinkMessage* netlink_message) const {
  if (!error_handler_.is_null())
    error_handler_.Run(type, netlink_message);
}

bool NetlinkManager::NetlinkResponseHandler::HandleAck() const {
  if (!ack_handler_.is_null()) {
    // Default behavior is not to remove callbacks. In the case where the
    // callback is not successfully invoked, this is safe as it does not
    // prevent any further responses from behind handled.
    bool remove_callbacks = false;
    ack_handler_.Run(&remove_callbacks);
    // If there are no other handlers other than the Ack handler, then force
    // the callback to be removed after handling the Ack.
    return remove_callbacks || error_handler_.is_null();
  } else {
    // If there is no Ack handler, do not delete registered callbacks
    // for this function because we are not explicitly told to do so.
    return false;
  }
}

class ControlResponseHandler : public NetlinkManager::NetlinkResponseHandler {
 public:
  ControlResponseHandler(
      const NetlinkManager::NetlinkAckHandler& ack_handler,
      const NetlinkManager::NetlinkAuxiliaryMessageHandler& error_handler,
      const NetlinkManager::ControlNetlinkMessageHandler& handler)
      : NetlinkManager::NetlinkResponseHandler(ack_handler, error_handler),
        handler_(handler) {}
  ControlResponseHandler(const ControlResponseHandler&) = delete;
  ControlResponseHandler& operator=(const ControlResponseHandler&) = delete;

  bool HandleMessage(const NetlinkMessage& netlink_message) const override {
    if (netlink_message.message_type() !=
        ControlNetlinkMessage::GetMessageType()) {
      LOG(ERROR) << "Message is type " << netlink_message.message_type()
                 << ", not " << ControlNetlinkMessage::GetMessageType()
                 << " (Control).";
      return false;
    }
    if (!handler_.is_null()) {
      const ControlNetlinkMessage* message =
          static_cast<const ControlNetlinkMessage*>(&netlink_message);
      handler_.Run(*message);
    }
    return true;
  }

  bool HandleAck() const override {
    if (handler_.is_null()) {
      return NetlinkManager::NetlinkResponseHandler::HandleAck();
    } else {
      bool remove_callbacks = false;
      NetlinkManager::NetlinkResponseHandler::ack_handler_.Run(
          &remove_callbacks);
      return remove_callbacks;
    }
  }

 private:
  NetlinkManager::ControlNetlinkMessageHandler handler_;
};

NetlinkManager::MessageType::MessageType()
    : family_id(NetlinkMessage::kIllegalMessageType) {}

NetlinkManager::NetlinkManager()
    : weak_ptr_factory_(this), dump_pending_(false) {}

NetlinkManager::~NetlinkManager() = default;

NetlinkManager* NetlinkManager::GetInstance() {
  return g_netlink_manager.Pointer();
}

void NetlinkManager::Reset(bool full) {
  ClearBroadcastHandlers();
  message_handlers_.clear();
  message_types_.clear();
  while (!pending_messages_.empty()) {
    pending_messages_.pop();
  }
  pending_dump_timeout_callback_.Cancel();
  resend_dump_message_callback_.Cancel();
  dump_pending_ = false;
  if (full) {
    sock_watcher_.reset();
    sock_.reset();
  }
}

void NetlinkManager::OnNewFamilyMessage(const ControlNetlinkMessage& message) {
  uint16_t family_id;
  std::string family_name;

  if (!message.const_attributes()->GetU16AttributeValue(CTRL_ATTR_FAMILY_ID,
                                                        &family_id)) {
    LOG(ERROR) << __func__ << ": Couldn't get family_id attribute";
    return;
  }

  if (!message.const_attributes()->GetStringAttributeValue(
          CTRL_ATTR_FAMILY_NAME, &family_name)) {
    LOG(ERROR) << __func__ << ": Couldn't get family_name attribute";
    return;
  }

  VLOG(2) << "Socket family '" << family_name << "' has id=" << family_id;

  // Extract the available multicast groups from the message.
  AttributeListConstRefPtr multicast_groups;
  if (message.const_attributes()->ConstGetNestedAttributeList(
          CTRL_ATTR_MCAST_GROUPS, &multicast_groups)) {
    AttributeListConstRefPtr current_group;

    for (int i = 1;
         multicast_groups->ConstGetNestedAttributeList(i, &current_group);
         ++i) {
      std::string group_name;
      uint32_t group_id;
      if (!current_group->GetStringAttributeValue(CTRL_ATTR_MCAST_GRP_NAME,
                                                  &group_name)) {
        LOG(WARNING) << "Expected CTRL_ATTR_MCAST_GRP_NAME, found none";
        continue;
      }
      if (!current_group->GetU32AttributeValue(CTRL_ATTR_MCAST_GRP_ID,
                                               &group_id)) {
        LOG(WARNING) << "Expected CTRL_ATTR_MCAST_GRP_ID, found none";
        continue;
      }
      VLOG(2) << "  Adding group '" << group_name << "' = " << group_id;
      message_types_[family_name].groups[group_name] = group_id;
    }
  }

  message_types_[family_name].family_id = family_id;
}

std::string NetlinkManager::GetRawMessage(const NetlinkMessage* raw_message) {
  if (raw_message) {
    return raw_message->ToString();
  }
  return "<none>";
}

// static
void NetlinkManager::OnNetlinkMessageError(AuxiliaryMessageType type,
                                           const NetlinkMessage* raw_message) {
  switch (type) {
    case kErrorFromKernel:
      if (!raw_message) {
        LOG(ERROR) << "Unknown error from kernel.";
        return;
      }
      if (raw_message->message_type() == ErrorAckMessage::GetMessageType()) {
        const ErrorAckMessage* error_ack_message =
            static_cast<const ErrorAckMessage*>(raw_message);
        // error_ack_message->error() should be non-zero (i.e. not an ACK),
        // since ACKs would be routed to a NetlinkAckHandler in
        // NetlinkManager::OnNlMessageReceived.
        LOG(ERROR) << __func__
                   << ": Message (seq: " << error_ack_message->sequence_number()
                   << ") failed: " << error_ack_message->ToString();
      }
      return;

    case kUnexpectedResponseType:
      LOG(ERROR) << "Message not handled by regular message handler: "
                 << GetRawMessage(raw_message);
      return;

    case kTimeoutWaitingForResponse:
      LOG(WARNING) << "Timeout waiting for response: "
                   << GetRawMessage(raw_message);
      return;

    case kDone:
      VLOG(1) << __func__ << ": received kDone: " << GetRawMessage(raw_message);
      return;
  }

  LOG(ERROR) << "Unexpected auxiliary message type: " << type
             << ", message: " << GetRawMessage(raw_message);
}

bool NetlinkManager::Init() {
  // Install message factory for control class of messages, which has
  // statically-known message type.
  message_factory_.AddFactoryMethod(
      ControlNetlinkMessage::kMessageType,
      base::BindRepeating(&ControlNetlinkMessage::CreateMessage));
  if (!sock_) {
    sock_ = NetlinkSocket::Create();
    if (!sock_) {
      LOG(ERROR) << "Failed to create netlink socket";
      return false;
    }
  }
  return true;
}

void NetlinkManager::Start() {
  if (!sock_) {
    LOG(ERROR) << "The netlink socket hasn't been initialized";
    return;
  }

  // Create an watcher for receiving messages on the netlink socket.
  sock_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      sock_->file_descriptor(),
      // base::Unretained() is safe because |sock_watcher_| is owned by |*this|.
      base::BindRepeating(&NetlinkManager::OnReadable, base::Unretained(this)));
  if (sock_watcher_ == nullptr) {
    LOG(ERROR) << "Failed on watching the netlink socket";
  }
}

void NetlinkManager::OnReadable() {
  std::vector<uint8_t> message;
  if (sock_->RecvMessage(&message)) {
    OnRawNlMessageReceived(message);
  } else {
    PLOG(ERROR) << "NetlinkManager's netlink Socket read returns error";
  }
}

uint16_t NetlinkManager::GetFamily(
    const std::string& name,
    const NetlinkMessageFactory::FactoryMethod& message_factory) {
  MessageType& message_type = message_types_[name];
  if (message_type.family_id != NetlinkMessage::kIllegalMessageType) {
    return message_type.family_id;
  }
  if (!sock_) {
    LOG(FATAL) << "Must call |Init| before this method.";
    return false;
  }

  GetFamilyMessage msg;
  if (!msg.attributes()->SetStringAttributeValue(CTRL_ATTR_FAMILY_NAME, name)) {
    LOG(ERROR) << "Couldn't set string attribute";
    return false;
  }
  SendControlMessage(
      &msg,
      base::BindRepeating(&NetlinkManager::OnNewFamilyMessage,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&NetlinkManager::OnAckDoNothing),
      base::BindRepeating(&NetlinkManager::OnNetlinkMessageError));

  // Wait for a response.  The code absolutely needs family_ids for its
  // message types so we do a synchronous wait.  It's OK to do this because
  // a) libnl does a synchronous wait (so there's prior art), b) waiting
  // asynchronously would add significant and unnecessary complexity to the
  // code that deals with pending messages that could, potentially, be waiting
  // for a message type, and c) it really doesn't take very long for the
  // GETFAMILY / NEWFAMILY transaction to transpire (this transaction was timed
  // over 20 times and found a maximum duration of 11.1 microseconds and an
  // average of 4.0 microseconds).
  const base::TimeTicks end_time =
      base::TimeTicks::Now() + kMaximumNewFamilyTimeout;

  while (true) {
    const base::TimeDelta timeout = end_time - base::TimeTicks::Now();
    if (!timeout.is_positive()) {
      break;
    }

    // Wait with timeout for a message from the netlink socket.
    const int result = sock_->WaitForRead(timeout);
    if (result < 0) {
      PLOG(ERROR) << "Select failed";
      return NetlinkMessage::kIllegalMessageType;
    }
    if (result == 0) {
      LOG(WARNING) << "Timed out waiting for family_id for family '" << name
                   << "'.";
      return NetlinkMessage::kIllegalMessageType;
    }

    // Read and process any messages.
    std::vector<uint8_t> received;
    sock_->RecvMessage(&received);
    OnRawNlMessageReceived(received);
    if (message_type.family_id != NetlinkMessage::kIllegalMessageType) {
      uint16_t family_id = message_type.family_id;
      if (family_id != NetlinkMessage::kIllegalMessageType) {
        message_factory_.AddFactoryMethod(family_id, message_factory);
      }
      return message_type.family_id;
    }
  }

  LOG(ERROR) << "Timed out waiting for family_id for family '" << name << "'.";
  return NetlinkMessage::kIllegalMessageType;
}

bool NetlinkManager::AddBroadcastHandler(const NetlinkMessageHandler& handler) {
  if (FindBroadcastHandler(handler)) {
    LOG(WARNING) << "Trying to re-add a handler";
    return false;  // Should only be one copy in the list.
  }
  if (handler.is_null()) {
    LOG(WARNING) << "Trying to add a NULL handler";
    return false;
  }
  // And add the handler to the list.
  VLOG(2) << "NetlinkManager::" << __func__ << " - adding handler";
  broadcast_handlers_.push_back(handler);
  return true;
}

bool NetlinkManager::RemoveBroadcastHandler(
    const NetlinkMessageHandler& handler) {
  std::list<NetlinkMessageHandler>::iterator i;
  for (i = broadcast_handlers_.begin(); i != broadcast_handlers_.end(); ++i) {
    if (*i == handler) {
      broadcast_handlers_.erase(i);
      // Should only be one copy in the list so we don't have to continue
      // looking for another one.
      return true;
    }
  }
  LOG(WARNING) << "NetlinkMessageHandler not found.";
  return false;
}

bool NetlinkManager::FindBroadcastHandler(
    const NetlinkMessageHandler& handler) const {
  for (const auto& broadcast_handler : broadcast_handlers_) {
    if (broadcast_handler == handler) {
      return true;
    }
  }
  return false;
}

void NetlinkManager::ClearBroadcastHandlers() {
  broadcast_handlers_.clear();
}

bool NetlinkManager::SendControlMessage(
    ControlNetlinkMessage* message,
    const ControlNetlinkMessageHandler& message_handler,
    const NetlinkAckHandler& ack_handler,
    const NetlinkAuxiliaryMessageHandler& error_handler) {
  return SendOrPostMessage(
      message,
      new ControlResponseHandler(ack_handler, error_handler, message_handler));
}

bool NetlinkManager::SendOrPostMessage(
    NetlinkMessage* message, NetlinkResponseHandlerRefPtr response_handler) {
  if (!message) {
    LOG(ERROR) << "Message is NULL.";
    return false;
  }

  const uint32_t sequence_number = this->GetSequenceNumber();
  const bool is_dump_msg = message->flags() & NLM_F_DUMP;
  NetlinkPendingMessage pending_message(sequence_number, is_dump_msg,
                                        message->Encode(sequence_number),
                                        std::move(response_handler));

  // TODO(samueltan): print this debug message above the actual call to
  // NetlinkSocket::SendMessage in NetlinkManager::SendMessageInternal.
  VLOG(5) << "NL Message " << pending_message.sequence_number << " to send ("
          << pending_message.message_string.size() << " bytes) ===>";
  message->Print(6, 7);
  NetlinkMessage::PrintBytes(8, pending_message.message_string.data(),
                             pending_message.message_string.size());

  if (is_dump_msg) {
    pending_messages_.push(pending_message);
    if (IsDumpPending()) {
      VLOG(5) << "Dump pending -- will send message after dump is complete";
      return true;
    }
  }
  return RegisterHandlersAndSendMessage(pending_message);
}

bool NetlinkManager::RegisterHandlersAndSendMessage(
    const NetlinkPendingMessage& pending_message) {
  // Clean out timed-out message handlers.  The list of outstanding messages
  // should be small so the time wasted by looking through all of them should
  // be small.
  const base::TimeTicks now = base::TimeTicks::Now();
  auto handler_it = message_handlers_.begin();
  while (handler_it != message_handlers_.end()) {
    if (now > handler_it->second->delete_after()) {
      // A timeout isn't always unexpected so this is not a warning.
      VLOG(2) << "Removing timed-out handler for sequence number "
              << handler_it->first;
      handler_it->second->HandleError(kTimeoutWaitingForResponse, nullptr);
      handler_it = message_handlers_.erase(handler_it);
    } else {
      ++handler_it;
    }
  }

  // Register handlers for replies to this message.
  if (!pending_message.handler) {
    VLOG(2) << "Handler for message was null.";
  } else if (base::Contains(message_handlers_,
                            pending_message.sequence_number)) {
    LOG(ERROR) << "A handler already existed for sequence: "
               << pending_message.sequence_number;
    return false;
  } else {
    pending_message.handler->set_delete_after(now + kResponseTimeout);
    message_handlers_[pending_message.sequence_number] =
        pending_message.handler;
  }
  return SendMessageInternal(pending_message);
}

bool NetlinkManager::SendMessageInternal(
    const NetlinkPendingMessage& pending_message) {
  VLOG(5) << "Sending NL message " << pending_message.sequence_number;

  if (!sock_->SendMessage(pending_message.message_string)) {
    LOG(ERROR) << "Failed to send Netlink message.";
    return false;
  }
  if (pending_message.is_dump_request) {
    VLOG(5) << "Waiting for replies to NL dump message "
            << pending_message.sequence_number;
    dump_pending_ = true;
    pending_dump_timeout_callback_.Reset(base::BindOnce(
        &NetlinkManager::OnPendingDumpTimeout, weak_ptr_factory_.GetWeakPtr()));
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, pending_dump_timeout_callback_.callback(),
        kPendingDumpTimeout);
  }
  return true;
}

bool NetlinkManager::IsBroadcastPacket(const NetlinkPacket& packet) const {
  const uint32_t sequence_number = packet.GetMessageSequence();
  return !base::Contains(message_handlers_, sequence_number) &&
         packet.GetMessageType() != ErrorAckMessage::kMessageType;
}

void NetlinkManager::OnPendingDumpTimeout() {
  VLOG(2) << "Timed out waiting for replies to NL dump message "
          << PendingDumpSequenceNumber();
  if (IsDumpPending() && pending_messages_.front().retries_left > 0) {
    VLOG(2) << "Resending NL dump message";
    ResendPendingDumpMessage();
    return;
  }
  CallErrorHandler(PendingDumpSequenceNumber(), kTimeoutWaitingForResponse,
                   nullptr);
  OnPendingDumpComplete();
}

void NetlinkManager::OnPendingDumpComplete() {
  VLOG(2) << __func__;
  dump_pending_ = false;
  pending_dump_timeout_callback_.Cancel();
  resend_dump_message_callback_.Cancel();
  pending_messages_.pop();
  if (!pending_messages_.empty()) {
    VLOG(2) << "Sending next pending message";
    NetlinkPendingMessage to_send = pending_messages_.front();
    RegisterHandlersAndSendMessage(to_send);
  }
}

bool NetlinkManager::IsDumpPending() {
  return dump_pending_ && !pending_messages_.empty();
}

uint32_t NetlinkManager::PendingDumpSequenceNumber() {
  if (!IsDumpPending()) {
    LOG(ERROR) << __func__ << ": no pending dump";
    return 0;
  }
  return pending_messages_.front().sequence_number;
}

bool NetlinkManager::RemoveMessageHandler(const NetlinkMessage& message) {
  if (!base::Contains(message_handlers_, message.sequence_number())) {
    return false;
  }
  message_handlers_.erase(message.sequence_number());
  return true;
}

uint32_t NetlinkManager::GetSequenceNumber() {
  return sock_ ? sock_->GetSequenceNumber()
               : NetlinkMessage::kBroadcastSequenceNumber;
}

bool NetlinkManager::SubscribeToEvents(const std::string& family_id,
                                       const std::string& group_name) {
  if (!base::Contains(message_types_, family_id)) {
    LOG(ERROR) << "Family '" << family_id << "' doesn't exist";
    return false;
  }

  if (!base::Contains(message_types_[family_id].groups, group_name)) {
    LOG(ERROR) << "Group '" << group_name << "' doesn't exist in family '"
               << family_id << "'";
    return false;
  }

  uint32_t group_id = message_types_[family_id].groups[group_name];
  if (!sock_) {
    LOG(FATAL) << "Need to call |Init| first.";
  }
  return sock_->SubscribeToEvents(group_id);
}

void NetlinkManager::OnRawNlMessageReceived(base::span<const uint8_t> data) {
  base::span<const uint8_t> remain_data = data;
  while (!remain_data.empty()) {
    NetlinkPacket packet(remain_data);
    if (!packet.IsValid()) {
      break;
    }
    remain_data = remain_data.subspan(packet.GetLength());
    OnNlMessageReceived(&packet);
  }
}

void NetlinkManager::OnNlMessageReceived(NetlinkPacket* packet) {
  if (!packet) {
    LOG(ERROR) << __func__ << "() called with null packet.";
    return;
  }
  const uint32_t sequence_number = packet->GetMessageSequence();

  std::unique_ptr<NetlinkMessage> message(
      message_factory_.CreateMessage(packet, IsBroadcastPacket(*packet)));
  if (message == nullptr) {
    VLOG(2) << "NL Message " << sequence_number << " <===";
    VLOG(2) << __func__ << "(msg:NULL)";
    return;  // Skip current message, continue parsing buffer.
  }
  VLOG(5) << "NL Message " << sequence_number << " Received ("
          << packet->GetLength() << " bytes) <===";
  message->Print(6, 7);
  NetlinkMessage::PrintPacket(8, *packet);

  bool is_error_ack_message = false;
  int32_t error_code = 0;
  if (message->message_type() == ErrorAckMessage::GetMessageType()) {
    is_error_ack_message = true;
    const ErrorAckMessage* error_ack_message =
        static_cast<const ErrorAckMessage*>(message.get());
    error_code = error_ack_message->error();
  }

  // Note: assumes we only receive one reply to a dump request: an error
  // message, an ACK, or a single multi-part reply. If we receive two replies,
  // then we will stop waiting for replies after the first reply is processed
  // here. This assumption should hold unless the NLM_F_ACK or NLM_F_ECHO
  // flags are explicitly added to the dump request.
  if (IsDumpPending() &&
      (message->sequence_number() == PendingDumpSequenceNumber()) &&
      !((message->flags() & NLM_F_MULTI) &&
        (message->message_type() != NLMSG_DONE))) {
    // Dump currently in progress, this message's sequence number matches that
    // of the pending dump request, and we are not in the middle of receiving a
    // multi-part reply.
    if (is_error_ack_message && (error_code == -EBUSY)) {
      VLOG(2) << "EBUSY reply received for NL dump message "
              << PendingDumpSequenceNumber();
      if (pending_messages_.front().retries_left) {
        pending_messages_.front().last_received_error = error_code;
        pending_dump_timeout_callback_.Cancel();
        ResendPendingDumpMessageAfterDelay();
        // Since we will resend the message, do not invoke error handler.
        return;
      } else {
        VLOG(2) << "No more resend attempts left for NL dump message "
                << PendingDumpSequenceNumber()
                << " -- stop waiting "
                   "for replies";
        OnPendingDumpComplete();
      }
    } else {
      VLOG(2) << "Reply received for NL dump message "
              << PendingDumpSequenceNumber() << " -- stop waiting for replies";
      OnPendingDumpComplete();
    }
  }

  if (is_error_ack_message) {
    VLOG(2) << "Error/ACK response to message " << sequence_number;
    if (error_code) {
      CallErrorHandler(sequence_number, kErrorFromKernel, message.get());
    } else {
      if (base::Contains(message_handlers_, sequence_number)) {
        VLOG(6) << "Found message-specific ACK handler";
        if (message_handlers_[sequence_number]->HandleAck()) {
          VLOG(6) << "ACK handler invoked -- removing callback";
          message_handlers_.erase(sequence_number);
        } else {
          VLOG(6) << "ACK handler invoked -- not removing callback";
        }
      }
    }
    return;
  }

  if (base::Contains(message_handlers_, sequence_number)) {
    VLOG(6) << "Found message-specific handler";
    if ((message->flags() & NLM_F_MULTI) &&
        (message->message_type() == NLMSG_DONE)) {
      message_handlers_[sequence_number]->HandleError(kDone, message.get());
    } else if (!message_handlers_[sequence_number]->HandleMessage(*message)) {
      LOG(ERROR) << "Couldn't call message handler for " << sequence_number;
      // Call the error handler but, since we don't have an |ErrorAckMessage|,
      // we'll have to pass a nullptr.
      message_handlers_[sequence_number]->HandleError(kUnexpectedResponseType,
                                                      nullptr);
    }
    if ((message->flags() & NLM_F_MULTI) &&
        (message->message_type() != NLMSG_DONE)) {
      VLOG(6) << "Multi-part message -- not removing callback";
    } else {
      VLOG(6) << "Removing callbacks";
      message_handlers_.erase(sequence_number);
    }
    return;
  }

  for (const auto& handler : broadcast_handlers_) {
    VLOG(6) << "Calling broadcast handler";
    if (!handler.is_null()) {
      handler.Run(*message);
    }
  }
}

void NetlinkManager::ResendPendingDumpMessage() {
  if (!IsDumpPending()) {
    VLOG(2) << "No pending dump, so do not resend dump message";
    return;
  }
  --pending_messages_.front().retries_left;
  if (SendMessageInternal(pending_messages_.front())) {
    VLOG(2) << "NL message " << PendingDumpSequenceNumber()
            << " sent again successfully";
    return;
  }
  VLOG(2) << "Failed to resend NL message " << PendingDumpSequenceNumber();
  if (pending_messages_.front().retries_left) {
    ResendPendingDumpMessageAfterDelay();
  } else {
    VLOG(2) << "No more resend attempts left for NL dump message "
            << PendingDumpSequenceNumber()
            << " -- stop waiting "
               "for replies";
    ErrorAckMessage err_message(pending_messages_.front().last_received_error);
    CallErrorHandler(PendingDumpSequenceNumber(), kErrorFromKernel,
                     &err_message);
    OnPendingDumpComplete();
  }
}

void NetlinkManager::CallErrorHandler(uint32_t sequence_number,
                                      AuxiliaryMessageType type,
                                      const NetlinkMessage* netlink_message) {
  if (base::Contains(message_handlers_, sequence_number)) {
    VLOG(6) << "Found message-specific error handler";
    message_handlers_[sequence_number]->HandleError(type, netlink_message);
    message_handlers_.erase(sequence_number);
  }
}

void NetlinkManager::ResendPendingDumpMessageAfterDelay() {
  VLOG(2) << "Resending NL dump message " << PendingDumpSequenceNumber()
          << " after " << kNlMessageRetryDelay.InMilliseconds() << " ms";
  resend_dump_message_callback_.Reset(
      base::BindOnce(&NetlinkManager::ResendPendingDumpMessage,
                     weak_ptr_factory_.GetWeakPtr()));
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, resend_dump_message_callback_.callback(),
      kNlMessageRetryDelay);
}

}  // namespace net_base
