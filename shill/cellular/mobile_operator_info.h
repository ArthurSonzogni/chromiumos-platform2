// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MOBILE_OPERATOR_INFO_H_
#define SHILL_CELLULAR_MOBILE_OPERATOR_INFO_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/files/file_path.h>
#include <base/observer_list_types.h>

#include "shill/cellular/mobile_operator_mapper.h"
namespace shill {

class EventDispatcher;

// An MobileOperatorInfo object encapsulates the knowledge pertaining to all
// mobile operators. Typical usage consists of three steps:
//   - Initialize the object, set database file paths for the operator
//   information.
//   - Add observers to be notified whenever an M[V]NO has been determined / any
//   information about the M[V]NO changes.
//   - Send operator information updates to the object.
//
// So a class Foo that wants to use this object typically looks like:
//
// class Foo {
//   class OperatorObserver : public MobileOperatorInfoObserver {
//     // Implement all Observer functions.
//   }
//   ...
//
//   MobileOperatorInfo operator_info;
//   // Optional: Set a non-default database file.
//   operator_info.ClearDatabasePaths();
//   operator_info.AddDatabasePath(some_path);
//
//   operator_info.Init();  // Required.
//
//   OperatorObserver my_observer;
//   operator_info.AddObserver(my_observer);
//   ...
//   operator_info.UpdateIMSI(some_imsi);
//   operator_info.UpdateName(some_name);
//   ...
//   // Whenever enough information is available, |operator_info| notifies us
//   through |my_observer|.
// };
//

class MobileOperatorInfo {
 public:
  // |Init| must be called on the constructed object before it is used.
  // This object does not take ownership of dispatcher, and |dispatcher| is
  // expected to outlive this object.
  MobileOperatorInfo(EventDispatcher* dispatcher,
                     const std::string& info_owner);
  MobileOperatorInfo(const MobileOperatorInfo&) = delete;
  MobileOperatorInfo& operator=(const MobileOperatorInfo&) = delete;

  virtual ~MobileOperatorInfo();

  // These functions can be called before Init to read non default database
  // file(s). Files included earlier will take precedence over later additions.
  void ClearDatabasePaths();
  void AddDatabasePath(const base::FilePath& absolute_path);

  std::string GetLogPrefix(const char* func) const;
  bool Init();

  // Add/remove observers to subscribe to notifications.
  void AddObserver(MobileOperatorInfoObserver* observer);
  void RemoveObserver(MobileOperatorInfoObserver* observer);

  // ///////////////////////////////////////////////////////////////////////////
  // Functions to obtain information about the current mobile operator.
  // Any of these accessors can return an emtpy response if the information is
  // not available. Use |IsMobileNetworkOperatorKnown| and
  // |IsMobileVirtualNetworkOperatorKnown| to determine if a fix on the operator
  // has been made. Note that the information returned by the other accessors is
  // only valid when at least |IsMobileNetworkOperatorKnown| returns true. Their
  // values are undefined otherwise.

  // Query whether a mobile network operator has been successfully determined.
  virtual bool IsMobileNetworkOperatorKnown() const;
  // Query whether a mobile network operator has been successfully
  // determined.
  bool IsMobileVirtualNetworkOperatorKnown() const;

  // The unique identifier of this carrier. This is primarily used to
  // identify the user profile in store for each carrier. This identifier is
  // access technology agnostic.
  virtual const std::string& uuid() const;

  virtual const std::string& operator_name() const;
  virtual const std::string& country() const;
  virtual const std::string& mccmnc() const;
  virtual const std::string& gid1() const;

  // A given MVNO can be associated with multiple mcc/mnc pairs. A list of all
  // associated mcc/mnc pairs concatenated together.
  const std::vector<std::string>& mccmnc_list() const;
  // All localized names associated with this carrier entry.
  const std::vector<MobileOperatorMapper::LocalizedName>& operator_name_list()
      const;
  // All access point names associated with this carrier entry.
  virtual const std::vector<MobileOperatorMapper::MobileAPN>& apn_list() const;
  // All Online Payment Portal URLs associated with this carrier entry. There
  // are usually multiple OLPs based on access technology and it is up to the
  // application to use the appropriate one.
  virtual const std::vector<MobileOperatorMapper::OnlinePortal>& olp_list()
      const;

  // Some carriers are only available while roaming. This is mainly used by
  // Chrome.
  bool requires_roaming() const;
  // If specified, the MTU value to be used on the network interface.
  int32_t mtu() const;

  // ///////////////////////////////////////////////////////////////////////////
  // Functions used to notify this object of operator data changes.
  // The Update* methods update the corresponding property of the network
  // operator, and this value may be used to determine the M[V]NO.
  // These values are also the values reported through accessors, overriding any
  // information from the database.

  // Throw away all information provided to the object, and start from top.
  void Reset();

  virtual void UpdateMCCMNC(const std::string& mccmnc);

  virtual void UpdateIMSI(const std::string& imsi);
  void UpdateICCID(const std::string& iccid);
  virtual void UpdateOperatorName(const std::string& operator_name);
  void UpdateGID1(const std::string& gid1);
  void UpdateOnlinePortal(const std::string& url,
                          const std::string& method,
                          const std::string& post_data);
  void UpdateRequiresRoaming(const MobileOperatorInfo* serving_operator_info);

  // ///////////////////////////////////////////////////////////////////////////
  // Expose implementation for test purposes only.
  MobileOperatorMapper* impl() const { return impl_.get(); }

 private:
  std::unique_ptr<MobileOperatorMapper> impl_;
};

}  // namespace shill

#endif  // SHILL_CELLULAR_MOBILE_OPERATOR_INFO_H_
