// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <utility>
#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <gio/gunixsocketaddress.h>
#include <libmbim-glib/libmbim-glib.h>
#include "hermes/apdu.h"
#include "hermes/euicc_manager_interface.h"
#include "hermes/hermes_common.h"
#include "hermes/modem_mbim.h"
#include "hermes/sgp_22.h"
#include "hermes/type_traits.h"

namespace {
const int kExecutorIndex = 0;
const guint kMbimTimeoutSeconds = 30;
constexpr int kMbimMessageSuccess = 144;
constexpr auto kSlotInfoDelay = base::Seconds(5);
// Application identifier for the eUICC's SIM EID
const std::array<uint8_t, 12> kMbimEidReqApdu = {
    0x81, 0xE2, 0x91, 0x00, 0x06, 0xBF, 0x3E, 0x03, 0x5C, 0x01, 0x5A, 0x00,
};

// ModemManager uses channel_group=1. Make Hermes use 2 just to be cautious.
constexpr int kChannelGroupId = 2;

bool GetReadyState(MbimDevice* device,
                   bool is_notification,
                   MbimMessage* notification,
                   MbimSubscriberReadyState* ready_state) {
  g_autoptr(GError) error = NULL;

  if (mbim_device_check_ms_mbimex_version(device, 3, 0)) {
    MbimSubscriberReadyStatusFlag flags =
        MBIM_SUBSCRIBER_READY_STATUS_FLAG_NONE;
    auto parser_v3 =
        is_notification
            ? &mbim_message_ms_basic_connect_v3_subscriber_ready_status_notification_parse
            : &mbim_message_ms_basic_connect_v3_subscriber_ready_status_response_parse;
    if (!parser_v3(notification, ready_state, &flags, NULL, NULL,
                   NULL, /* ready_info */
                   NULL, /* telephone_numbers_count */
                   NULL, /* telephone_numbers */
                   &error)) {
      LOG(ERROR) << __func__ << ": Failed due to error: " << error->message;
      return false;
    }
  } else {
    auto parser = is_notification
                      ? &mbim_message_subscriber_ready_status_notification_parse
                      : &mbim_message_subscriber_ready_status_response_parse;
    if (!parser(notification, ready_state,
                /* subscriber_id */ NULL,
                /* sim_iccid */ NULL,
                /* ready_info */ NULL,
                /* telephone_numbers_count */ NULL,
                /* telephone_numbers */ NULL, &error)) {
      LOG(ERROR) << __func__ << ": Failed due to error: " << error->message;
      return false;
    }
  }
  return true;
}

}  // namespace

