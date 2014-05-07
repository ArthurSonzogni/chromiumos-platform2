// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_SERVICE_H_
#define SHILL_CELLULAR_SERVICE_H_

#include <map>
#include <string>

#include <base/basictypes.h>
#include <base/time/time.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/cellular.h"
#include "shill/out_of_credits_detector.h"
#include "shill/refptr_types.h"
#include "shill/service.h"

namespace shill {

class ControlInterface;
class Error;
class EventDispatcher;
class Manager;
class OutOfCreditsDetector;

class CellularService : public Service {
 public:
  CellularService(ModemInfo *modem_info,
                  const CellularRefPtr &device);
  virtual ~CellularService();

  // Inherited from Service.
  virtual void AutoConnect();
  virtual void Connect(Error *error, const char *reason);
  virtual void Disconnect(Error *error);
  virtual void ActivateCellularModem(const std::string &carrier,
                                     Error *error,
                                     const ResultCallback &callback);
  virtual void CompleteCellularActivation(Error *error);
  virtual void SetState(ConnectState new_state);

  virtual std::string GetStorageIdentifier() const;
  void SetStorageIdentifier(const std::string &identifier);

  const CellularRefPtr &cellular() const { return cellular_; }

  void SetActivateOverNonCellularNetwork(bool state);
  bool activate_over_non_cellular_network() const {
    return activate_over_non_cellular_network_;
  }

  virtual void SetActivationState(const std::string &state);
  virtual const std::string &activation_state() const {
      return activation_state_;
  }

  void SetOLP(const std::string &url,
              const std::string &method,
              const std::string &post_data);
  const Stringmap &olp() const { return olp_; }

  void SetUsageURL(const std::string &url);
  const std::string &usage_url() const { return usage_url_; }

  void SetServingOperator(const Cellular::Operator &oper);
  const Cellular::Operator &serving_operator() const;

  // Sets network technology to |technology| and broadcasts the property change.
  void SetNetworkTechnology(const std::string &technology);
  const std::string &network_technology() const { return network_technology_; }

  // Sets roaming state to |state| and broadcasts the property change.
  void SetRoamingState(const std::string &state);
  const std::string &roaming_state() const { return roaming_state_; }

  bool is_auto_connecting() const {
    return is_auto_connecting_;
  }

  const std::string &ppp_username() const { return ppp_username_; }
  const std::string &ppp_password() const { return ppp_password_; }

  virtual const base::Time &resume_start_time() const {
    return resume_start_time_;
  }

  OutOfCreditsDetector *out_of_credits_detector() {
    return out_of_credits_detector_.get();
  }
  void SignalOutOfCreditsChanged(bool state) const;

  // Overrides Load and Save from parent Service class.  We will call
  // the parent method.
  virtual bool Load(StoreInterface *storage);
  virtual bool Save(StoreInterface *storage);

  Stringmap *GetUserSpecifiedApn();
  Stringmap *GetLastGoodApn();
  virtual void SetLastGoodApn(const Stringmap &apn_info);
  virtual void ClearLastGoodApn();

  virtual void OnAfterResume();

  // Initialize out-of-credits detection.
  void InitOutOfCreditsDetection(OutOfCreditsDetector::OOCType ooc_type);

 protected:
  // Overrides IsAutoConnectable from parent Service class.
  virtual bool IsAutoConnectable(const char **reason) const;

