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

constexpr int kEsimSlot = 1;
const guint kMbimResponseTimeout = 30;
constexpr int kMbimMessageSuccess = 144;
// Application identifier for the eUICC's SIM EID
const std::array<uint8_t, 12> kMbimEidReqApdu = {
    0x81, 0xE2, 0x91, 0x00, 0x06, 0xBF, 0x3E, 0x03, 0x5C, 0x01, 0x5A, 0x00,
};

// ModemManager uses channel_group=1. Make Hermes use 2 just to be cautious.
constexpr int kChannelGroupId = 2;

}  // namespace

namespace hermes {

/* static */
std::unique_ptr<ModemMbim> ModemMbim::Create(Logger* logger,
                                             Executor* executor) {
  VLOG(2) << __func__;
  GFile* file = NULL;
  const gchar* const path = "/dev/cdc-wdm0";
  file = g_file_new_for_path(path);
  if (file == NULL) {
    LOG(ERROR) << __func__ << " :No file exist";
    return nullptr;
  }
  return std::unique_ptr<ModemMbim>(new ModemMbim(file, logger, executor));
}

ModemMbim::ModemMbim(GFile* file, Logger* logger, Executor* executor)
    : Modem<MbimCmd>(logger, executor),
      channel_(kInvalidChannel),
      pending_response_(false),
      ready_state_(MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED),
      file_(file),
      is_ready_state_valid(false),
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
  init_done_cb_ = std::move(cb);
  current_state_.Transition(State::kMbimInitializeStarted);
  mbim_device_new(file_, /* cancellable */ NULL,
                  (GAsyncReadyCallback)MbimCreateNewDeviceCb, this);
}

void ModemMbim::Shutdown() {
  VLOG(2) << __func__;
  CloseDevice();
  channel_ = kInvalidChannel;
  pending_response_ = false;
  ready_state_ = MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED,
  current_state_.Transition(State::kMbimUninitialized);
}

void ModemMbim::TransmitFromQueue() {
  VLOG(2) << __func__;
  if (tx_queue_.empty() || pending_response_ ||
      retry_initialization_callback_) {
    return;
  }

  auto mbim_cmd = tx_queue_[0].msg_.get();
  switch (mbim_cmd->mbim_type()) {
    case MbimCmd::MbimType::kMbimOpenLogicalChannel:
      TransmitMbimOpenLogicalChannel();
      break;
    case MbimCmd::MbimType::kMbimCloseLogicalChannel:
      TransmitMbimCloseChannel();
      break;
    case MbimCmd::MbimType::kMbimSendApdu:
      TransmitMbimSendApdu(&tx_queue_[0]);
      break;
    case MbimCmd::MbimType::kMbimSubscriberStatusReady:
      TransmitSubscriberReadyStatusQuery();
      break;
    case MbimCmd::MbimType::kMbimDeviceCaps:
      TransmitMbimLoadCurrentCapabilities();
      break;
    case MbimCmd::MbimType::kMbimSendEidApdu:
      TransmitMbimSendEidApdu();
      break;
    default:
      break;
  }
}

std::unique_ptr<MbimCmd> ModemMbim::GetTagForSendApdu() {
  return std::make_unique<MbimCmd>(MbimCmd::MbimType::kMbimSendApdu);
}