namespace hermes {

/* static */
std::unique_ptr<ModemMbim> ModemMbim::Create(
    Logger* logger,
    Executor* executor,
    std::unique_ptr<ModemManagerProxy> modem_manager_proxy) {
  VLOG(2) << __func__;
  return std::unique_ptr<ModemMbim>(
      new ModemMbim(logger, executor, std::move(modem_manager_proxy)));
}

ModemMbim::ModemMbim(Logger* logger,
                     Executor* executor,
                     std::unique_ptr<ModemManagerProxy> modem_manager_proxy)
    : Modem<MbimCmd>(logger, executor, std::move(modem_manager_proxy)),
      channel_(kInvalidChannel),
      is_ready_state_valid(false),
      ready_state_(MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED),
      slot_info_(),
      weak_factory_(this) {}

ModemMbim::~ModemMbim() {
  VLOG(2) << "~ModemMbim Destructor++";
  Shutdown();
}

void ModemMbim::Initialize(EuiccManagerInterface* euicc_manager,
                           ResultCallback cb) {
  LOG(INFO) << __func__;
  CHECK(current_state_ == State::kMbimUninitialized);
  retry_initialization_callback_.Reset();
  euicc_manager_ = euicc_manager;
  InitializationStep(State::kMbimUninitialized, std::move(cb));
}

void ModemMbim::Shutdown() {
  LOG(INFO) << __func__;
  CloseDevice();
  channel_ = kInvalidChannel;
  ready_state_ = MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED;
  current_state_.Transition(State::kMbimUninitialized);
  slot_info_.Clear();
  modem_manager_proxy_->ScheduleUninhibit(base::Seconds(0));
}

void ModemMbim::TransmitFromQueue() {
  VLOG(2) << __func__;
  if (tx_queue_.empty() || retry_initialization_callback_) {
    return;
  }
  if (!device_) {
    LOG(ERROR) << "No MBIM device. Cannot transmit MBIM message";
    ProcessMbimResult(kModemMessageProcessingError);
    return;
  }

  auto mbim_cmd = tx_queue_[0].msg_.get();
  switch (mbim_cmd->mbim_type()) {
    case MbimCmd::MbimType::kMbimOpenChannel:
      TransmitOpenChannel();
      break;
    case MbimCmd::MbimType::kMbimCloseChannel:
      TransmitCloseChannel();
      break;
    case MbimCmd::MbimType::kMbimSendApdu:
      TransmitMbimSendApdu(&tx_queue_[0]);
      break;
    case MbimCmd::MbimType::kMbimSubscriberStatusReady:
      TransmitSubscriberStatusReady();
      break;
    case MbimCmd::MbimType::kMbimDeviceCaps:
      TransmitDeviceCaps();
      break;
    case MbimCmd::MbimType::kMbimSendEidApdu:
      TransmitSendEidApdu();
      break;
    case MbimCmd::MbimType::kMbimSlotInfoStatus:
      TransmitSlotInfoStatus();
      break;
    case MbimCmd::MbimType::kMbimDeviceSlotMapping:
      TransmitDeviceSlotMapping();
      break;
    case MbimCmd::MbimType::kMbimSetDeviceSlotMapping:
      TransmitSetDeviceSlotMapping();
      break;
    case MbimCmd::MbimType::kMbimSysCaps:
      TransmitSysCapsQuery();
      break;
    default:
      VLOG(2) << "no instruction";
      break;
  }
}

std::unique_ptr<MbimCmd> ModemMbim::GetTagForSendApdu() {
  return std::make_unique<MbimCmd>(MbimCmd::MbimType::kMbimSendApdu);
}

void ModemMbim::ProcessMbimResult(int err) {
  if (tx_queue_.empty()) {
    VLOG(2) << __func__ << ": Queue is empty";
    return;
  }
  // pop before running the callback since the callback might change the state
  // of the queue.
  auto cb_ = std::move(tx_queue_[0].cb_);
  tx_queue_.pop_front();
  if (!cb_.is_null()) {
    std::move(cb_).Run(err);
  }
}

/* static */
void ModemMbim::MbimCreateNewDeviceCb(GObject* source,
                                      GAsyncResult* res,
                                      ModemMbim* modem_mbim) {
  /* Open the device */
  VLOG(2) << __func__;
  CHECK(modem_mbim) << "modem_mbim does not exist";
  g_autoptr(GError) error = NULL;
  glib_bridge::ScopedGObject<MbimDevice> mbimdevice(
      mbim_device_new_finish(res, &error));
  modem_mbim->device_ = std::move(mbimdevice);
  if (!modem_mbim->device_.get() || error != NULL) {
    LOG(INFO) << "The modem may be booting ...";
    modem_mbim->RetryInitialization(std::move(modem_mbim->init_done_cb_));
    return;
  }
  mbim_device_open_full(modem_mbim->device_.get(), MBIM_DEVICE_OPEN_FLAGS_PROXY,
                        kMbimTimeoutSeconds, /* cancellable */ NULL,
                        (GAsyncReadyCallback)MbimDeviceOpenReadyCb, modem_mbim);
}

/* static */
void ModemMbim::MbimDeviceOpenReadyCb(MbimDevice* device,
                                      GAsyncResult* res,
                                      ModemMbim* modem_mbim) {
  VLOG(2) << __func__;
  g_autoptr(GError) error = NULL;
  base::OnceCallback<void(base::OnceCallback<void(int)>)> get_caps;
  if (!mbim_device_open_finish(device, res, &error)) {
    LOG(ERROR) << "Failed  due to error: " << error->message;
    modem_mbim->RetryInitialization(std::move(modem_mbim->init_done_cb_));
    return;
  }
  modem_mbim->indication_id_ = g_signal_connect(
      modem_mbim->device_.get(), MBIM_DEVICE_SIGNAL_INDICATE_STATUS,
      G_CALLBACK(ClientIndicationCb), modem_mbim);
  if (modem_mbim->current_state_ == State::kMbimStarted) {
    VLOG(2) << "Opened device. Reusing previous IMEI";
    std::move(modem_mbim->init_done_cb_).Run(kModemSuccess);
    return;
  }
  LOG(INFO) << "Mbim device is ready, acquire imei and eid";
  modem_mbim->InitializationStep(State::kReadImei,
                                 std::move(modem_mbim->init_done_cb_));
}

void ModemMbim::InitializationStep(ModemMbim::State::Value next_state,
                                   ResultCallback cb) {
  // cb is executed only after all initialization steps complete or if there is
  // an error. cb is passed in from Initialize and passed around until the last
  // step. At the last step, the cb is executed.
  current_state_.Transition(next_state);
  switch (next_state) {
    case State::Value::kMbimUninitialized:
      // Store cb in init_done_cb_ so that MbimCreateNewDeviceCb can use it
      // while calling InitializationStep(kGetSubscriberReadyState)
      init_done_cb_ = std::move(cb);
      modem_manager_proxy_->WaitForModem(base::BindOnce(
          &ModemMbim::InitializationStep, weak_factory_.GetWeakPtr(),
          State::kMbimInitializeStarted, std::move(cb)));
      break;
    case State::kMbimInitializeStarted: {
      if (modem_manager_proxy_->GetPrimaryPort().empty()) {
        LOG(ERROR) << __func__ << ": Could not get primary port from MM";
        std::move(init_done_cb_).Run(kModemManagerError);
        break;
      }
      std::string dev_path = "/dev/" + modem_manager_proxy_->GetPrimaryPort();
      const gchar* const path = dev_path.c_str();
      LOG(INFO) << __func__ << ": Opening path:" << path;
      file_ = g_file_new_for_path(path);
      mbim_device_new(file_, /* cancellable */ NULL,
                      (GAsyncReadyCallback)MbimCreateNewDeviceCb,
                      this);  // MbimCreateNewDeviceCb will call
                              // InitializationStep(kReadImei)
      break;
    }
    case State::kReadImei:
      SendMessage(MbimCmd::MbimType::kMbimDeviceCaps,
                  std::make_unique<TxInfo>(), std::move(cb),
                  &ModemMbim::InitializationStep,
                  State::kGetSubscriberReadyState);
      break;
    case State::kGetSubscriberReadyState:
      SendMessage(MbimCmd::MbimType::kMbimSubscriberStatusReady,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::InitializationStep, State::kCheckSingleSim);
      break;
    case State::kCheckSingleSim: {
      if (!mbim_device_check_ms_mbimex_version(device_.get(), 2, 0)) {
        // we have a single sim device. Skip to reading EID if a SIM was seen.
        if (ready_state_ == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED) {
          VLOG(2) << "Sim not inserted";
          // skip retries, close device, listen for a new modem, and run cb
          retry_count_ = kMaxRetries + 1;
          RetryInitialization(std::move(cb));
          break;
        }
        slot_info_.InitSlotInfo(1 /* slot_count */, 1 /* map_count */);
        slot_info_.cached_active_slot_ = 0;
        slot_info_.slot_state_[0] =
            MBIM_UICC_SLOT_STATE_ACTIVE_ESIM;  // Assume we have an eSIM until
                                               // we can confirm the EID. This
                                               // is an mbimv1 limitation.
        InitializationStep(State::kCloseChannel, std::move(cb));
        break;
      }
      InitializationStep(State::kSysQuery, std::move(cb));
      break;
    }
    case State::kSysQuery:
      SendMessage(MbimCmd::MbimType::kMbimSysCaps, std::unique_ptr<TxInfo>(),
                  std::move(cb), &ModemMbim::InitializationStep,
                  State::kDeviceSlotMapping);
      break;
    case State::kDeviceSlotMapping:
      // To figure out active slot
      SendMessage(MbimCmd::MbimType::kMbimDeviceSlotMapping,
                  std::make_unique<SwitchSlotTxInfo>(0), std::move(cb),
                  &ModemMbim::InitializationStep, State::kSlotInfo);
      break;
    case State::kSlotInfo:
      ReadSlotsInfo(
          0 /* slot_num */, 0 /* retry_count */,
          std::move(
              cb));  // ReadSlotsInfo calls InitializationStep(CloseChannel)
                     // after slot info for all slots has been read. This is an
                     // exception to the rule that all state transitions must be
                     // captured in InitializationStep.
      break;
    case State::kCloseChannel:
      if (!slot_info_.IsEsimActive()) {
        InitializationStep(State::kMbimStarted, std::move(cb));
        break;
      }
      SendMessage(MbimCmd::MbimType::kMbimCloseChannel,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::InitializationStep, State::kOpenChannel);
      break;
    case State::kOpenChannel:
      SendMessage(MbimCmd::MbimType::kMbimOpenChannel,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::InitializationStep, State::kReadEid);
      break;
    case State::kReadEid:
      SendMessage(MbimCmd::MbimType::kMbimSendEidApdu,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::InitializationStep, State::kEidReadComplete);
      break;
    case State::kEidReadComplete:
      SendMessage(MbimCmd::MbimType::kMbimCloseChannel,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::InitializationStep, State::kMbimStarted);
      break;
    case State::kMbimStarted:
      VLOG(2) << "eSIM initialized for MBIM modem";
      CloseDevice();
      std::move(cb).Run(kModemSuccess);
      OnEuiccUpdated();
      break;
  }
}

void ModemMbim::OnEuiccUpdated() {
  VLOG(2) << __func__ << ": " << slot_info_;
  for (int i = 0; i < slot_info_.slot_count_; i++) {
    if (slot_info_.IsEuicc(i)) {
      auto euicc_slot_info =
          (slot_info_.cached_active_slot_ == i)
              ? EuiccSlotInfo(kExecutorIndex, slot_info_.eid_[i])
              : EuiccSlotInfo(slot_info_.eid_[i]);
      euicc_manager_->OnEuiccUpdated(i + 1, EuiccSlotInfo(slot_info_.eid_[i]));
    }
  }
  euicc_manager_->OnLogicalSlotUpdated(slot_info_.cached_active_slot_ + 1,
                                       slot_info_.cached_active_slot_ + 1);
}

void ModemMbim::ReadSlotsInfo(uint32_t slot_num,
                              uint32_t retry_count,
                              ResultCallback cb) {
  VLOG(2) << __func__ << ": slot_num:" << slot_num;
  if (slot_num == slot_info_.slot_count_) {
    if (slot_info_.IsEuiccPresentOnAnySlot() || retry_count >= kMaxRetries) {
      InitializationStep(State::kCloseChannel, std::move(cb));
      return;
    }
    // post a  delayed task after 5secs
    executor_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ModemMbim::ReadSlotsInfo, weak_factory_.GetWeakPtr(), 0,
                       ++retry_count, std::move(cb)),
        kSlotInfoDelay);
  }
  SendMessage(MbimCmd::MbimType::kMbimSlotInfoStatus,
              std::make_unique<SwitchSlotTxInfo>(slot_num), std::move(cb),
              &ModemMbim::ReadSlotsInfo, slot_num + 1, 0 /* retry_count */);
}