 private:
  friend class CellularCapabilityUniversalTest;
  friend class CellularServiceTest;
  FRIEND_TEST(CellularCapabilityGSMTest, SetupApnTryList);
  FRIEND_TEST(CellularCapabilityTest, TryApns);
  FRIEND_TEST(CellularCapabilityUniversalMainTest,
              UpdatePendingActivationState);
  FRIEND_TEST(CellularCapabilityUniversalMainTest, UpdateServiceName);
  FRIEND_TEST(CellularTest, Connect);
  FRIEND_TEST(CellularTest, GetLogin);  // ppp_username_, ppp_password_
  FRIEND_TEST(CellularTest, OnConnectionHealthCheckerResult);
  FRIEND_TEST(CellularServiceTest, SetApn);
  FRIEND_TEST(CellularServiceTest, ClearApn);
  FRIEND_TEST(CellularServiceTest, LastGoodApn);
  FRIEND_TEST(CellularServiceTest, LoadResetsPPPAuthFailure);
  FRIEND_TEST(CellularServiceTest, IsAutoConnectable);
  FRIEND_TEST(CellularServiceTest, OutOfCreditsDetected);
  FRIEND_TEST(CellularServiceTest,
              OutOfCreditsDetectionNotSkippedAfterSlowResume);
  FRIEND_TEST(CellularServiceTest, OutOfCreditsDetectionSkippedAfterResume);
  FRIEND_TEST(CellularServiceTest,
              OutOfCreditsDetectionSkippedAlreadyOutOfCredits);
  FRIEND_TEST(CellularServiceTest,
              OutOfCreditsDetectionSkippedExplicitDisconnect);
  FRIEND_TEST(CellularServiceTest, OutOfCreditsNotDetectedConnectionNotDropped);
  FRIEND_TEST(CellularServiceTest, OutOfCreditsNotDetectedIntermittentNetwork);
  FRIEND_TEST(CellularServiceTest, OutOfCreditsNotEnforced);
  FRIEND_TEST(CellularServiceTest, CustomSetterNoopChange);

  static const char kAutoConnActivating[];
  static const char kAutoConnBadPPPCredentials[];
  static const char kAutoConnDeviceDisabled[];
  static const char kAutoConnOutOfCredits[];
  static const char kAutoConnOutOfCreditsDetectionInProgress[];
  static const char kStoragePPPUsername[];
  static const char kStoragePPPPassword[];

  void HelpRegisterDerivedStringmap(
      const std::string &name,
      Stringmap(CellularService::*get)(Error *error),
      bool(CellularService::*set)(const Stringmap &value, Error *error));
  void HelpRegisterDerivedBool(
      const std::string &name,
      bool(CellularService::*get)(Error *),
      bool(CellularService::*set)(const bool&, Error *));

  virtual std::string GetDeviceRpcId(Error *error) const;

  Stringmap GetApn(Error *error);
  bool SetApn(const Stringmap &value, Error *error);
  static void SaveApn(StoreInterface *storage,
                      const std::string &storage_group,
                      const Stringmap *apn_info,
                      const std::string &keytag);
  static void SaveApnField(StoreInterface *storage,
                           const std::string &storage_group,
                           const Stringmap *apn_info,
                           const std::string &keytag,
                           const std::string &apntag);
  static void LoadApn(StoreInterface *storage,
                      const std::string &storage_group,
                      const std::string &keytag,
                      Stringmap *apn_info);
  static bool LoadApnField(StoreInterface *storage,
                           const std::string &storage_group,
                           const std::string &keytag,
                           const std::string &apntag,
                           Stringmap *apn_info);
  bool IsOutOfCredits(Error */*error*/);

  // For unit test.
  void set_out_of_credits_detector(OutOfCreditsDetector *detector);

  base::WeakPtrFactory<CellularService> weak_ptr_factory_;

  // Properties
  bool activate_over_non_cellular_network_;
  std::string activation_state_;
  Cellular::Operator serving_operator_;
  std::string network_technology_;
  std::string roaming_state_;
  Stringmap olp_;
  std::string usage_url_;
  Stringmap apn_info_;
  Stringmap last_good_apn_info_;
  std::string ppp_username_;
  std::string ppp_password_;

  std::string storage_identifier_;

  CellularRefPtr cellular_;

  // Flag indicating that a connect request is an auto-connect request.
  // Note: Since Connect() is asynchronous, this flag is only set during the
  // call to Connect().  It does not remain set while the async request is
  // in flight.
  bool is_auto_connecting_;
  // Time when the last resume occurred.
  base::Time resume_start_time_;
  // Out-of-credits detector.
  scoped_ptr<OutOfCreditsDetector> out_of_credits_detector_;

  DISALLOW_COPY_AND_ASSIGN(CellularService);
};

}  // namespace shill

#endif  // SHILL_CELLULAR_SERVICE_H_
