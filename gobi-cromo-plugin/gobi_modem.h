// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GOBI_CROMO_PLUGIN_GOBI_MODEM_H_
#define GOBI_CROMO_PLUGIN_GOBI_MODEM_H_

#include <glib.h>
#include <pthread.h>
#include <stdint.h>

#include <map>
#include <set>
#include <string>

// TODO(ers) remove following #include once logging spew is resolved
#include <base/logging.h>
#include <base/macros.h>
#include <gtest/gtest_prod.h>  // For FRIEND_TEST
#include <cromo/cromo_server.h>
#include <cromo/dbus_adaptors/org.freedesktop.DBus.Properties.h>
#include <cromo/dbus_adaptors/org.freedesktop.ModemManager.Modem.h>
#include <cromo/dbus_adaptors/org.freedesktop.ModemManager.Modem.Simple.h>
#include <cromo/modem.h>
#include <cromo/utilities.h>
#include <metrics/metrics_library.h>
#include <mm/mm-modem.h>

#include "gobi-cromo-plugin/gobi_sdk_wrapper.h"
#include "gobi-cromo-plugin/metrics_stopwatch.h"
#include "gobi-cromo-plugin/modem_gobi_server_glue.h"

// TODO(rochberg)  Fix namespace pollution
#define METRIC_BASE_NAME "Network.3G.Gobi."

#define DEFINE_ERROR(name) extern const char* k##name##Error;
#define DEFINE_MM_ERROR(name, str) extern const char* kError##name;
#include "gobi-cromo-plugin/gobi_modem_errors.h"  // NOLINT(build/include_alpha)
#undef DEFINE_ERROR
#undef DEFINE_MM_ERROR


const char* QMIReturnCodeToMMError(unsigned int qmicode);
const char* QMICallFailureToMMError(unsigned int qmireason);
unsigned int QMIReasonToMMReason(unsigned int qmireason);

