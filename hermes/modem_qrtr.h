// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_MODEM_QRTR_H_
#define HERMES_MODEM_QRTR_H_

#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <google-lpa/lpa/card/euicc_card.h>
#include <libqrtr.h>

#include "hermes/dms_cmd.h"
#include "hermes/executor.h"
#include "hermes/logger.h"
#include "hermes/modem_control_interface.h"
#include "hermes/socket_qrtr.h"
#include "hermes/uim_cmd.h"

namespace hermes {

class EuiccManagerInterface;

// Implementation of EuiccCard using QRTR sockets to send QMI UIM
// messages.
class ModemQrtr : public lpa::card::EuiccCard, public ModemControlInterface {
 public:
  // Base class for the tx info specific to a certain type of uim command.
  // Uim command types that need any additional information should define
  // a child class. An instance of that class should be set to the data pointer
  // in its corresponding TxElement.
  struct TxInfo {
    virtual ~TxInfo() = default;
  };

  using ResponseCallback =
      std::function<void(std::vector<std::vector<uint8_t>>&
                             responses,  // NOLINT(runtime/references)
                         int err)>;

  static std::unique_ptr<ModemQrtr> Create(
      std::unique_ptr<SocketInterface> socket,
      Logger* logger,
      Executor* executor);
  virtual ~ModemQrtr();

  void Initialize(EuiccManagerInterface* euicc_manager);

  // ModemControlInterface overrides
  void StoreAndSetActiveSlot(uint32_t physical_slot) override;
  void RestoreActiveSlot() override;
  void StartProfileOp(uint32_t physical_slot) override;
  void FinishProfileOp() override;

  // lpa::card::EuiccCard overrides.
  void SendApdus(std::vector<lpa::card::Apdu> apdus,
                 ResponseCallback cb) override;
  bool IsSimValidAfterEnable() override;
  bool IsSimValidAfterDisable() override;
  std::string GetImei() override { return imei_; };
  lpa::util::EuiccLog* logger() override { return logger_; }

 private:
  // Delay between SwitchSlot and the next QMI message. Allows the modem to
  // power on the new slot, and for the eUICC to boot. If this delay is
  // insufficient, we retry after kInitRetryDelay
  static constexpr auto kSwitchSlotDelay = base::TimeDelta::FromSeconds(3);

  struct TxElement {
    std::unique_ptr<TxInfo> info_;
    uint16_t id_;
    std::unique_ptr<QmiCmdInterface> qmi_msg_;
  };

  ModemQrtr(std::unique_ptr<SocketInterface> socket,
            Logger* logger,
            Executor* executor);
  void InitializeUim();
  void RetryInitialization();
  void Shutdown();
  uint16_t AllocateId();

  // Top-level method to transmit an element from the tx queue. Dispatches to
  // the proper Transmit*CmdFromQueue method based on the service being
  // transmitted to.
  void TransmitFromQueue();
  // Transmit*CmdFromQueue methods perform QMI encoding prior to sending
  // data to the socket. Will remove elements from the tx queue as needed.
  void TransmitDmsCmdFromQueue();
  void TransmitUimCmdFromQueue();
  // Creates and sends a SWITCH_SLOT QMI request
  void TransmitQmiSwitchSlot(TxElement* tx_element);
  // Creates and sends OPEN_LOGICAL_CHANNEL QMI request.
  void TransmitQmiOpenLogicalChannel(TxElement* tx_element);
  // Creates and sends SEND_APDU QMI request.
  void TransmitQmiSendApdu(TxElement* tx_element);
  // Performs QMI encoding and sends data to the QRTR socket.
  bool SendCommand(QmiCmdInterface* qmi_command,
                   uint16_t id,
                   void* c_struct,
                   qmi_elem_info* ei);

  // Top-level method when a packet is read from the socket into |buffer_|. Will
  // perform proper processing based on QRTR packet type. Attempts to transmit
  // the next element in the tx queue when complete.
  void ProcessQrtrPacket(uint32_t node, uint32_t port, int size);
  // Dispatches to proper ReceiveQmi* method based on QMI type.
  void ProcessQmiPacket(const qrtr_packet& packet);
  // Performs decoding for UIM RESET QMI response.
  void ReceiveQmiReset(const qrtr_packet& packet);
  // Performs decoding for SWITCH_SLOT QMI response.
  void ReceiveQmiSwitchSlot(const qrtr_packet& packet);
  // Performs decoding for GET_SLOTS QMI response.
  void ReceiveQmiGetSlots(const qrtr_packet& packet);
  // Performs decoding for OPEN_LOGICAL_CHANNEL QMI response.
  void ReceiveQmiOpenLogicalChannel(const qrtr_packet& packet);
  void ParseQmiOpenLogicalChannel(const qrtr_packet& packet);
  // Performs decoding for SEND_APDU response and calls |on_recv_| with
  // appropriate parameters.
  void ReceiveQmiSendApdu(const qrtr_packet& packet);
  // Performs decoding of GET_DEVICE_SERIAL_NUMBERS response. Parses the IMEI.
  void ReceiveQmiGetSerialNumbers(const qrtr_packet& packet);
  void DisableQmi(base::TimeDelta duration);
  void EnableQmi();

  void OnDataAvailable(SocketInterface* socket);

  // lpa::card::EuiccCard overrides.
  const lpa::proto::EuiccSpecVersion& GetCardVersion() override;
  lpa::util::Executor* executor() override { return executor_; }