void ModemMbim::ProcessMbimResult(int err) {
  if (tx_queue_.empty()) {
    VLOG(2) << __func__ << " :Queue is Empty";
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
  g_autoptr(GError) error = NULL;
  glib_bridge::ScopedGObject<MbimDevice> mbimdevice(
      mbim_device_new_finish(res, &error));
  modem_mbim->device_ = std::move(mbimdevice);
  if (!modem_mbim->device_.get() || error != NULL) {
    // TODO(pholla): Gate initialization until a modem dbus object appears.
    // (b/197256318)
    LOG(INFO) << __func__ << ": " << error->message
              << ". The modem may be booting ...";
    modem_mbim->RetryInitialization(std::move(modem_mbim->init_done_cb_));
    return;
  }
  mbim_device_open_full(modem_mbim->device_.get(), MBIM_DEVICE_OPEN_FLAGS_PROXY,
                        kMbimResponseTimeout, /* cancellable */ NULL,
                        (GAsyncReadyCallback)MbimDeviceOpenReadyCb, modem_mbim);
}

/* static */
void ModemMbim::MbimDeviceOpenReadyCb(MbimDevice* device,
                                      GAsyncResult* res,
                                      ModemMbim* modem_mbim) {
  VLOG(2) << __func__;
  g_autoptr(GError) error = NULL;
  if (!mbim_device_open_finish(device, res, &error)) {
    LOG(ERROR) << "Failed  due to error: " << error->message;
    modem_mbim->RetryInitialization(std::move(modem_mbim->init_done_cb_));
    return;
  }
  modem_mbim->indication_id_ = g_signal_connect(
      modem_mbim->device_.get(), MBIM_DEVICE_SIGNAL_INDICATE_STATUS,
      G_CALLBACK(ClientIndicationCb), modem_mbim);

  if (modem_mbim->current_state_ == State::kMbimStarted) {
    VLOG(2) << "Opened device. Reusing previous EID and IMEI";
    std::move(modem_mbim->init_done_cb_).Run(kModemSuccess);
    return;
  }

  LOG(INFO) << "Mbim device is ready, acquire eid and imei";
  auto get_imei = base::BindOnce(&ModemMbim::QueryCurrentMbimCapabilities,
                                 modem_mbim->weak_factory_.GetWeakPtr());

  modem_mbim->tx_queue_.push_back(
      {std::unique_ptr<TxInfo>(), modem_mbim->AllocateId(),
       std::make_unique<MbimCmd>(MbimCmd::MbimType::kMbimSubscriberStatusReady),
       base::BindOnce(&ModemMbim::RunNextStepOrRetry,
                      modem_mbim->weak_factory_.GetWeakPtr(),
                      std::move(get_imei),
                      std::move(modem_mbim->init_done_cb_))});
  modem_mbim->TransmitFromQueue();
}

void ModemMbim::TransmitSubscriberReadyStatusQuery() {
  g_autoptr(MbimMessage) message = NULL;
  VLOG(2) << __func__;
  message = mbim_message_subscriber_ready_status_query_new(NULL);
  if (!message) {
    LOG(ERROR) << "Mbim message creation failed";
    ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  mbim_device_command(device_.get(), message, kMbimResponseTimeout,
                      /*cancellable*/ NULL,
                      (GAsyncReadyCallback)SubscriberReadyStatusRspCb, this);
}

void ModemMbim::TransmitMbimLoadCurrentCapabilities() {
  g_autoptr(MbimMessage) message = NULL;
  VLOG(2) << __func__;
  message = mbim_message_device_caps_query_new(/* error */ NULL);
  if (!message) {
    LOG(ERROR) << __func__ << " :Mbim message creation failed";
    ProcessMbimResult(kModemMessageProcessingError);
    return;
  }
  mbim_device_command(device_.get(), message, kMbimResponseTimeout,
                      /*cancellable*/ NULL,
                      (GAsyncReadyCallback)DeviceCapsQueryReady, this);
}

void ModemMbim::TransmitMbimCloseChannel() {
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
  pending_response_ = true;
  mbim_device_command(device_.get(), message, kMbimResponseTimeout,
                      /*cancellable*/ NULL,
                      (GAsyncReadyCallback)UiccLowLevelAccessCloseChannelSetCb,
                      this);
}

void ModemMbim::TransmitMbimOpenLogicalChannel() {
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
  pending_response_ = true;
  mbim_device_command(device_.get(), message, kMbimResponseTimeout,
                      /*cancellable*/ NULL,
                      (GAsyncReadyCallback)UiccLowLevelAccessOpenChannelSetCb,
                      this);
}

void ModemMbim::TransmitMbimSendEidApdu() {
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
  mbim_device_command(device_.get(), message, kMbimResponseTimeout,
                      /*cancellable*/ NULL,
                      (GAsyncReadyCallback)UiccLowLevelAccessApduEidParse,
                      this);
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
  VLOG(2) << "Fragment size" << fragment_size;
  apdu_len = fragment_size;
  std::copy(fragment, fragment + fragment_size, apduCmd);
  apduCmd[apdu_len++] = 0x00;

  LOG(INFO) << "Sending APDU fragment (" << apdu_len << " bytes): over channel "
            << channel_;
  VLOG(2) << "APDU: " << base::HexEncode(apduCmd, apdu_len);
  pending_response_ = true;
  message = (mbim_message_ms_uicc_low_level_access_apdu_set_new(
      channel_, secure_messaging, class_byte_type, apdu_len, apduCmd, NULL));
  mbim_device_command(device_.get(), message, kMbimResponseTimeout,
                      /* cancellable */ NULL,
                      (GAsyncReadyCallback)UiccLowLevelAccessApduResponseParse,
                      this);

  return;
}

void ModemMbim::QueryCurrentMbimCapabilities(ResultCallback cb) {
  auto reacquire_channel = base::BindOnce(&ModemMbim::GetEidStepCloseChannel,
                                          weak_factory_.GetWeakPtr());
  tx_queue_.push_front(
      {std::make_unique<TxInfo>(), AllocateId(),
       std::make_unique<MbimCmd>(MbimCmd::MbimType::kMbimDeviceCaps),
       base::BindOnce(&ModemMbim::RunNextStepOrRetry,
                      weak_factory_.GetWeakPtr(), std::move(reacquire_channel),
                      std::move(cb))});
  TransmitFromQueue();
}

void ModemMbim::AcquireChannel(base::OnceCallback<void(int)> cb) {
  LOG(INFO) << __func__;
  tx_queue_.push_back(
      {std::unique_ptr<TxInfo>(), AllocateId(),
       std::make_unique<MbimCmd>(MbimCmd::MbimType::kMbimOpenLogicalChannel),
       std::move(cb)});
  TransmitFromQueue();
}

void ModemMbim::ReacquireChannel(const uint32_t physical_slot,
                                 ResultCallback cb) {
  LOG(INFO) << __func__ << " with physical_slot: " << physical_slot;
  auto acquire_channel =
      base::BindOnce(&ModemMbim::AcquireChannel, weak_factory_.GetWeakPtr());
  tx_queue_.push_back(
      {std::unique_ptr<TxInfo>(), AllocateId(),
       std::make_unique<MbimCmd>(MbimCmd::MbimType::kMbimCloseLogicalChannel),
       base::BindOnce(&RunNextStep, std::move(acquire_channel),
                      std::move(cb))});
  TransmitFromQueue();
}

void ModemMbim::GetEidFromSim(ResultCallback cb) {
  VLOG(2) << __func__;
  tx_queue_.push_back(
      {std::unique_ptr<TxInfo>(), AllocateId(),
       std::make_unique<MbimCmd>(MbimCmd::MbimType::kMbimSendEidApdu),
       std::move(cb)});
  TransmitFromQueue();
}

void ModemMbim::GetEidStepCloseChannel(ResultCallback cb) {
  auto open_channel = base::BindOnce(&ModemMbim::GetEidStepOpenChannel,
                                     weak_factory_.GetWeakPtr());
  tx_queue_.push_back(
      {std::unique_ptr<TxInfo>(), AllocateId(),
       std::make_unique<MbimCmd>(MbimCmd::MbimType::kMbimCloseLogicalChannel),
       base::BindOnce(&RunNextStep, std::move(open_channel), std::move(cb))});
  TransmitFromQueue();
}

void ModemMbim::GetEidStepOpenChannel(ResultCallback cb) {
  VLOG(2) << __func__;

  auto get_eid_from_sim =
      base::BindOnce(&ModemMbim::GetEidFromSim, weak_factory_.GetWeakPtr());
  tx_queue_.push_back(
      {std::unique_ptr<TxInfo>(), AllocateId(),
       std::make_unique<MbimCmd>(MbimCmd::MbimType::kMbimOpenLogicalChannel),
       base::BindOnce(&ModemMbim::RunNextStepOrRetry,
                      weak_factory_.GetWeakPtr(), std::move(get_eid_from_sim),
                      std::move(cb))});
  TransmitFromQueue();
}

/* static */
void ModemMbim::SubscriberReadyStatusRspCb(MbimDevice* device,
                                           GAsyncResult* res,
                                           ModemMbim* modem_mbim) {
  g_autoptr(MbimMessage) response = NULL;
  VLOG(2) << __func__;
  g_autoptr(GError) error = NULL;
  MbimSubscriberReadyState ready_state =
      MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED;
  g_autofree gchar* subscriber_id = NULL;
  response = mbim_device_command_finish(device, res, &error);
  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_subscriber_ready_status_response_parse(
          response, &ready_state, &subscriber_id,
          /* sim_iccid */ NULL,
          /* ready_info */ NULL,
          /* telephone_numbers_count */ NULL,
          /* telephone_numbers */ NULL, &error)) {
    modem_mbim->ready_state_ = ready_state;
    LOG(INFO) << "Current Sim status: " << ready_state;
    if (ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED) {
      VLOG(2) << "Sim not inserted";
      modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
      return;
    }
    if (ready_state == MBIM_SUBSCRIBER_READY_STATE_INITIALIZED)
      VLOG(2) << "Profile already enabled";

    modem_mbim->ProcessMbimResult(kModemSuccess);
    return;
  }
  LOG(ERROR) << __func__ << "Failed due to error: " << error->message;
  modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
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
  VLOG(2) << "IMEI received from modem: " << modem_mbim->imei_;
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
  modem_mbim->pending_response_ = false;

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
    LOG(INFO) << "Operation not allowed from modem: " << error->message;
  } else {
    LOG(INFO) << "Channel could not be closed: " << error->message;
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
  modem_mbim->pending_response_ = false;
  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_ms_uicc_low_level_access_open_channel_response_parse(
          response, &status, &chl, &rsp_size, &rsp, &error)) {
    if (status != kMbimMessageSuccess) {
      LOG(INFO) << "Could not open channel: " << error->message
                << ". Inserted sim may not be an eSIM.";
      modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
      return;
    }
    VLOG(2) << "Successfully opened channel: " << chl;
    modem_mbim->channel_ = chl;
    modem_mbim->ProcessMbimResult(kModemSuccess);
    return;
  }
  if (g_error_matches(error, MBIM_STATUS_ERROR,
                      MBIM_STATUS_ERROR_OPERATION_NOT_ALLOWED)) {
    LOG(INFO) << "Modem FW may not support eSIM: " << error->message;
  } else {
    LOG(INFO) << "Could not open channel:" << error->message
              << ". Inserted sim may not be an eSIM.";
  }
  modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
}