void ModemMbim::TransmitSysCapsQuery() {
  g_autoptr(MbimMessage) message = NULL;
  VLOG(2) << __func__;
  message = mbim_message_ms_basic_connect_extensions_sys_caps_query_new(NULL);
  if (!message) {
    LOG(ERROR) << "Mbim message creation failed";
    ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /* cancellable */ NULL,
                      (GAsyncReadyCallback)QuerySysCapsReady, this);
}

void ModemMbim::TransmitSubscriberStatusReady() {
  g_autoptr(MbimMessage) message = NULL;
  VLOG(2) << __func__;
  message = mbim_message_subscriber_ready_status_query_new(NULL);
  if (!message) {
    LOG(ERROR) << "Mbim message creation failed";
    ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /* cancellable */ NULL,
                      (GAsyncReadyCallback)SubscriberReadyStatusRspCb, this);
}

void ModemMbim::TransmitDeviceCaps() {
  g_autoptr(MbimMessage) message = NULL;
  VLOG(2) << __func__;
  message = mbim_message_device_caps_query_new(/* error */ NULL);
  if (!message) {
    LOG(ERROR) << __func__ << " :Mbim message creation failed";
    ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /*cancellable*/ NULL,
                      (GAsyncReadyCallback)DeviceCapsQueryReady, this);
}

void ModemMbim::TransmitCloseChannel() {
  g_autoptr(MbimMessage) message = NULL;
  g_autoptr(GError) error = NULL;
  VLOG(2) << __func__;
  message = mbim_message_ms_uicc_low_level_access_close_channel_set_new(
      /* channel */ 0, kChannelGroupId, &error);
  if (!message) {
    LOG(ERROR) << "Mbim message creation failed:" << error->message;
    ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /* cancellable */ NULL,
                      (GAsyncReadyCallback)UiccLowLevelAccessCloseChannelSetCb,
                      this);
}

void ModemMbim::TransmitOpenChannel() {
  VLOG(2) << __func__;
  guint8 appId[16];
  guint32 appIdSize = kAidIsdr.size();
  g_autoptr(GError) error = NULL;
  g_autoptr(MbimMessage) message = NULL;
  std::copy(kAidIsdr.begin(), kAidIsdr.end(), appId);
  message = mbim_message_ms_uicc_low_level_access_open_channel_set_new(
      appIdSize, appId, /* selectP2arg */ 4, kChannelGroupId, &error);
  if (!message) {
    LOG(ERROR) << __func__ << ": Mbim Message Creation Failed";
    ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /*cancellable*/ NULL,
                      (GAsyncReadyCallback)UiccLowLevelAccessOpenChannelSetCb,
                      this);
}

void ModemMbim::TransmitSendEidApdu() {
  VLOG(2) << __func__;
  uint8_t eid_apduCmd[kMaxApduLen];
  guint32 kMbimEidReqApduSize = kMbimEidReqApdu.size();
  g_autoptr(MbimMessage) message = NULL;
  MbimUiccSecureMessaging secure_messaging = MBIM_UICC_SECURE_MESSAGING_NONE;
  MbimUiccClassByteType class_byte_type = MBIM_UICC_CLASS_BYTE_TYPE_EXTENDED;
  std::copy(kMbimEidReqApdu.begin(), kMbimEidReqApdu.end(), eid_apduCmd);
  message = (mbim_message_ms_uicc_low_level_access_apdu_set_new(
      channel_, secure_messaging, class_byte_type, kMbimEidReqApduSize,
      eid_apduCmd, NULL));
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /* cancellable */ NULL,
                      (GAsyncReadyCallback)UiccLowLevelAccessApduEidParse,
                      this);
}

void ModemMbim::TransmitSlotInfoStatus() {
  g_autoptr(MbimMessage) message = NULL;
  g_autoptr(GError) error = NULL;
  auto switch_slot_tx_info =
      dynamic_cast<SwitchSlotTxInfo*>(tx_queue_[0].info_.get());
  VLOG(2) << __func__ << ": slot:" << switch_slot_tx_info->physical_slot_ + 1;
  message =
      (mbim_message_ms_basic_connect_extensions_slot_info_status_query_new(
          switch_slot_tx_info->physical_slot_, &error));
  if (!message) {
    LOG(ERROR) << "Mbim message creation failed";
    return;
  }
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /* cancellable */ NULL,
                      (GAsyncReadyCallback)DeviceSlotStatusInfoRspCb, this);
}