#define ENSURE_SDK_SUCCESS_WITH_RESULT(function, rc, errtype, result) \
    do {                                                   \
      if (rc != 0) {                                       \
        const char *errname = QMIReturnCodeToMMError(rc);  \
        if (errname != NULL)                               \
          error.set(errname, #function);                   \
        else                                               \
          error.set(errtype, #function);                   \
        LOG(WARNING) << #function << " failed : " << rc;   \
        return result;                                     \
      }                                                    \
    } while (0)                                            \

#define ENSURE_SDK_SUCCESS(function, rc, errtype)   \
        ENSURE_SDK_SUCCESS_WITH_RESULT(function, rc, errtype, )

// from mm-modem.h in ModemManager. This enum
// should move into an XML file to become part
// of the DBus API
typedef enum {
    MM_MODEM_STATE_UNKNOWN = 0,
    MM_MODEM_STATE_DISABLED = 10,
    MM_MODEM_STATE_DISABLING = 20,
    MM_MODEM_STATE_ENABLING = 30,
    MM_MODEM_STATE_ENABLED = 40,
    MM_MODEM_STATE_SEARCHING = 50,
    MM_MODEM_STATE_REGISTERED = 60,
    MM_MODEM_STATE_DISCONNECTING = 70,
    MM_MODEM_STATE_CONNECTING = 80,
    MM_MODEM_STATE_CONNECTED = 90,

    MM_MODEM_STATE_LAST = MM_MODEM_STATE_CONNECTED
} MMModemState;

static const size_t kDefaultBufferSize = 128;

class Carrier;
class CromoServer;
class GobiModem;
class GobiModemHandler;
class SessionStarter;
class InjectedFaults;
class GobiModemHelper;

class PendingEnable;

class ScopedGSource {
 public:
  ScopedGSource() : id_(0) {}
  ~ScopedGSource() { Remove(); }

  // Remove old timeout if exists and add a new one
  guint TimeoutAddFull(gint priority,
                       guint interval,
                       GSourceFunc function,
                       gpointer data,
                       GDestroyNotify notify) {
    Remove();
    id_ = g_timeout_add_seconds_full(
        priority, interval, function, data, notify);
    return id_;
  }

  void Remove(void) {
    if (id_) {
      g_source_remove(id_);
      id_ = 0;
    }
  }

 private:
  uint32_t id_;

  DISALLOW_COPY_AND_ASSIGN(ScopedGSource);
};

class GobiModem : public Modem,
                  public org::freedesktop::ModemManager::Modem_adaptor,
                  public org::freedesktop::ModemManager::Modem::Simple_adaptor,
                  public org::chromium::ModemManager::Modem::Gobi_adaptor,
                  public org::freedesktop::DBus::Properties_adaptor,
                  public DBus::IntrospectableAdaptor,
                  public DBus::PropertiesAdaptor,
                  public DBus::ObjectAdaptor {
 public:
  enum NetworkPreference {
    kNetworkPreferenceAutomatic = 0,
    kNetworkPreferenceCdma1xRtt = 1,
    kNetworkPreferenceCdmaEvdo = 2,
    kNetworkPreferenceGsm = 3,
    kNetworkPreferenceWcdma = 4
  };

  typedef std::map<ULONG, int> StrengthMap;

  GobiModem(DBus::Connection& connection,
            const DBus::Path& path,
            const gobi::DeviceElement &device,
            gobi::Sdk *sdk,
            GobiModemHelper *helper);
  virtual ~GobiModem();

  virtual void Init();

  int last_seen() { return last_seen_; }
  void set_last_seen(int scan_count) { last_seen_ = scan_count; }

  uint32_t mm_state() { return mm_state_; }
  void SetMmState(uint32_t new_state, uint32_t reason);

  static void set_handler(GobiModemHandler* handler) { handler_ = handler; }

  // Called by the SDK wrapper when it receives an error that requires
  // that the modem be reset.  Posts a main-thread callback that
  // power cycles the modem.
  static void SinkSdkError(const std::string& modem_path,
                           const std::string& sdk_function,
                           ULONG error);

  std::string GetUSBAddress();

  // Modem methods.
  virtual ModemAdaptor* modem_adaptor() {
    return static_cast<ModemAdaptor*>(this);
  }

  virtual SimpleAdaptor* simple_adaptor() {
    return static_cast<SimpleAdaptor*>(this);
  }

  virtual CdmaAdaptor* cdma_adaptor() {
    LOG(WARNING) << "Modem::cdma_adaptor() called on non-CDMA modem.";
    return NULL;
  }

  // DBUS Methods: Modem
  virtual void Enable(const bool& enable, DBus::Error& error);
  virtual void Connect(const std::string& number, DBus::Error& error);
  virtual void Disconnect(DBus::Error& error);
  virtual void FactoryReset(const std::string& number, DBus::Error& error);

  virtual ::DBus::Struct<uint32_t, uint32_t, uint32_t, uint32_t> GetIP4Config(
      DBus::Error& error);

  virtual ::DBus::Struct<std::string, std::string, std::string> GetInfo(
      DBus::Error& error);

  virtual void Reset(DBus::Error& error);

  // DBUS Methods: ModemSimple
  virtual void Connect(const utilities::DBusPropertyMap& properties,
                       DBus::Error& error);

  // Contract addition: GetStatus never fails, it simply does not set
  // properties it cannot determine.
  virtual utilities::DBusPropertyMap GetStatus(DBus::Error& error);

  // DBUS Methods: ModemGobi
  virtual void SetCarrier(const std::string& image, DBus::Error& error);
  virtual void SoftReset(DBus::Error& error);
  virtual void PowerCycle(DBus::Error& error);
  virtual void RequestEvents(const std::string& events, DBus::Error& error);
  virtual void SetAutomaticTracking(const bool& service_enable,
                                    const bool& port_enable,
                                    DBus::Error& error);
  virtual void InjectFault(const std::string& name,
                           const int32_t &value,
                           DBus::Error& error);
  virtual void SetNetworkPreference(const int32_t &value,
                                    DBus::Error& error);
  virtual void ForceModemActivatedStatus(DBus::Error& error);

  void ClearIdleCallbacks();

 protected:
  struct SerialNumbers {
    std::string esn;
    std::string imei;
    std::string meid;
  };

  static const int kNumStartDataSessionRetries;

  void ApiConnect(DBus::Error& error);
  ULONG ApiDisconnect(void);
  bool IsApiConnected() { return connected_modem_ != NULL; }
  virtual uint32_t CommonGetSignalQuality(DBus::Error& error);
  void GetSignalStrengthDbm(int& strength,
                            StrengthMap *interface_to_strength,
                            DBus::Error& error);

  // Read power state from the modem; update Enabled and mm_state_
  void GetPowerState();

  virtual void RegisterCallbacks();
  void ResetModem(DBus::Error& error);

  void GetSerialNumbers(SerialNumbers* out, DBus::Error &error);
  void LogGobiInformation();

  // TODO(cros-connectivity):  Move these to a utility class
  static unsigned long MapDbmToPercent(INT8 signal_strength_dbm);
  static unsigned long MapDataBearerToRfi(ULONG data_bearer_technology);

  // Send the return for the outstanding dbus call associated with *tag.
  void FinishDeferredCall(DBus::Tag *tag, const DBus::Error &error);

  // This does not use the callback args mechanism below.
  void SessionStarterDoneCallback(SessionStarter *starter);

  unsigned int QCStateToMMState(ULONG qcstate);

  struct CallbackArgs {
    CallbackArgs() : path(NULL) {}
    explicit CallbackArgs(DBus::Path* path) : path(path) {}
    ~CallbackArgs() { delete path; }
    DBus::Path* path;

   private:
    DISALLOW_COPY_AND_ASSIGN(CallbackArgs);
  };

  struct CallbackArgsWrapper {
    CallbackArgsWrapper() : callback(NULL), callback_id(0), args(NULL) {}
    ~CallbackArgsWrapper() { delete args; }
    GSourceFunc callback;
    guint callback_id;
    CallbackArgs* args;

   private:
    DISALLOW_COPY_AND_ASSIGN(CallbackArgsWrapper);
  };

  static void PostCallbackRequest(GSourceFunc callback,
                                  CallbackArgs* args) {
    pthread_mutex_lock(&modem_mutex_.mutex_);
    if (connected_modem_ && !connected_modem_->getting_deallocated_) {
      args->path = new DBus::Path(connected_modem_->path());
      CallbackArgsWrapper *args_wrapper = new CallbackArgsWrapper();
      args_wrapper->args = args;
      args_wrapper->callback = callback;
      args_wrapper->callback_id =
          g_idle_add(ExecuteCallbackRequest, args_wrapper);
      connected_modem_->idle_callback_ids_.insert(args_wrapper->callback_id);
    } else {
      delete args;
    }
    pthread_mutex_unlock(&modem_mutex_.mutex_);
  }

  static gboolean ExecuteCallbackRequest(gpointer data) {
    CallbackArgsWrapper *args_wrapper =
        reinterpret_cast<CallbackArgsWrapper*>(data);
    bool run_callback = false;
    pthread_mutex_lock(&modem_mutex_.mutex_);
    if (connected_modem_ && !connected_modem_->getting_deallocated_) {
      connected_modem_->idle_callback_ids_.erase(args_wrapper->callback_id);
      run_callback = true;
    }
    pthread_mutex_unlock(&modem_mutex_.mutex_);
    if (run_callback)
      args_wrapper->callback(args_wrapper->args);
    delete args_wrapper;
    return FALSE;
  }

  struct SessionStateArgs : public CallbackArgs {
    SessionStateArgs(ULONG state, ULONG session_end_reason)
        : state(state), session_end_reason(session_end_reason) {}
    ULONG state;
    ULONG session_end_reason;
  };

  static void SessionStateCallbackTrampoline(ULONG state,
                                             ULONG session_end_reason) {
    PostCallbackRequest(SessionStateCallback,
                        new SessionStateArgs(state, session_end_reason));
  }
  static gboolean SessionStateCallback(gpointer data);

  struct DataBearerTechnologyArgs : public CallbackArgs {
    explicit DataBearerTechnologyArgs(ULONG technology)
        : technology(technology) {}
    ULONG technology;
  };

  static void DataBearerCallbackTrampoline(ULONG technology) {
    // ignore the supplied argument
    PostCallbackRequest(DataBearerTechnologyCallback,
                        new DataBearerTechnologyArgs(technology));
  }
  static gboolean DataBearerTechnologyCallback(gpointer data);

  static void RoamingIndicatorCallbackTrampoline(ULONG roaming) {
    // ignore the supplied argument
    PostCallbackRequest(RegistrationStateCallback, new CallbackArgs());
  }
  static void PowerCallbackTrampoline(ULONG power_mode) {
    // ignore the supplied argument
    PostCallbackRequest(PowerCallback, new CallbackArgs());
  }
  static gboolean PowerCallback(gpointer data);

  static void RFInfoCallbackTrampoline(ULONG iface, ULONG bandclass,
                                       ULONG channel) {
    // ignore the supplied arguments
    PostCallbackRequest(RegistrationStateCallback, new CallbackArgs());
  }
  static gboolean RegistrationStateCallback(gpointer data);

  struct SignalStrengthArgs : public CallbackArgs {
    SignalStrengthArgs(INT8 signal_strength, ULONG radio_interface)
        : signal_strength(signal_strength), radio_interface(radio_interface) {}
    INT8 signal_strength;
    ULONG radio_interface;
  };

  static void SignalStrengthCallbackTrampoline(INT8 signal_strength,
                                               ULONG radio_interface) {
    PostCallbackRequest(SignalStrengthCallback,
                        new SignalStrengthArgs(signal_strength,
                                               radio_interface));
  }
  static gboolean SignalStrengthCallback(gpointer data);

  struct DormancyStatusArgs : public CallbackArgs {
    explicit DormancyStatusArgs(ULONG status) : status(status) {}
    ULONG status;
  };

  static void DormancyStatusCallbackTrampoline(ULONG status) {
    PostCallbackRequest(DormancyStatusCallback,
                        new DormancyStatusArgs(status));
  }

  static gboolean DormancyStatusCallback(gpointer data);

  struct DataCapabilitiesArgs : public CallbackArgs {
    DataCapabilitiesArgs(BYTE num_caps, BYTE* data) : num_data_caps(num_caps) {
      ULONG* dcp = reinterpret_cast<ULONG*>(data);
      if (num_data_caps > 12)
        num_data_caps = 12;

      for (int i = 0; i < num_data_caps; i++)
        data_caps[i] = dcp[i];
    }
    BYTE num_data_caps;
    // undocumented: the SDK limits the number of
    // data capabilities reported to 12
    ULONG data_caps[12];
  };

  static void DataCapabilitiesCallbackTrampoline(BYTE num_data_caps,
                                                 BYTE* data_caps) {
    PostCallbackRequest(DataCapabilitiesCallback,
                        new DataCapabilitiesArgs(num_data_caps, data_caps));
  }
  static gboolean DataCapabilitiesCallback(gpointer data);

  struct SdkErrorArgs : public CallbackArgs {
    explicit SdkErrorArgs(ULONG error) : error(error) {}
    ULONG error;
  };
  static gboolean SdkErrorHandler(gpointer data);

  // Set DBus-exported properties
  void SetDeviceProperties();
  void SetModemProperties();
  virtual void SetTechnologySpecificProperties() = 0;
  virtual void GetTechnologySpecificStatus(
      utilities::DBusPropertyMap* properties) = 0;
  virtual bool CheckEnableOk(DBus::Error &error) = 0;

  // Handlers for events delivered as callbacks by the SDK. These
  // all run in the main thread.
  virtual void RegistrationStateHandler() = 0;
  virtual void DataCapabilitiesHandler(BYTE num_data_caps,
                                       ULONG* data_caps) = 0;
  virtual void SignalStrengthHandler(INT8 signal_strength,
                                     ULONG radio_interface) = 0;

  virtual void SessionStateHandler(ULONG state, ULONG session_end_reason);
  virtual void DataBearerTechnologyHandler(ULONG technology);
  virtual void PowerModeHandler();

  static GobiModemHandler* handler_;
  // Wraps the Gobi SDK for dependency injection
  gobi::Sdk* sdk_;
  scoped_ptr<GobiModemHelper> modem_helper_;
  gobi::DeviceElement device_;
  int last_seen_;  // Updated every scan where the modem is present
  // Mirrors the DBus "State" property. This variable exists because
  // the DBus properties are essentially write-only.
  uint32_t mm_state_;

  ULONG session_id_;
  bool signal_available_;

  std::string sysfs_path_;

  struct mutex_wrapper_ {
    mutex_wrapper_() { pthread_mutex_init(&mutex_, NULL); }
    pthread_mutex_t mutex_;
  };
  static GobiModem* connected_modem_;
  static mutex_wrapper_ modem_mutex_;

  bool exiting_;
  bool device_resetting_;
  bool getting_deallocated_;

  std::set<guint> idle_callback_ids_;

  bool session_starter_in_flight_;
  scoped_ptr<PendingEnable> pending_enable_;

  ScopedGSource retry_disable_callback_source_;
  void RescheduleDisable();
  static void CleanupRetryDisableCallback(gpointer data);
  static gboolean RetryDisableCallback(gpointer data);
  bool EnableHelper(const bool& enable, DBus::Error& error,
                    const bool& user_initiated);
  void FinishEnable(const DBus::Error& error);
  void PerformDeferredDisable();

  static gobi::RegistrationState GetRegistrationState(gobi::Sdk* sdk);

  friend class GobiModemTest;

  // Return true if a data session is in the process of starting or
  // has started
  bool is_connecting_or_connected();

  // Call the Gobi API to stop a data session
  ULONG StopDataSession(ULONG session_id);

  // Force a disconnect of a data session, or stop the process of
  // starting a datasession
  ULONG ForceDisconnect();
  bool StartExit();
  bool ExitOk();

  // Resets the device by kicking it off the USB and allowing it back
  // on.  Reason is a QCWWAN error code for logging/metrics, or 0 for
  // 'externally initiated'
  void ExitAndResetDevice(ULONG reason);
  // Helper that compresses reason to a small int and mails to UMA
  void RecordResetReason(ULONG reason);

  bool CanMakeMethodCalls(void);

  std::string hooks_name_;

  friend bool StartExitTrampoline(void* arg);
  friend bool ExitOkTrampoline(void* arg);

  scoped_ptr<MetricsLibraryInterface> metrics_lib_;

  MetricsStopwatch disconnect_time_;
  MetricsStopwatch registration_time_;
  std::map<std::string, int32_t> injected_faults_;

  // This ought to go on the gobi modem XML interface, but dbus-c++ can't handle
  // enums, and all the ways of hacking around it seem worse than just using
  // strings and translating to the enum when we get the DBus call.
  enum {
    GOBI_EVENT_DORMANCY = 0,
    GOBI_EVENT_MAX
  };

  static int EventKeyToIndex(const char* key);
  void RequestEvent(const std::string req, DBus::Error& error);
  bool event_enabled[GOBI_EVENT_MAX];

  // Sets the Enabled property and emits a property changed signal.
  void SetEnabled(bool enabled);

  FRIEND_TEST(GobiModemTest, GetSignalStrengthDbmDisconnected);
  FRIEND_TEST(GobiModemTest, GetSignalStrengthDbmConnected);

  friend class SessionStarter;
  friend class PendingEnable;
  friend class ScopedApiConnection;
  friend class Gobi3KModemHelper;  // a bad idea, but necessary until
                                   // code is restructured

 private:
  DISALLOW_COPY_AND_ASSIGN(GobiModem);
};

class ScopedApiConnection {
 public:
  explicit ScopedApiConnection(GobiModem& modem)
      : modem_(modem), was_connected_(modem_.IsApiConnected()) {}

  ~ScopedApiConnection() {
    if (!was_connected_ && modem_.IsApiConnected())
      modem_.ApiDisconnect();
  }

  void ApiConnect(DBus::Error& error) {
    if (!was_connected_)
      modem_.ApiConnect(error);
  }

  // Force an immediate disconnect independent of prior state
  void ApiDisconnect() {
    // Prevent auto disconnect on destruction by faking we had been connected
    was_connected_ = true;
    modem_.ApiDisconnect();
  }

 private:
  GobiModem& modem_;
  bool was_connected_;

  DISALLOW_COPY_AND_ASSIGN(ScopedApiConnection);
};

class GobiModemHelper {
 public:
  explicit GobiModemHelper(gobi::Sdk* sdk) : sdk_(sdk) {}
  virtual ~GobiModemHelper() {}

  virtual void SetCarrier(GobiModem* modem,
                          GobiModemHandler* handler,
                          const std::string& carrier_name,
                          DBus::Error& error) = 0;

 protected:
  static const char kErrorUnknownCarrier[];

  gobi::Sdk* sdk_;

 private:
  DISALLOW_COPY_AND_ASSIGN(GobiModemHelper);
};

#endif  // GOBI_CROMO_PLUGIN_GOBI_MODEM_H_
