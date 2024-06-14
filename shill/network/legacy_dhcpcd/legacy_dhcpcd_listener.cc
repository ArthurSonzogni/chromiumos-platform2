// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_listener.h"

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/containers/fixed_flat_map.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/stringprintf.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <dbus/error.h>
#include <dbus/util.h>

#include "shill/event_dispatcher.h"
#include "shill/network/dhcp_client_proxy.h"
#include "shill/network/dhcp_provider.h"
#include "shill/network/legacy_dhcp_controller.h"

namespace shill {
namespace {

// dbus constants.
constexpr char kDBusInterfaceName[] = "org.chromium.dhcpcd";
constexpr char kSignalEvent[] = "Event";
constexpr char kSignalStatusChanged[] = "StatusChanged";

std::optional<DHCPClientProxy::EventReason> ConvertToEventReason(
    std::string_view reason) {
  // Constants used as event type got from dhcpcd.
  static constexpr auto kEventReasonTable =
      base::MakeFixedFlatMap<std::string_view, DHCPClientProxy::EventReason>(
          {{"BOUND", DHCPClientProxy::EventReason::kBound},
           {"FAIL", DHCPClientProxy::EventReason::kFail},
           {"GATEWAY-ARP", DHCPClientProxy::EventReason::kGatewayArp},
           {"NAK", DHCPClientProxy::EventReason::kNak},
           {"REBIND", DHCPClientProxy::EventReason::kRebind},
           {"REBOOT", DHCPClientProxy::EventReason::kReboot},
           {"RENEW", DHCPClientProxy::EventReason::kRenew}});

  const auto iter = kEventReasonTable.find(reason);
  if (iter == kEventReasonTable.end()) {
    return std::nullopt;
  }
  return iter->second;
}

std::optional<LegacyDHCPCDListener::Status> ConvertToStatus(
    std::string_view status) {
  static constexpr auto kStatusTable =
      base::MakeFixedFlatMap<std::string_view, LegacyDHCPCDListener::Status>(
          {{"Init", LegacyDHCPCDListener::Status::kInit},
           {"Bound", LegacyDHCPCDListener::Status::kBound},
           {"Release", LegacyDHCPCDListener::Status::kRelease},
           {"Discover", LegacyDHCPCDListener::Status::kDiscover},
           {"Request", LegacyDHCPCDListener::Status::kRequest},
           {"Renew", LegacyDHCPCDListener::Status::kRenew},
           {"Rebind", LegacyDHCPCDListener::Status::kRebind},
           {"ArpSelf", LegacyDHCPCDListener::Status::kArpSelf},
           {"Inform", LegacyDHCPCDListener::Status::kInform},
           {"Reboot", LegacyDHCPCDListener::Status::kReboot},
           {"NakDefer", LegacyDHCPCDListener::Status::kNakDefer},
           {"IPv6OnlyPreferred",
            LegacyDHCPCDListener::Status::kIPv6OnlyPreferred},
           {"IgnoreInvalidOffer",
            LegacyDHCPCDListener::Status::kIgnoreInvalidOffer},
           {"IgnoreFailedOffer",
            LegacyDHCPCDListener::Status::kIgnoreFailedOffer},
           {"IgnoreAdditionalOffer",
            LegacyDHCPCDListener::Status::kIgnoreAdditionalOffer},
           {"IgnoreNonOffer", LegacyDHCPCDListener::Status::kIgnoreNonOffer},
           {"ArpGateway", LegacyDHCPCDListener::Status::kArpGateway}});

  const auto iter = kStatusTable.find(status);
  if (iter == kStatusTable.end()) {
    return std::nullopt;
  }
  return iter->second;
}

class LegacyDHCPCDListenerImpl : public LegacyDHCPCDListener {
 public:
  LegacyDHCPCDListenerImpl(scoped_refptr<dbus::Bus> bus,
                           EventDispatcher* dispatcher,
                           EventSignalCB event_signal_cb,
                           StatusChangedCB status_changed_cb);
  LegacyDHCPCDListenerImpl(const LegacyDHCPCDListenerImpl&) = delete;
  LegacyDHCPCDListenerImpl& operator=(const LegacyDHCPCDListenerImpl&) = delete;

  ~LegacyDHCPCDListenerImpl() override;

 protected:
  // Redirects the function call to HandleMessage
  static DBusHandlerResult HandleMessageThunk(DBusConnection* connection,
                                              DBusMessage* raw_message,
                                              void* user_data);

  // Handles incoming messages.
  DBusHandlerResult HandleMessage(DBusMessage* raw_message);

  // Signal handlers.
  void EventSignal(const std::string& sender,
                   uint32_t pid,
                   const std::string& reason,
                   const brillo::VariantDictionary& configurations);
  void StatusChangedSignal(const std::string& sender,
                           uint32_t pid,
                           const std::string& status);

  scoped_refptr<dbus::Bus> bus_;
  EventDispatcher* dispatcher_;
  EventSignalCB event_signal_cb_;
  StatusChangedCB status_changed_cb_;

  const std::string match_rule_;