void ModemMbim::TransmitDeviceSlotMapping() {
  g_autoptr(MbimMessage) message = NULL;
  VLOG(2) << __func__;
  message =
      mbim_message_ms_basic_connect_extensions_device_slot_mappings_query_new(
          NULL);
  if (!message) {
    LOG(ERROR) << "Mbim message creation failed";
    return;
  }
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /* cancellable */ NULL,
                      (GAsyncReadyCallback)DeviceSlotStatusMappingRspCb, this);
}

void ModemMbim::TransmitSetDeviceSlotMapping() {
  g_autoptr(MbimMessage) message = NULL;
  g_autoptr(MbimSlotArray) slot_mappings =
      NULL;  // Array of MbimSlotArray. Every executor gets a MbimSlotArray.
             // MbimSlotArray always holds a single element.
  slot_mappings = g_new0(MbimSlot*, slot_info_.map_count_ + 1);
  slot_mappings[kExecutorIndex] = g_new0(MbimSlot, 1);
  VLOG(2) << __func__;
  auto switch_slot_tx_info =
      dynamic_cast<SwitchSlotTxInfo*>(tx_queue_[0].info_.get());
  VLOG(2) << __func__ << ": Hermes trying to operate on slot:"
          << switch_slot_tx_info->physical_slot_;
  slot_mappings[kExecutorIndex]->slot = switch_slot_tx_info->physical_slot_;
  message =
      mbim_message_ms_basic_connect_extensions_device_slot_mappings_set_new(
          slot_info_.map_count_, (const MbimSlot**)slot_mappings, NULL);
  if (!message) {
    LOG(ERROR) << "Mbim message creation failed";
    return;
  }
  LOG(INFO) << __func__ << ": before mbim_device_command";
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /* cancellable */ NULL,
                      (GAsyncReadyCallback)SetDeviceSlotMappingsRspCb, this);
}

void ModemMbim::TransmitMbimSendApdu(TxElement* tx_element) {
  g_autoptr(MbimMessage) message = NULL;
  MbimUiccSecureMessaging secure_messaging = MBIM_UICC_SECURE_MESSAGING_NONE;
  MbimUiccClassByteType class_byte_type = MBIM_UICC_CLASS_BYTE_TYPE_EXTENDED;
  uint8_t* fragment;
  size_t apdu_len = 0;
  uint8_t apduCmd[kMaxApduLen] = {0};
  VLOG(2) << __func__;
  ApduTxInfo* apdu = static_cast<ApduTxInfo*>(tx_element->info_.get());
  size_t fragment_size = apdu->apdu_.GetNextFragment(&fragment);
  VLOG(2) << "Fragment size:" << fragment_size;
  apdu_len = fragment_size;
  std::copy(fragment, fragment + fragment_size, apduCmd);
  apduCmd[apdu_len++] =
      0x00;  // append extra byte for 4G Mbim Modem required for apdu handling
  LOG(INFO) << "Sending APDU fragment (" << apdu_len << " bytes): over channel "
            << channel_;
  VLOG(2) << "APDU:" << base::HexEncode(apduCmd, apdu_len);
  message = (mbim_message_ms_uicc_low_level_access_apdu_set_new(
      channel_, secure_messaging, class_byte_type, apdu_len, apduCmd, NULL));
  mbim_device_command(device_.get(), message, kMbimTimeoutSeconds,
                      /* cancellable */ NULL,
                      (GAsyncReadyCallback)UiccLowLevelAccessApduResponseParse,
                      this);
  return;
}

void ModemMbim::ReacquireChannel(EuiccEventStep step, ResultCallback cb) {
  LOG(INFO) << __func__ << ":" << to_underlying(step);
  switch (step) {
    case EuiccEventStep::CLOSE_CHANNEL:
      SendMessage(MbimCmd::MbimType::kMbimCloseChannel,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::ReacquireChannel, EuiccEventStep::OPEN_CHANNEL);
      break;
    case EuiccEventStep::OPEN_CHANNEL:
      SendMessage(MbimCmd::MbimType::kMbimOpenChannel,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::ReacquireChannel,
                  EuiccEventStep::EUICC_EVENT_STEP_LAST);
      break;
    case EuiccEventStep::EUICC_EVENT_STEP_LAST:
      std::move(cb).Run(kModemSuccess);
      break;
    default:
      VLOG(2) << "No Suitable operation";
  }
}

