// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CDMA specialization of Gobi Modem

#ifndef PLUGIN_GOBI_CDMA_MODEM_H_
#define PLUGIN_GOBI_CDMA_MODEM_H_

#include "gobi_modem.h"
#include <base/files/file_path.h>
#include <cromo/dbus_adaptors/org.freedesktop.ModemManager.Modem.Cdma.h>

class GobiModem;

class GobiCdmaModem
    : public GobiModem,
      public org::freedesktop::ModemManager::Modem::Cdma_adaptor {
 public:

  GobiCdmaModem(DBus::Connection& connection,
                const DBus::Path& path,
                const gobi::DeviceElement& device,
                gobi::Sdk *sdk,
                GobiModemHelper *modem_helper);
  virtual ~GobiCdmaModem();

  virtual void Init();

  // Modem methods
  virtual CdmaAdaptor *cdma_adaptor() {
    return static_cast<CdmaAdaptor*>(this);
  }

  // DBUS Methods: Modem.CDMA
  virtual uint32_t GetSignalQuality(DBus::Error& error);
  virtual std::string GetEsn(DBus::Error& error);
  virtual DBus::Struct<uint32_t, std::string, uint32_t> GetServingSystem(
      DBus::Error& error);
  virtual void GetRegistrationState(
      uint32_t& cdma_1x_state, uint32_t& evdo_state, DBus::Error& error);
  virtual uint32_t Activate(const std::string& carrier_name,
                            DBus::Error& error);
  virtual void ActivateManual(const utilities::DBusPropertyMap& properties,
                              DBus::Error& error);
  virtual void ActivateManualDebug(
      const std::map<std::string, std::string>& properties,
      DBus::Error &error);

  // DBUS Methods: ModemSimple
  virtual void Connect(const utilities::DBusPropertyMap& properties,
                       DBus::Error& error);

  // DBUS Methods: ModemGobi
  virtual void ForceModemActivatedStatus(DBus::Error& error);

 protected:
  void GetCdmaRegistrationState(ULONG* cdma_1x_state, ULONG* cdma_evdo_state,
                                ULONG* roaming_state, DBus::Error& error);

  // Returns the modem activation state as an enum value from
  // MM_MODEM_CDMA_ACTIVATION_STATE_..., or < 0 for error.  This state
  // may be further overridden by ModifyModemStatusReturn()
  int32_t GetMmActivationState();

  // Overrides and extensions of GobiModem functions
  virtual void RegisterCallbacks();
  virtual void RegistrationStateHandler();
  virtual void DataCapabilitiesHandler(BYTE num_data_caps,
                                       ULONG* data_caps);
  virtual void SignalStrengthHandler(INT8 signal_strength,
                                     ULONG radio_interface);
  virtual void SetTechnologySpecificProperties();
  virtual void GetTechnologySpecificStatus(
      utilities::DBusPropertyMap* properties);
  virtual bool CheckEnableOk(DBus::Error &error);


  // ======================================================================

  struct ActivationStatusArgs : public CallbackArgs {
    ActivationStatusArgs(ULONG device_activation_state)
      : device_activation_state(device_activation_state) { }
    ULONG device_activation_state;
  };

  static void ActivationStatusCallbackTrampoline(ULONG activation_state) {
      PostCallbackRequest(ActivationStatusCallback,
                          new ActivationStatusArgs(activation_state));
  }
  static gboolean ActivationStatusCallback(gpointer data);

  struct OmadmStateArgs : public CallbackArgs {
    OmadmStateArgs(ULONG session_state,
                   ULONG failure_reason)
      : session_state(session_state),
        failure_reason(failure_reason) { }
    ULONG session_state;
    ULONG failure_reason;
  };
  static void OmadmStateDeviceConfigureCallbackTrampoline(
      ULONG session_state, ULONG failure_reason) {
    PostCallbackRequest(OmadmStateDeviceConfigureCallback,
                        new OmadmStateArgs(session_state,
                                           failure_reason));
  }
  static gboolean OmadmStateDeviceConfigureCallback(gpointer data);
  static void OmadmStateClientPrlUpdateCallbackTrampoline(
      ULONG session_state, ULONG failure_reason) {
    PostCallbackRequest(OmadmStateClientPrlUpdateCallback,
                        new OmadmStateArgs(session_state,
                                           failure_reason));
  }
  static gboolean OmadmStateClientPrlUpdateCallback(gpointer data);

  // ======================================================================

  // Computes arguments for an ActivationStateChanged signal and sends
  // it.  Overrides MM_MODEM_CDMA_ACTIVATION_ERROR_TIMED_OUT if device
  // is in fact activated.
  void SendActivationStateChanged(uint32_t mm_activation_error);
  // Helper that sends failure
  void SendActivationStateFailed();

  MetricsStopwatch activation_time_;
  bool activation_in_progress_;
  bool force_activated_status_;

  // Returns a enum value from MM_MODEM_CDMA_ACTIVATION_ERROR
  uint32_t ActivateOmadm();
  // Returns a enum value from MM_MODEM_CDMA_ACTIVATION_ERROR
  uint32_t ActivateOtasp(const std::string& number);
  // Perform actions necessary when activation as finished
  void ActivationFinished(void);
  // Performs actions necessary when a modem is seen for the first time after
  // it was activated and resetted.
  void PerformPostActivationSteps();
  // Starts a client initiated PRL update request.
  void StartClientInitiatedPrlUpdate();

 private:
  // Returns the path of the cookie crumb file used to indicate that post
  // activation steps should be executed.
  base::FilePath GetExecPostActivationStepsCookieCrumbPath() const;
  // Writes a cookie crumb that signals post activation steps should be
  // executed next time the modem appears to the system.
  void MarkForExecPostActivationStepsAfterReset();
  // Checks to see if post activation steps should be executed.
  bool ShouldExecPostActivationSteps() const;

  DISALLOW_COPY_AND_ASSIGN(GobiCdmaModem);
};


#endif  // PLUGIN_GOBI_CDMA_MODEM_H_