  base::WeakPtrFactory<LegacyDHCPCDListenerImpl> weak_factory_{this};
};

LegacyDHCPCDListenerImpl::LegacyDHCPCDListenerImpl(
    scoped_refptr<dbus::Bus> bus,
    EventDispatcher* dispatcher,
    EventSignalCB event_signal_cb,
    StatusChangedCB status_changed_cb)
    : bus_(std::move(bus)),
      dispatcher_(dispatcher),
      event_signal_cb_(std::move(event_signal_cb)),
      status_changed_cb_(std::move(status_changed_cb)),
      match_rule_(base::StringPrintf("type='signal', interface='%s'",
                                     kDBusInterfaceName)) {
  bus_->AssertOnDBusThread();
  CHECK(bus_->SetUpAsyncOperations());
  if (!bus_->IsConnected()) {
    LOG(FATAL) << "DBus isn't connected.";
  }

  // Register filter function to the bus.  It will be called when incoming
  // messages are received.
  bus_->AddFilterFunction(&LegacyDHCPCDListenerImpl::HandleMessageThunk, this);

  // Add match rule to the bus.
  dbus::Error error;
  bus_->AddMatch(match_rule_, &error);
  if (error.IsValid()) {
    LOG(FATAL) << "Failed to add match rule: " << error.name() << " "
               << error.message();
  }
}

LegacyDHCPCDListenerImpl::~LegacyDHCPCDListenerImpl() {
  bus_->RemoveFilterFunction(&LegacyDHCPCDListenerImpl::HandleMessageThunk,
                             this);
  dbus::Error error;
  bus_->RemoveMatch(match_rule_, &error);
  if (error.IsValid()) {
    LOG(FATAL) << "Failed to remove match rule: " << error.name() << " "
               << error.message();
  }
}

// static.
DBusHandlerResult LegacyDHCPCDListenerImpl::HandleMessageThunk(
    DBusConnection* connection, DBusMessage* raw_message, void* user_data) {
  LegacyDHCPCDListenerImpl* self =
      static_cast<LegacyDHCPCDListenerImpl*>(user_data);
  return self->HandleMessage(raw_message);
}

DBusHandlerResult LegacyDHCPCDListenerImpl::HandleMessage(
    DBusMessage* raw_message) {
  bus_->AssertOnDBusThread();

  // Only interested in signal message.
  if (dbus_message_get_type(raw_message) != DBUS_MESSAGE_TYPE_SIGNAL) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  // raw_message will be unrefed in Signal's parent class's (dbus::Message)
  // destructor. Increment the reference so we can use it in Signal.
  dbus_message_ref(raw_message);
  std::unique_ptr<dbus::Signal> signal(
      dbus::Signal::FromRawMessage(raw_message));

  // Verify the signal comes from the interface that we interested in.
  if (signal->GetInterface() != kDBusInterfaceName) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  const auto sender = signal->GetSender();
  const auto member_name = signal->GetMember();
  dbus::MessageReader reader(signal.get());
  if (member_name == kSignalEvent) {
    uint32_t pid;
    std::string reason;
    brillo::VariantDictionary configurations;
    // ExtractMessageParameters will log the error if it failed.
    if (brillo::dbus_utils::ExtractMessageParameters(
            &reader, nullptr, &pid, &reason, &configurations)) {
      dispatcher_->PostTask(
          FROM_HERE, base::BindOnce(&LegacyDHCPCDListenerImpl::EventSignal,
                                    weak_factory_.GetWeakPtr(), sender, pid,
                                    reason, configurations));
    }
  } else if (member_name == kSignalStatusChanged) {
    uint32_t pid;
    std::string status;
    // ExtractMessageParameters will log the error if it failed.
    if (brillo::dbus_utils::ExtractMessageParameters(&reader, nullptr, &pid,
                                                     &status)) {
      dispatcher_->PostTask(
          FROM_HERE,
          base::BindOnce(&LegacyDHCPCDListenerImpl::StatusChangedSignal,
                         weak_factory_.GetWeakPtr(), sender, pid, status));
    }
  } else {
    LOG(INFO) << "Ignore signal: " << member_name;
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

void LegacyDHCPCDListenerImpl::EventSignal(
    const std::string& sender,
    uint32_t pid,
    const std::string& reason_str,
    const brillo::VariantDictionary& configuration) {
  const std::optional<DHCPClientProxy::EventReason> reason =
      ConvertToEventReason(reason_str);
  if (!reason.has_value()) {
    LOG(WARNING) << "Unknown reason: " << reason_str;
    return;
  }

  KeyValueStore configuration_store =
      KeyValueStore::ConvertFromVariantDictionary(configuration);
  event_signal_cb_.Run(sender, pid, *reason, configuration_store);
}

void LegacyDHCPCDListenerImpl::StatusChangedSignal(
    const std::string& sender, uint32_t pid, const std::string& status_str) {
  const std::optional<Status> status = ConvertToStatus(status_str);
  if (!status.has_value()) {
    LOG(WARNING) << "Unknown status: " << status_str;
    return;
  }

  status_changed_cb_.Run(sender, pid, *status);
}

}  // namespace

std::unique_ptr<LegacyDHCPCDListener> LegacyDHCPCDListenerFactory::Create(
    scoped_refptr<dbus::Bus> bus,
    EventDispatcher* dispatcher,
    EventSignalCB event_signal_cb,
    StatusChangedCB status_changed_cb) {
  return std::make_unique<LegacyDHCPCDListenerImpl>(
      std::move(bus), dispatcher, std::move(event_signal_cb),
      std::move(status_changed_cb));
}

}  // namespace shill