/* static */
void ModemMbim::SubscriberReadyStatusRspCb(MbimDevice* device,
                                           GAsyncResult* res,
                                           ModemMbim* modem_mbim) {
  g_autoptr(MbimMessage) response = NULL;
  VLOG(2) << __func__;
  g_autoptr(GError) error = NULL;
  response = mbim_device_command_finish(device, res, &error);
  if (!GetReadyState(device, false /* is_notification */, response,
                     &modem_mbim->ready_state_)) {
    LOG(ERROR) << "Could not parse ready state";
    modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  LOG(INFO) << "Current Sim status:" << modem_mbim->ready_state_;
  modem_mbim->ProcessMbimResult(kModemSuccess);
}

/* static */
void ModemMbim::DeviceCapsQueryReady(MbimDevice* device,
                                     GAsyncResult* res,
                                     ModemMbim* modem_mbim) {
  g_autoptr(MbimMessage) response = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree gchar* caps_device_id = NULL;
  VLOG(2) << __func__;
  response = mbim_device_command_finish(device, res, &error);
  if (!response ||
      !mbim_message_response_get_result(
          response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
      !mbim_message_device_caps_response_parse(response, NULL, /* device_type */
                                               NULL, /* cellular class  */
                                               NULL, /* voice_class  */
                                               NULL, /* sim_class  */
                                               NULL, /* data_class */
                                               NULL, /* sms_caps */
                                               NULL, /* ctrl_caps */
                                               NULL, /* max_sessions */
                                               NULL, /* custom_data_class */
                                               &caps_device_id,
                                               NULL, /* firmware_info */
                                               NULL, /* hardware_info */
                                               &error)) {
    modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  modem_mbim->imei_ = caps_device_id;
  VLOG(2) << "IMEI received from modem:" << modem_mbim->imei_;
  modem_mbim->ProcessMbimResult(kModemSuccess);
}

/* static */
void ModemMbim::UiccLowLevelAccessCloseChannelSetCb(MbimDevice* device,
                                                    GAsyncResult* res,
                                                    ModemMbim* modem_mbim) {
  g_autoptr(GError) error = NULL;
  g_autoptr(MbimMessage) response = NULL;
  guint32 status;
  LOG(INFO) << __func__;
  response = mbim_device_command_finish(device, res, &error);
  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_ms_uicc_low_level_access_close_channel_response_parse(
          response, &status, &error)) {
    modem_mbim->channel_ = kInvalidChannel;
    modem_mbim->ProcessMbimResult(kModemSuccess);
    return;
  }
  if (g_error_matches(error, MBIM_STATUS_ERROR,
                      MBIM_STATUS_ERROR_OPERATION_NOT_ALLOWED)) {
    LOG(INFO) << "Operation not allowed from modem:" << error->message;
  } else {
    LOG(INFO) << "Channel could not be closed:" << error->message;
  }
  modem_mbim->ProcessMbimResult(kModemSuccess);
}

/* static */
void ModemMbim::UiccLowLevelAccessOpenChannelSetCb(MbimDevice* device,
                                                   GAsyncResult* res,
                                                   ModemMbim* modem_mbim) {
  g_autoptr(GError) error = NULL;
  g_autoptr(MbimMessage) response = NULL;
  guint32 status;
  guint32 chl;
  guint32 rsp_size;
  const guint8* rsp = NULL;
  LOG(INFO) << __func__;
  response = mbim_device_command_finish(device, res, &error);
  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_ms_uicc_low_level_access_open_channel_response_parse(
          response, &status, &chl, &rsp_size, &rsp, &error)) {
    if (status != kMbimMessageSuccess) {
      LOG(INFO) << "Could not open channel:" << error->message
                << ". Inserted sim may not be an eSIM.";
      modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
      return;
    }
    VLOG(2) << "Successfully opened channel:" << chl;
    modem_mbim->channel_ = chl;
    modem_mbim->ProcessMbimResult(kModemSuccess);
    return;
  }
  if (g_error_matches(error, MBIM_STATUS_ERROR,
                      MBIM_STATUS_ERROR_OPERATION_NOT_ALLOWED)) {
    LOG(INFO) << "Modem FW may not support eSIM:" << error->message;
  } else {
    LOG(INFO) << "Could not open channel:" << error->message
              << ". Inserted sim may not be an eSIM.";
  }
  // not being able to open a channel is irrecoverable on L850. On dual sim
  // modems too, we should not attempt to open a channel unless we know we have
  // an eSIM.
  modem_mbim->retry_count_ = kMaxRetries + 1;
  modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
}

/* static */
bool ModemMbim::ParseEidApduResponse(const MbimMessage* response,
                                     std::string* eid) {
  g_autoptr(GError) error = NULL;
  guint32 status;
  guint32 response_size = 0;
  const guint8* out_response = NULL;
  std::vector<uint8_t> kGetEidDgiTag = {0xBF, 0x3E, 0x12, 0x5A, 0x10};
  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_ms_uicc_low_level_access_apdu_response_parse(
          response, &status, &response_size, &out_response, &error)) {
    if (response_size < 2 || out_response[0] != kGetEidDgiTag[0] ||
        out_response[1] != kGetEidDgiTag[1]) {
      return false;
    }
    VLOG(2) << "Adding to payload from APDU response (" << response_size
            << " bytes)"
            << base::HexEncode(&out_response[kGetEidDgiTag.size()],
                               response_size - kGetEidDgiTag.size());
    for (int j = kGetEidDgiTag.size(); j < response_size; j++) {
      *eid += bcd_chars[(out_response[j] >> 4) & 0xF];
      *eid += bcd_chars[out_response[j] & 0xF];
    }
    return true;
  }
  return false;
}

/* static */
void ModemMbim::UiccLowLevelAccessApduEidParse(MbimDevice* device,
                                               GAsyncResult* res,
                                               ModemMbim* modem_mbim) {
  g_autoptr(GError) error = NULL;
  std::string eid;
  g_autoptr(MbimMessage) response = NULL;
  response = mbim_device_command_finish(device, res, &error);
  if (ParseEidApduResponse(response, &eid)) {
    VLOG(2) << "EID for physical slot:"
            << modem_mbim->slot_info_.cached_active_slot_ << " is " << eid;
    modem_mbim->slot_info_.SetSlotStateActiveSlot(MBIM_UICC_SLOT_STATE_UNKNOWN);
    if (!eid.empty()) {
      modem_mbim->slot_info_.SetEidActiveSlot(std::move(eid));
      modem_mbim->slot_info_.SetSlotStateActiveSlot(
          MBIM_UICC_SLOT_STATE_ACTIVE_ESIM);
    }
    modem_mbim->ProcessMbimResult(kModemSuccess);
    return;
  }
  LOG(ERROR) << "Could not read EID";
  modem_mbim->euicc_manager_->OnEuiccRemoved(
      modem_mbim->slot_info_.cached_active_slot_ + 1);
  modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
}

/* static */
void ModemMbim::UiccLowLevelAccessApduResponseParse(MbimDevice* device,
                                                    GAsyncResult* res,
                                                    ModemMbim* modem_mbim) {
  g_autoptr(GError) error = NULL;
  g_autoptr(MbimMessage) response = NULL;
  guint32 status;
  guint32 response_size = 0;
  const guint8* out_response;
  CHECK(modem_mbim->tx_queue_.size());
  // Ensure that the queued element is for a kSendApdu command
  TxInfo* base_info = modem_mbim->tx_queue_[0].info_.get();
  CHECK(base_info);
  static ResponseApdu payload;
  ApduTxInfo* info = static_cast<ApduTxInfo*>(base_info);
  response = mbim_device_command_finish(device, res, &error);
  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_ms_uicc_low_level_access_apdu_response_parse(
          response, &status, &response_size, &out_response, &error)) {
    LOG(INFO) << "Adding to payload from APDU response (" << response_size
              << " bytes)";
    VLOG(2) << "Payload: " << base::HexEncode(out_response, response_size)
            << ", status: " << status;

    payload.AddData(out_response, response_size);
    payload.AddStatusBytes((status >> 8) & 0xFF, status & 0xFF);
    if (payload.MorePayloadIncoming()) {
      // Make the next transmit operation be a request for more APDU data
      info->apdu_ = payload.CreateGetMoreCommand(/* is_extended_apdu */ false,
                                                 info->apdu_.cls_);
      LOG(INFO) << "Requesting more APDUs...";
      modem_mbim->TransmitFromQueue();
      return;
    }
    if (info->apdu_.HasMoreFragments()) {
      // Send next fragment of APDU
      LOG(INFO) << "Sending next APDU fragment...";
      modem_mbim->TransmitFromQueue();
      return;
    }
    modem_mbim->responses_.push_back(std::move(payload));
    std::move(modem_mbim->tx_queue_[0].cb_).Run(lpa::card::EuiccCard::kNoError);
    modem_mbim->tx_queue_.pop_front();
    modem_mbim->TransmitFromQueue();
  } else {
    LOG(ERROR) << __func__ << ": Failed to parse APDU response";
    std::move(modem_mbim->tx_queue_[0].cb_)
        .Run(lpa::card::EuiccCard::kSendApduError);
    modem_mbim->tx_queue_.pop_front();
    modem_mbim->TransmitFromQueue();
    return;
  }
}

