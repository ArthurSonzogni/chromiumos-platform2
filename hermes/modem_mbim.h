// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_MODEM_MBIM_H_
#define HERMES_MODEM_MBIM_H_

#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <glib-bridge/glib_bridge.h>
#include <glib-bridge/glib_logger.h>
#include <glib-bridge/glib_scopers.h>
#include <google-lpa/lpa/card/euicc_card.h>
#include <libmbim-glib/libmbim-glib.h>
#include <libmbim-glib/mbim-enums.h>
#include "hermes/euicc_interface.h"
#include "hermes/executor.h"
#include "hermes/logger.h"
#include "hermes/mbim_cmd.h"
#include "hermes/modem.h"
#include "hermes/modem_control_interface.h"
#include "hermes/modem_manager_proxy.h"
#include "hermes/socket_interface.h"

namespace hermes {
// Implementation of EuiccCard using MBIM
// messages.
class ModemMbim : public Modem<MbimCmd> {
 public:
  static std::unique_ptr<ModemMbim> Create(
      Logger* logger,
      Executor* executor,
      std::unique_ptr<ModemManagerProxy> modem_manager_proxy);
  virtual ~ModemMbim();
  // EuiccInterface overrides
  void Initialize(EuiccManagerInterface* euicc_manager,
                  ResultCallback cb) override;
  void StoreAndSetActiveSlot(const uint32_t physical_slot,
                             ResultCallback cb) override;
  void ProcessEuiccEvent(EuiccEvent event, ResultCallback cb) override;
  void RestoreActiveSlot(ResultCallback cb) override;
  bool IsSimValidAfterEnable() override;
  bool IsSimValidAfterDisable() override;

  static bool ParseEidApduResponseForTesting(const MbimMessage* response,
                                             std::string* eid);

 private:
  ModemMbim(Logger* logger,
            Executor* executor,
            std::unique_ptr<ModemManagerProxy> modem_manager_proxy);
  void OnModemAvailable();
  void Shutdown() override;
  void TransmitFromQueue() override;
  std::unique_ptr<MbimCmd> GetTagForSendApdu() override;
  void ProcessMbimResult(int err);
  static void MbimCreateNewDeviceCb(GObject* source,
                                    GAsyncResult* res,
                                    ModemMbim* modem_mbim);
  static void MbimDeviceOpenReadyCb(MbimDevice* dev,
                                    GAsyncResult* res,
                                    ModemMbim* modem_mbim);
  void TransmitSubscriberReadyStatusQuery();
  void TransmitMbimLoadCurrentCapabilities();
  void TransmitMbimCloseChannel();
  void TransmitMbimOpenLogicalChannel();
  void TransmitMbimSendEidApdu();
  void TransmitMbimSendApdu(TxElement* tx_element);
  void QueryCurrentMbimCapabilities(ResultCallback cb);
  void CloseChannel(base::OnceCallback<void(int)> cb);
  void AcquireChannel(base::OnceCallback<void(int)> cb);
  void ReacquireChannel(const uint32_t physical_slot, ResultCallback cb);
  void GetEidFromSim(ResultCallback cb);
  void GetEidStepCloseChannel(ResultCallback cb);
  void GetEidStepOpenChannel(ResultCallback cb);

  static void SubscriberReadyStatusRspCb(MbimDevice* device,
                                         GAsyncResult* res,
                                         ModemMbim* modem_mbim);

  static void DeviceCapsQueryReady(MbimDevice* device,
                                   GAsyncResult* res,
                                   ModemMbim* modem_mbim);

  static void UiccLowLevelAccessCloseChannelSetCb(MbimDevice* device,
                                                  GAsyncResult* res,
                                                  ModemMbim* modem_mbim);

  static void UiccLowLevelAccessOpenChannelSetCb(MbimDevice* device,
                                                 GAsyncResult* res,
                                                 ModemMbim* modem_mbim);

  static void UiccLowLevelAccessApduEidParse(MbimDevice* device,
                                             GAsyncResult* res,
                                             ModemMbim* modem_mbim);
  static bool ParseEidApduResponse(const MbimMessage* response,
                                   std::string* eid);

  static void UiccLowLevelAccessApduResponseParse(MbimDevice* device,
                                                  GAsyncResult* res,
                                                  ModemMbim* modem_mbim);

  static void ClientIndicationCb(MbimDevice* device,
                                 MbimMessage* notification,
                                 ModemMbim* modem_mbim);

  void CloseDevice();

  void CloseDeviceAndUninhibit(ResultCallback cb);
  ///////////////////
  // State Diagram //
  ///////////////////
  //
  //       Uninitialized --------------------------------> InitializeStarted
  //                              Initialize()
  //       InitializeStarted ----------------------------> MbimStarted
  //                              GetEidFromSim()

  class State {
   public:
    enum Value : uint8_t {
      kMbimUninitialized,
      kMbimInitializeStarted,
      kMbimStarted,
    };

    State() : value_(kMbimUninitialized) {}
    // Transitions to the indicated state. Returns whether or not the
    // transition was successful.
    bool Transition(Value value);

    bool operator==(Value value) const { return value_ == value; }
    bool operator!=(Value value) const { return value_ != value; }
    friend std::ostream& operator<<(std::ostream& os, const State state) {
      switch (state.value_) {
        case kMbimUninitialized:
          os << "Uninitialized";
          break;
        case kMbimInitializeStarted:
          os << "InitializeStarted";
          break;
        case kMbimStarted:
          os << "MbimStarted";
          break;
      }
      return os;
    }

   private:
    explicit State(Value value) : value_(value) {}
    Value value_;
  };
  State current_state_;
  ResultCallback init_done_cb_;
  std::string eid_;
  guint32 channel_;
  glib_bridge::ScopedGObject<MbimDevice> device_;
  uint8_t indication_id_;
  bool pending_response_;
  MbimSubscriberReadyState ready_state_;
  GFile* file_ = NULL;
  bool is_ready_state_valid;

  base::CancelableOnceClosure scheduled_uninhibit_;

  base::WeakPtrFactory<ModemMbim> weak_factory_;
  void AcquireChannelAfterCardReady(EuiccEvent event, ResultCallback cb);
};

}  // namespace hermes

#endif  // HERMES_MODEM_MBIM_H_
