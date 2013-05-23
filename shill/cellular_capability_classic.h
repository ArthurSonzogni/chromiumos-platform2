// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_CAPABILITY_CLASSIC_H_
#define SHILL_CELLULAR_CAPABILITY_CLASSIC_H_

#include <string>
#include <vector>

#include <base/basictypes.h>
#include <base/callback.h>
#include <base/memory/scoped_ptr.h>
#include <base/memory/weak_ptr.h>
#include <base/gtest_prod_util.h>  // for FRIEND_TEST_ALL_PREFIXES
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/cellular.h"
#include "shill/cellular_capability.h"
#include "shill/dbus_properties.h"
#include "shill/modem_proxy_interface.h"
#include "shill/modem_simple_proxy_interface.h"

namespace shill {

class Cellular;
class Error;
class EventDispatcher;
class ModemGobiProxyInterface;
class ModemInfo;
class ProxyFactory;

enum ModemClassicState {
  kModemClassicStateUnknown = 0,
  kModemClassicStateDisabled = 10,
  kModemClassicStateDisabling = 20,
  kModemClassicStateEnabling = 30,
  kModemClassicStateEnabled = 40,
  kModemClassicStateSearching = 50,
  kModemClassicStateRegistered = 60,
  kModemClassicStateDisconnecting = 70,
  kModemClassicStateConnecting = 80,
  kModemClassicStateConnected = 90,
};

// CellularCapabilityClassic handles modems using the
// org.chromium.ModemManager DBUS interface.
class CellularCapabilityClassic : public CellularCapability {
 public:
  static const char kConnectPropertyApn[];
  static const char kConnectPropertyApnUsername[];
  static const char kConnectPropertyApnPassword[];
  static const char kConnectPropertyHomeOnly[];
  static const char kConnectPropertyPhoneNumber[];
  static const char kModemPropertyEnabled[];
  static const int kTimeoutSetCarrierMilliseconds;

  // |cellular| is the parent Cellular device.
  CellularCapabilityClassic(Cellular *cellular,
                            ProxyFactory *proxy_factory,
                            ModemInfo *modem_info);
  virtual ~CellularCapabilityClassic();

  virtual void StopModem(Error *error, const ResultCallback &callback);
  virtual void Connect(const DBusPropertiesMap &properties, Error *error,
                       const ResultCallback &callback);
  virtual void Disconnect(Error *error, const ResultCallback &callback);
  virtual void DisconnectCleanup();

  virtual void Activate(const std::string &carrier,
                        Error *error, const ResultCallback &callback);

  // Network registration.
  virtual void RegisterOnNetwork(const std::string &network_id,
                                 Error *error,
                                 const ResultCallback &callback);

  // PIN management. The default implementation fails by returning an error.
  virtual void RequirePIN(const std::string &pin, bool require,
                          Error *error, const ResultCallback &callback);
  virtual void EnterPIN(const std::string &pin,
                        Error *error, const ResultCallback &callback);
  virtual void UnblockPIN(const std::string &unblock_code,
                          const std::string &pin,
                          Error *error, const ResultCallback &callback);
  virtual void ChangePIN(const std::string &old_pin,
                         const std::string &new_pin,
                         Error *error, const ResultCallback &callback);

  virtual void SetCarrier(const std::string &carrier,
                          Error *error, const ResultCallback &callback);

  virtual void Scan(Error *error, const ResultCallback &callback);

  virtual void OnDBusPropertiesChanged(
      const std::string &interface,
      const DBusPropertiesMap &properties,
      const std::vector<std::string> &invalidated_properties);

 protected:
  typedef std::vector<base::Closure> CellularTaskList;

  virtual void GetRegistrationState() = 0;

  // The following five methods are only ever called as
  // callbacks (from the main loop), which is why they
  // don't take an Error * argument.
  virtual void EnableModem(const ResultCallback &callback);
  virtual void DisableModem(const ResultCallback &callback);
  virtual void GetModemStatus(const ResultCallback &callback);
  virtual void GetModemInfo(const ResultCallback &callback);
  virtual void GetProperties(const ResultCallback &callback) = 0;