void ModemMbim::DeviceSlotStatusInfoRspCb(MbimDevice* device,
                                          GAsyncResult* res,
                                          ModemMbim* modem_mbim) {
  g_autoptr(MbimMessage) response = NULL;
  g_autoptr(GError) error = NULL;
  guint32 slot_index;
  MbimUiccSlotState slot_status;
  response = mbim_device_command_finish(device, res, &error);
  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_ms_basic_connect_extensions_slot_info_status_response_parse(
          response, &slot_index, &slot_status, &error)) {
    modem_mbim->slot_info_.slot_state_[slot_index] = slot_status;
    VLOG(2) << "Response received with slot_index:" << slot_index + 1
            << " & status:" << slot_status;
    modem_mbim->ProcessMbimResult(kModemSuccess);
    return;
  }
  LOG(ERROR) << __func__ << ":" << error->message;
  modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
}

void ModemMbim::QuerySysCapsReady(MbimDevice* device,
                                  GAsyncResult* res,
                                  ModemMbim* modem_mbim) {
  g_autoptr(MbimMessage) response = NULL;
  g_autoptr(GError) error = NULL;
  guint32 number_executors;
  guint32 number_slots;
  guint32 concurrency;
  guint64 modem_id;
  LOG(INFO) << __func__;
  response = mbim_device_command_finish(device, res, &error);
  if (!response ||
      !mbim_message_response_get_result(
          response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
      !mbim_message_ms_basic_connect_extensions_sys_caps_response_parse(
          response, &number_executors, &number_slots, &concurrency, &modem_id,
          &error)) {
    modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  VLOG(2) << "executor index:" << number_executors
          << " Number of slots:" << number_slots;
  modem_mbim->slot_info_.InitSlotInfo(number_slots, number_executors);
  modem_mbim->ProcessMbimResult(kModemSuccess);
}

void ModemMbim::DeviceSlotStatusMappingRspCb(MbimDevice* device,
                                             GAsyncResult* res,
                                             ModemMbim* modem_mbim) {
  g_autoptr(MbimMessage) response = NULL;
  g_autoptr(GError) error = NULL;
  guint32 map_count = 0;
  g_autoptr(MbimSlotArray) slot_mappings = NULL;
  response = mbim_device_command_finish(device, res, &error);
  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_ms_basic_connect_extensions_device_slot_mappings_response_parse(
          response, &map_count, &slot_mappings, &error)) {
    CHECK_EQ(map_count, 1) << "Unexpected multi radio modem";
    modem_mbim->slot_info_.map_count_ = map_count;
    modem_mbim->slot_info_.cached_active_slot_ =
        slot_mappings[kExecutorIndex]->slot;
    modem_mbim->slot_info_.get_slot_mapping_result_ =
        slot_mappings[kExecutorIndex]->slot;
    VLOG(2) << "Map count:" << map_count
            << "& current active slot:" << slot_mappings[kExecutorIndex]->slot;
    modem_mbim->ProcessMbimResult(kModemSuccess);
    return;
  }
  modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
}

void ModemMbim::SetDeviceSlotMappingsRspCb(MbimDevice* device,
                                           GAsyncResult* res,
                                           ModemMbim* modem_mbim) {
  g_autoptr(MbimMessage) response = NULL;
  g_autoptr(GError) error = NULL;
  guint32 map_count = 0;
  uint8_t physical_switch_slot = 1;
  g_autoptr(MbimSlotArray) slot_mappings = NULL;
  VLOG(2) << __func__;
  auto switch_slot_tx_info =
      dynamic_cast<SwitchSlotTxInfo*>(modem_mbim->tx_queue_[0].info_.get());
  physical_switch_slot = switch_slot_tx_info->physical_slot_;
  response = mbim_device_command_finish(device, res, &error);
  if (!response ||
      !mbim_message_response_get_result(
          response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
      !mbim_message_ms_basic_connect_extensions_device_slot_mappings_response_parse(
          response, &map_count, &slot_mappings, &error)) {
    LOG(ERROR) << "Sim slot switch to " << physical_switch_slot + 1
               << " failed";
    modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  if (physical_switch_slot != slot_mappings[kExecutorIndex]->slot) {
    LOG(ERROR) << "Sim slot switch to " << physical_switch_slot + 1
               << " failed";
    modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  modem_mbim->slot_info_.cached_active_slot_ = physical_switch_slot;
  for (int i = 0; i < modem_mbim->slot_info_.slot_count_; i++) {
    if (i != physical_switch_slot) {
      modem_mbim->euicc_manager_->OnLogicalSlotUpdated(
          modem_mbim->slot_info_.cached_active_slot_, base::nullopt);
      continue;
    }
    modem_mbim->euicc_manager_->OnLogicalSlotUpdated(
        modem_mbim->slot_info_.cached_active_slot_, kExecutorIndex);
  }
  LOG(INFO) << "Sim switch was successful to:"
            << slot_mappings[kExecutorIndex]->slot + 1;
  modem_mbim->ProcessMbimResult(kModemSuccess);
}

/* static */
void ModemMbim::ClientIndicationCb(MbimDevice* device,
                                   MbimMessage* notification,
                                   ModemMbim* modem_mbim) {
  MbimService service;
  service = mbim_message_indicate_status_get_service(notification);
  VLOG(2) << "Received notification for service:"
          << mbim_service_get_string(service);
  VLOG(2) << "Command received from the modem:"
          << mbim_cid_get_printable(
                 service, mbim_message_indicate_status_get_cid(notification));
  switch (service) {
    case MBIM_SERVICE_BASIC_CONNECT:
      if (mbim_message_indicate_status_get_cid(notification) ==
          MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS) {
        modem_mbim->is_ready_state_valid =
            GetReadyState(device, true /*is_notification*/, notification,
                          &modem_mbim->ready_state_);
        LOG(INFO) << "Current sim status:" << modem_mbim->ready_state_;
        if (modem_mbim->ready_state_ ==
            MBIM_SUBSCRIBER_READY_STATE_INITIALIZED) {
          VLOG(2) << "Sim has one profile enabled";
        } else if (modem_mbim->ready_state_ ==
                   MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED) {
          VLOG(2) << "Sim not inserted";
        }
      }
      break;
    default:
      VLOG(2) << "Indication received is not handled";
      break;
  }
  return;
}

void ModemMbim::CloseDevice() {
  if (device_ && g_signal_handler_is_connected(device_.get(), indication_id_))
    g_signal_handler_disconnect(device_.get(), indication_id_);
  device_.reset();
}

bool ModemMbim::State::Transition(ModemMbim::State::Value value) {
  bool valid_transition;
  switch (value) {
    case kMbimUninitialized:
      valid_transition = true;
      break;
    case kCloseChannel:
      valid_transition = (value_ == kCheckSingleSim || value_ == kSlotInfo);
      break;
    case kMbimStarted:
      valid_transition =
          (value_ == kCloseChannel || value_ == kEidReadComplete);
      break;
    default:
      // Most states can only transition from the previous state.
      valid_transition = (value == value_ + 1);
  }
  if (valid_transition) {
    LOG(INFO) << "Transitioning from state " << *this << " to state "
              << State(value);
    value_ = value;
  } else {
    LOG(ERROR) << "Cannot transition from state " << *this << " to state "
               << State(value);
  }
  return valid_transition;
}

std::ostream& operator<<(std::ostream& os, const ModemMbim::State state) {
  switch (state.value_) {
    case ModemMbim::State::kMbimUninitialized:
      os << "Uninitialized";
      break;
    case ModemMbim::State::kMbimInitializeStarted:
      os << "InitializeStarted";
      break;
    case ModemMbim::State::kReadImei:
      os << "GetReadImei";
      break;
    case ModemMbim::State::kGetSubscriberReadyState:
      os << "GetSubscriberReadyState";
      break;
    case ModemMbim::State::kCheckSingleSim:
      os << "CheckSingleSim";
      break;
    case ModemMbim::State::kSysQuery:
      os << "GetSysQuery";
      break;
    case ModemMbim::State::kDeviceSlotMapping:
      os << "GetDeviceSlotMapping";
      break;
    case ModemMbim::State::kSlotInfo:
      os << "GetSlotInfo";
      break;
    case ModemMbim::State::kCloseChannel:
      os << "Close Channel";
      break;
    case ModemMbim::State::kOpenChannel:
      os << "Open Channel";
      break;
    case ModemMbim::State::kReadEid:
      os << "GetReadEid";
      break;
    case ModemMbim::State::kEidReadComplete:
      os << "kEidReadComplete";
      break;
    case ModemMbim::State::kMbimStarted:
      os << "MbimStarted";
      break;
  }
  return os;
}

void ModemMbim::OnEuiccEventStart(const uint32_t physical_slot,
                                  bool switch_slot_only,
                                  EuiccEventStep step,
                                  ResultCallback cb) {
  LOG(INFO) << __func__ << ": step: " << to_underlying(step);
  switch (step) {
    case EuiccEventStep::GET_MBIM_DEVICE:
      CloseDevice();
      {
        auto next_step = (switch_slot_only) ? EuiccEventStep::SET_SLOT_MAPPING
                                            : EuiccEventStep::GET_SLOT_MAPPING;
        auto get_slot_mapping = base::BindOnce(
            &ModemMbim::OnEuiccEventStart, weak_factory_.GetWeakPtr(),
            physical_slot, switch_slot_only, next_step);
        init_done_cb_ = base::BindOnce(
            &RunNextStep, std::move(get_slot_mapping), std::move(cb));
        mbim_device_new(file_, /* cancellable */ NULL,
                        (GAsyncReadyCallback)MbimCreateNewDeviceCb, this);
      }
      break;
    case EuiccEventStep::GET_SLOT_MAPPING:
      if (!mbim_device_check_ms_mbimex_version(device_.get(), 2, 0)) {
        OnEuiccEventStart(physical_slot, false /* switch_slot_only */
                          ,
                          EuiccEventStep::CLOSE_CHANNEL, std::move(cb));
        break;
      }
      SendMessage(MbimCmd::MbimType::kMbimDeviceSlotMapping,
                  std::make_unique<SwitchSlotTxInfo>(physical_slot),
                  std::move(cb), &ModemMbim::OnEuiccEventStart, physical_slot,
                  switch_slot_only, EuiccEventStep::GET_SLOT_INFO);
      break;
    case EuiccEventStep::GET_SLOT_INFO: {
      auto next_step = (physical_slot == slot_info_.cached_active_slot_)
                           ? EuiccEventStep::CLOSE_CHANNEL
                           : EuiccEventStep::SET_SLOT_MAPPING;
      SendMessage(MbimCmd::MbimType::kMbimSlotInfoStatus,
                  std::make_unique<SwitchSlotTxInfo>(physical_slot),
                  std::move(cb), &ModemMbim::OnEuiccEventStart, physical_slot,
                  switch_slot_only, next_step);
    } break;
    case EuiccEventStep::SET_SLOT_MAPPING: {
      auto next_step = (switch_slot_only)
                           ? EuiccEventStep::EUICC_EVENT_STEP_LAST
                           : EuiccEventStep::CLOSE_CHANNEL;
      SendMessage(MbimCmd::MbimType::kMbimSetDeviceSlotMapping,
                  std::make_unique<SwitchSlotTxInfo>(physical_slot),
                  std::move(cb), &ModemMbim::OnEuiccEventStart, physical_slot,
                  switch_slot_only, next_step);
    } break;
    case EuiccEventStep::CLOSE_CHANNEL:
      if (!slot_info_.IsEsimActive()) {
        LOG(ERROR) << "Active slot does not have an eSIM";
        euicc_manager_->OnEuiccRemoved(slot_info_.cached_active_slot_ + 1);
        std::move(cb).Run(kModemMessageProcessingError);
        break;
      }
      SendMessage(MbimCmd::MbimType::kMbimCloseChannel,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::OnEuiccEventStart, physical_slot,
                  switch_slot_only, EuiccEventStep::OPEN_CHANNEL);
      break;
    case EuiccEventStep::OPEN_CHANNEL:
      SendMessage(MbimCmd::MbimType::kMbimOpenChannel,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::OnEuiccEventStart, physical_slot,
                  switch_slot_only, EuiccEventStep::GET_EID);
      break;
    case EuiccEventStep::GET_EID:
      SendMessage(MbimCmd::MbimType::kMbimSendEidApdu,
                  std::unique_ptr<TxInfo>(), std::move(cb),
                  &ModemMbim::OnEuiccEventStart, physical_slot,
                  switch_slot_only, EuiccEventStep::EUICC_EVENT_STEP_LAST);
      break;
    case EuiccEventStep::EUICC_EVENT_STEP_LAST:
      OnEuiccUpdated();
      std::move(cb).Run(kModemSuccess);
      break;
  }
}

void ModemMbim::StoreAndSetActiveSlot(const uint32_t physical_slot,
                                      ResultCallback cb) {
  LOG(INFO) << __func__ << ": physical_slot:" << physical_slot;
}

void ModemMbim::ProcessEuiccEvent(EuiccEvent event, ResultCallback cb) {
  LOG(INFO) << __func__ << ": " << event;
  if (event.step == EuiccStep::START) {
    auto on_euicc_event_start = base::BindOnce(
        &ModemMbim::OnEuiccEventStart, weak_factory_.GetWeakPtr(),
        event.slot - 1, false /* switch_slot_only */,
        EuiccEventStep::GET_MBIM_DEVICE);
    modem_manager_proxy_->WaitForModemAndInhibit(base::BindOnce(
        &RunNextStep, std::move(on_euicc_event_start), std::move(cb)));
    return;
  }
  if (event.step == EuiccStep::PENDING_NOTIFICATIONS) {
    AcquireChannelAfterCardReady(event, std::move(cb));
    return;
  }
  if (event.step == EuiccStep::END) {
    slot_info_.get_slot_mapping_result_.reset();
    auto close_device_and_uninhibit = base::BindOnce(
        &ModemMbim::CloseDeviceAndUninhibit, weak_factory_.GetWeakPtr());
    // Close channel, followed by close device and uninhibit, and then execute
    // cb
    SendMessage(MbimCmd::MbimType::kMbimCloseChannel, std::unique_ptr<TxInfo>(),
                std::move(cb), &ModemMbim::CloseDeviceAndUninhibit);
    return;
  }
}

void ModemMbim::AcquireChannelAfterCardReady(EuiccEvent event,
                                             ResultCallback cb) {
  const guint MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE = 7;
  if (!is_ready_state_valid ||
      !(ready_state_ == MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED ||
        ready_state_ == MBIM_SUBSCRIBER_READY_STATE_INITIALIZED ||
        ready_state_ == MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED ||
        ready_state_ == MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE)) {
    if (retry_count_ > kMaxRetries) {
      LOG(ERROR) << "Could not finish profile operation,ready_state_="
                 << ready_state_
                 << ", is_ready_state_valid=" << is_ready_state_valid;
      std::move(cb).Run(kModemMessageProcessingError);
      return;
    }
    retry_count_++;
    executor_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ModemMbim::AcquireChannelAfterCardReady,
                       weak_factory_.GetWeakPtr(), std::move(event),
                       std::move(cb)),
        kSimRefreshDelay);
    return;
  }
  retry_count_ = 0;
  ReacquireChannel(EuiccEventStep::OPEN_CHANNEL, std::move(cb));
}

void ModemMbim::CloseDeviceAndUninhibit(ResultCallback cb) {
  CloseDevice();
  modem_manager_proxy_->ScheduleUninhibit(kUninhibitDelay);
  std::move(cb).Run(kModemSuccess);
}

void ModemMbim::RestoreActiveSlot(ResultCallback cb) {
  LOG(INFO) << __func__;
  if (!mbim_device_check_ms_mbimex_version(device_.get(), 2, 0)) {
    std::move(cb).Run(kModemSuccess);
    return;
  }
  if (!slot_info_.get_slot_mapping_result_.has_value()) {
    LOG(ERROR) << "Could not find slot number to switch to";
    std::move(cb).Run(kModemMessageProcessingError);
    return;
  }
  if (slot_info_.get_slot_mapping_result_ == slot_info_.cached_active_slot_) {
    VLOG(2) << __func__ << "Already on the right slot";
    std::move(cb).Run(kModemSuccess);
    return;
  }
  OnEuiccEventStart(slot_info_.get_slot_mapping_result_.value(),
                    true /* switch_slot_only */,
                    EuiccEventStep::GET_MBIM_DEVICE, std::move(cb));
}

bool ModemMbim::IsSimValidAfterEnable() {
  VLOG(2) << __func__;
  return false;
  // The sim issues a proactive refresh after an enable. This
  // function should return true immediately after the refresh completes,
  // However, the LPA expects that this function does not read any
  // other state variable. Thus, we simply return false until the LPA
  // times out, and then finish the operation. This imposes a 15 sec penalty
  // on every enable and 30 sec penalty on every disable.
  // A workaround is to return true and complete the eSIM operation before the
  // refresh. FinishProfileOp can gate the dbus response until the refresh is
  // complete. However, this exposes UI issues.
}

bool ModemMbim::IsSimValidAfterDisable() {
  VLOG(2) << __func__;
  return false;
}

/* static */
bool ModemMbim::ParseEidApduResponseForTesting(const MbimMessage* response,
                                               std::string* eid) {
  return ParseEidApduResponse(response, eid);
}

template <typename Func, typename... Args>
void ModemMbim::SendMessage(MbimCmd::MbimType type,
                            std::unique_ptr<TxInfo> tx_info,
                            ResultCallback cb,
                            Func&& next_step,
                            Args&&... args) {
  // First, transmit an mbim message. If the message is successful, run
  // next_step(args).
  auto next_step_weak =
      base::BindOnce(next_step, weak_factory_.GetWeakPtr(), args...);
  auto run_next_step =
      base::BindOnce(&ModemMbim::RunNextStepOrRetry, weak_factory_.GetWeakPtr(),
                     std::move(next_step_weak), std::move(cb));
  tx_queue_.push_back({std::move(tx_info), AllocateId(),
                       std::make_unique<MbimCmd>(type),
                       std::move(run_next_step)});
  TransmitFromQueue();
}

bool ModemMbim::SlotInfo::IsEuiccPresentOnAnySlot() const {
  for (int i = 0; i < slot_count_; i++) {
    if (IsEuicc(i)) {
      return true;
    }
  }
  return false;
}

bool ModemMbim::SlotInfo::IsEuicc(int slot) const {
  return (slot_state_[slot] == MBIM_UICC_SLOT_STATE_ACTIVE_ESIM) ||
         (slot_state_[slot] == MBIM_UICC_SLOT_STATE_ACTIVE_ESIM_NO_PROFILES);
}

void ModemMbim::SlotInfo::InitSlotInfo(guint32 slot_count, guint32 map_count) {
  slot_count_ = slot_count;
  map_count_ = map_count;  // number of executors/radios
  slot_state_.clear();
  eid_.clear();
  for (int i = 0; i < slot_count_; i++) {
    slot_state_.push_back(MBIM_UICC_SLOT_STATE_UNKNOWN);
    eid_.emplace_back();
  }
}

void ModemMbim::SlotInfo::SetEidActiveSlot(std::string eid) {
  eid_[cached_active_slot_] = std::move(eid);
}

void ModemMbim::SlotInfo::SetSlotStateActiveSlot(MbimUiccSlotState state) {
  slot_state_[cached_active_slot_] = state;
}

std::ostream& operator<<(std::ostream& os, const ModemMbim::SlotInfo& info) {
  os << "map_count_: " << info.map_count_
     << " slot_count_: " << info.slot_count_
     << " cached_active_slot_: " << info.cached_active_slot_;
  for (int i = 0; i < info.slot_count_; i++) {
    os << " slot_state[" << i << "]: " << info.slot_state_[i] << " eid[" << i
       << "]:" << info.eid_[i];
  }
  return os;
}

}  // namespace hermes