/* static */
void ModemMbim::UiccLowLevelAccessApduEidParse(MbimDevice* device,
                                               GAsyncResult* res,
                                               ModemMbim* modem_mbim) {
  g_autoptr(GError) error = NULL;
  g_autoptr(MbimMessage) response = NULL;
  guint32 status;
  guint32 response_size = 0;
  const guint8* out_response = NULL;
  std::vector<uint8_t> kGetEidDgiTag = {0xBF, 0x3E, 0x12, 0x5A, 0x10};
  response = mbim_device_command_finish(device, res, &error);

  // b/199808449. Close the device since we no longer need it. Hermes gets stuck
  // in an infinite loop if the modem is reset by modemfwd
  modem_mbim->CloseDevice();

  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_ms_uicc_low_level_access_apdu_response_parse(
          response, &status, &response_size, &out_response, &error)) {
    if (response_size < 2 || out_response[0] != kGetEidDgiTag[0] ||
        out_response[1] != kGetEidDgiTag[1]) {
      modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
      return;
    }

    VLOG(2) << "Adding to payload from APDU response (" << response_size
            << " bytes)"
            << base::HexEncode(&out_response[kGetEidDgiTag.size()],
                               response_size - kGetEidDgiTag.size());
    for (int j = kGetEidDgiTag.size(); j < response_size; j++) {
      modem_mbim->eid_ += bcd_chars[(out_response[j] >> 4) & 0xF];
      modem_mbim->eid_ += bcd_chars[out_response[j] & 0xF];
    }
    LOG(INFO) << "EID for physical slot: " << kEsimSlot << " is "
              << modem_mbim->eid_;
    if (modem_mbim->current_state_ == State::kMbimInitializeStarted)
      modem_mbim->current_state_.Transition(State::kMbimStarted);
    modem_mbim->euicc_manager_->OnEuiccUpdated(
        kEsimSlot, EuiccSlotInfo(kEsimSlot, std::move(modem_mbim->eid_)));
    modem_mbim->ProcessMbimResult(kModemSuccess);
    return;
  }
  LOG(INFO) << "Could not find eSIM";
  modem_mbim->euicc_manager_->OnEuiccRemoved(kEsimSlot);
  modem_mbim->ProcessMbimResult(kModemMessageProcessingError);
  return;
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
  modem_mbim->pending_response_ = false;
  if (response &&
      mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                       &error) &&
      mbim_message_ms_uicc_low_level_access_apdu_response_parse(
          response, &status, &response_size, &out_response, &error)) {
    LOG(INFO) << "Adding to payload from APDU response (" << response_size
              << " bytes)";
    VLOG(2) << "Payload: " << base::HexEncode(out_response, response_size);

    payload.AddData(out_response, response_size);
    if (payload.MorePayloadIncoming()) {
      // Make the next transmit operation be a request for more APDU data
      info->apdu_ = payload.CreateGetMoreCommand(/*is_extended_apdu*/ false);
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
    // In case of mbim there are no appended status bytes in the APDU received.
    // Hence no extra bytes to be removed.
    modem_mbim->responses_.push_back(payload.ReleaseOnly());
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

/* static */
void ModemMbim::ClientIndicationCb(MbimDevice* device,
                                   MbimMessage* notification,
                                   ModemMbim* modem_mbim) {
  MbimService service;
  g_autoptr(GError) error = NULL;
  service = mbim_message_indicate_status_get_service(notification);

  VLOG(2) << "Received notification for service: "
          << mbim_service_get_string(service);
  VLOG(2) << "Command received from the modem: "
          << mbim_cid_get_printable(
                 service, mbim_message_indicate_status_get_cid(notification));

  switch (service) {
    case MBIM_SERVICE_BASIC_CONNECT:
      if (mbim_message_indicate_status_get_cid(notification) ==
          MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS) {
        MbimSubscriberReadyState ready_state;
        if (mbim_message_subscriber_ready_status_notification_parse(
                notification, &ready_state,
                /* subscriber_id */ NULL,
                /* sim_iccid */ NULL,
                /* ready_info */ NULL,
                /* telephone_numbers_count */ NULL,
                /* telephone_numbers */ NULL, &error)) {
          modem_mbim->ready_state_ = ready_state;
          modem_mbim->is_ready_state_valid = true;
          LOG(INFO) << "Current sim status: " << ready_state;
          if (ready_state == MBIM_SUBSCRIBER_READY_STATE_INITIALIZED) {
            VLOG(2) << "Sim has one profile enabled";
          } else if (ready_state ==
                     MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED) {
            VLOG(2) << "Sim not inserted";
          }
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
  bool valid_transition = true;
  switch (value) {
    case kMbimUninitialized:
      valid_transition = true;
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

void ModemMbim::StoreAndSetActiveSlot(const uint32_t physical_slot,
                                      ResultCallback cb) {
  LOG(INFO) << __func__ << " physical_slot:" << physical_slot;
  // The modem may be reset, causing device_ to be invalid. Reopen to be
  // safe. Then acquire a channel.
  CloseDevice();

  auto reacquire_channel = base::BindOnce(
      &ModemMbim::ReacquireChannel, weak_factory_.GetWeakPtr(), physical_slot);
  init_done_cb_ =
      base::BindOnce(&RunNextStep, std::move(reacquire_channel), std::move(cb));
  mbim_device_new(file_, /* cancellable */ NULL,
                  (GAsyncReadyCallback)MbimCreateNewDeviceCb, this);
}

void ModemMbim::StartProfileOp(uint32_t physical_slot, ResultCallback cb) {
  LOG(INFO) << __func__ << " physical_slot:" << physical_slot;
  retry_count_ = 0;
  is_ready_state_valid = false;
  StoreAndSetActiveSlot(physical_slot, std::move(cb));
}

void ModemMbim::FinishProfileOp(ResultCallback cb) {
  LOG(INFO) << __func__;
  const guint MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE = 7;
  if (!is_ready_state_valid ||
      !(ready_state_ == MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED ||
        ready_state_ == MBIM_SUBSCRIBER_READY_STATE_INITIALIZED ||
        ready_state_ == MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE)) {
    if (retry_count_ > kMaxRetries) {
      LOG(ERROR) << "Could not finish profile operation, ready_state_="
                 << ready_state_
                 << ", is_ready_state_valid=" << is_ready_state_valid;
      std::move(cb).Run(kModemMessageProcessingError);
      return;
    }
    retry_count_++;
    executor_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ModemMbim::FinishProfileOp, weak_factory_.GetWeakPtr(),
                       std::move(cb)),
        kSimRefreshDelay);
    return;
  }
  retry_count_ = 0;
  // Ideally we would acquire a channel to send notifications here. However,
  // acquiring a channel could cause MM to stop reporting the EID due to a fw
  // bug in L850. Thus we skip sending profile enable/disable notifications
  // until b/195589882, and b/202401139 are fixed.
  CloseDevice();
  std::move(cb).Run(kModemMessageProcessingError);
}

void ModemMbim::RestoreActiveSlot(ResultCallback cb) {
  LOG(INFO) << __func__;
  std::move(cb).Run(kModemSuccess);
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

}  // namespace hermes