  void FinishEnable(const ResultCallback &callback);
  void FinishDisable(const ResultCallback &callback);
  virtual void InitProxies();
  virtual void ReleaseProxies();
  virtual void UpdateStatus(const DBusPropertiesMap &properties) = 0;

  static void OnUnsupportedOperation(const char *operation, Error *error);

  // Runs the next task in a list.
  // Precondition: |tasks| is not empty.
  void RunNextStep(CellularTaskList *tasks);
  // StepCompletedCallback is called after a task completes.
  // |callback| is the original callback that needs to be invoked when all of
  // the tasks complete or if there is a failure.  |ignore_error| will be set
  // to true if the next task should be run regardless of the result of the
  // just-completed task.  |tasks| is the list of tasks remaining.  |error| is
  // the result of the just-completed task.
  void StepCompletedCallback(const ResultCallback &callback, bool ignore_error,
                             CellularTaskList *tasks, const Error &error);

  // Properties
  bool scanning_supported_;
  std::string meid_;
  std::string imsi_;
  std::string imei_;
  std::string esn_;
  std::string mdn_;
  std::string min_;
  std::string model_id_;
  std::string manufacturer_;
  std::string firmware_revision_;
  std::string hardware_revision_;
  std::string carrier_;

  scoped_ptr<ModemSimpleProxyInterface> simple_proxy_;

 private:
  friend class CellularTest;
  friend class CellularCapabilityCDMATest;
  friend class CellularCapabilityTest;
  friend class CellularCapabilityGSMTest;
  FRIEND_TEST(CellularCapabilityGSMTest, SetProxy);
  FRIEND_TEST(CellularCapabilityGSMTest, SetStorageIdentifier);
  FRIEND_TEST(CellularCapabilityGSMTest, UpdateStatus);
  FRIEND_TEST(CellularCapabilityTest, AllowRoaming);
  FRIEND_TEST(CellularCapabilityTest, EnableModemFail);
  FRIEND_TEST(CellularCapabilityTest, EnableModemSucceed);
  FRIEND_TEST(CellularCapabilityTest, FinishEnable);
  FRIEND_TEST(CellularCapabilityTest, GetModemInfo);
  FRIEND_TEST(CellularCapabilityTest, GetModemStatus);
  FRIEND_TEST(CellularCapabilityTest, TryApns);
  FRIEND_TEST(CellularServiceTest, FriendlyName);
  FRIEND_TEST(CellularTest, StartCDMARegister);
  FRIEND_TEST(CellularTest, StartConnected);
  FRIEND_TEST(CellularTest, StartGSMRegister);
  FRIEND_TEST(CellularTest, StartLinked);
  FRIEND_TEST(CellularTest, Connect);
  FRIEND_TEST_ALL_PREFIXES(CellularTest, ConnectAddsTerminationAction);
  FRIEND_TEST(CellularTest, ConnectFailure);
  FRIEND_TEST(CellularTest, ConnectFailureNoService);
  FRIEND_TEST(CellularTest, ConnectSuccessNoService);
  FRIEND_TEST(CellularTest, Disconnect);
  FRIEND_TEST(CellularTest, DisconnectFailure);
  FRIEND_TEST(CellularTest, DisconnectWithCallback);
  FRIEND_TEST(CellularTest, ModemStateChangeEnable);
  FRIEND_TEST(CellularTest, ModemStateChangeDisable);

  // Method reply and signal callbacks from Modem interface
  void OnModemStateChangedSignal(
      uint32 old_state, uint32 new_state, uint32 reason);
  void OnGetModemInfoReply(const ResultCallback &callback,
                           const ModemHardwareInfo &info,
                           const Error &error);

  // Method reply callbacks from Modem.Simple interface
  void OnGetModemStatusReply(const ResultCallback &callback,
                             const DBusPropertiesMap &props,
                             const Error &error);

  Cellular *cellular_;
  base::WeakPtrFactory<CellularCapabilityClassic> weak_ptr_factory_;

  scoped_ptr<ModemProxyInterface> proxy_;
  scoped_ptr<ModemGobiProxyInterface> gobi_proxy_;

  Strings supported_carriers_;

  DISALLOW_COPY_AND_ASSIGN(CellularCapabilityClassic);
};

}  // namespace shill

#endif  // SHILL_CELLULAR_CAPABILITY_CLASSIC_H_