  // Set the active slot to a euicc so that a channel can be established and
  // profiles can be installed.
  void SetActiveSlot(uint32_t physical_slot);

  // Request that the Euicc does not send intermediate procedure bytes.
  // Useful in eliminating race between card refresh and profile enable response
  // b/169954635
  enum class ProcedureBytesMode : uint8_t {
    EnableIntermediateBytes = 0,
    DisableIntermediateBytes = 1
  };
  void SetProcedureBytes(ProcedureBytesMode procedure_bytes_mode);
  void ReacquireChannel();

  friend class ModemQrtrTest;

  ///////////////////
  // State Diagram //
  ///////////////////
  //
  //       [Start state]
  //     +---------------+  (FinalizeInitialization() called w/failure)
  //     | Uninitialized | <--------------------------------------------------+
  //     +---------------+                                                    |
  //             +                                                            |
  //             | (Initialize() called)                                      |
  //             |                                                            |
  //             V                                                            |
  //    +-------------------+     +------------+     +------------+           |
  //    | InitializeStarted | +-> | DmsStarted | +-> | UimStarted | +---+     |
  //    +-------------------+     +------------+     +------------+     |     |
  //                                                                    |     |
  //              +-----------------------------------------------------+     |
  //              |                                                           |
  //              V                                                           |
  //   +-----------------------+     +----------------------+                 |
  //   | LogicalChannelPending | +-> | LogicalChannelOpened | +---------------+
  //   +-----------------------+     +----------------------+                 |
  //                                                                          |
  //             +------------------------------------------------------------+
  //             |     (FinalizeInitialization() called w/success)
  //             V
  //         +---------------+
  //         | SendApduReady |
  //         +---------------+
  class State {
   public:
    enum Value : uint8_t {
      kUninitialized,
      kInitializeStarted,
      kDmsStarted,
      kUimStarted,
      kLogicalChannelPending,
      kLogicalChannelOpened,
      kSendApduReady,
    };

    State() : value_(kUninitialized) {}
    // Transitions to the indicated state. Returns whether or not the transition
    // was successful.
    bool Transition(Value value);

    bool IsInitialized() const { return value_ == kSendApduReady; }
    // Returns whether or not some QMI packet can be sent out in the state. Note
    // that APDUs in particular may only be sent in the kSendApduReady state.
    bool CanSend() const {
      return value_ == kDmsStarted || value_ == kUimStarted ||
             value_ == kSendApduReady;
    }

    bool operator==(Value value) const { return value_ == value; }
    bool operator!=(Value value) const { return value_ != value; }
    friend std::ostream& operator<<(std::ostream& os, const State state) {
      os << state.value_;
      return os;
    }

   private:
    explicit State(Value value) : value_(value) {}

    Value value_;
  };

  State current_state_;
  bool qmi_disabled_;
  base::RepeatingClosure retry_initialization_callback_;
  int retry_count_;
  // Indicates that a qmi message has been sent and that a response is expected
  // Set for all known message types except QMI_RESET
  std::unique_ptr<QmiCmdInterface> pending_response_type_;

  bool extended_apdu_supported_;  // There is no plan to support these.
  uint16_t current_transaction_id_;

  // Logical Channel that will be used to communicate with the chip, returned
  // from OPEN_LOGICAL_CHANNEL request sent once the QRTR socket has been
  // opened.
  uint8_t channel_;
  // The slot that the logical channel to the eSIM will be made. Initialized in
  // constructor, hardware specific.
  uint8_t logical_slot_;
  // Store the previous active slot before a switch slot
  base::Optional<uint32_t> stored_active_slot_;

  // Ask SendApdu commands to send final result and status words only.
  // If set, intermediate procedure bytes are not sent by the Euicc.
  ProcedureBytesMode procedure_bytes_mode_;

  std::unique_ptr<SocketInterface> socket_;

  // A bimap of {node,port} <-> Service .
  // Stores information similar to output of qrtr-lookup
  class QrtrTable {
    std::unordered_map<QmiCmdInterface::Service, SocketQrtr::PacketMetadata>
        qrtr_metadata_;
    std::unordered_map<SocketQrtr::PacketMetadata, QmiCmdInterface::Service>
        service_from_metadata_;

   public:
    bool ContainsService(QmiCmdInterface::Service service);
    void Insert(QmiCmdInterface::Service service,
                SocketQrtr::PacketMetadata metadata);
    void clear();
    const SocketQrtr::PacketMetadata& GetMetadata(
        QmiCmdInterface::Service service);
    const QmiCmdInterface::Service& GetService(
        SocketQrtr::PacketMetadata metadata);
  };
  QrtrTable qrtr_table_;

  std::string imei_;

  // Buffer for storing data from the QRTR socket
  std::vector<uint8_t> buffer_;
  // List of responses for the oldest SendApdus call that hasn't been completely
  // processed.
  std::vector<std::vector<uint8_t>> responses_;
  // Queue of packets to send to the modem
  std::deque<TxElement> tx_queue_;

  std::map<std::pair<QmiCmdInterface::Service, uint16_t>,
           base::Callback<void(const qrtr_packet&)>>
      qmi_rx_callbacks_;

  // Used to send notifications about eSIM slot changes.
  EuiccManagerInterface* euicc_manager_;

  Logger* logger_;
  Executor* executor_;
  lpa::proto::EuiccSpecVersion spec_version_;
};

}  // namespace hermes

#endif  // HERMES_MODEM_QRTR_H_
