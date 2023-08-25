// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CECSERVICE_CEC_MANAGER_H_
#define CECSERVICE_CEC_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/dbus/service_constants.h>

#include "cecservice/cec_device.h"
#include "cecservice/udev.h"

namespace cecservice {

// Main service object that maintains list of /dev/cec* nodes (with a help of
// udev) and passes received commands to CEC devices.
class CecManager {
 public:
  using GetTvsPowerStatusCallback =
      base::OnceCallback<void(const std::vector<TvPowerStatus>&)>;
  using PowerChangeSentCallback = base::OnceCallback<void()>;

  CecManager(const UdevFactory& udev_factory,
             const CecDeviceFactory& cec_factory);
  CecManager(const CecManager&) = delete;
  CecManager& operator=(const CecManager&) = delete;

  ~CecManager();

  // Queries power status of CEC-enabled TVs (devices with logical address 0).
  // The order of returned values is arbitrary.
  void GetTvsPowerStatus(GetTvsPowerStatusCallback callback);

  // Sends wake up (image view on + active source) request to all CEC-enabled
  // TVs.
  void SetWakeUp(PowerChangeSentCallback callback);

  // Passes stand by command to all CEC-enabled TVs.
  void SetStandBy(PowerChangeSentCallback callback);

 private:
  // Ids for get tv power queries.
  typedef unsigned QueryId;

  // Represents a power status query result from a single CEC device.
  struct TvPowerStatusResult;
  // Ongoing power status query.
  struct TvsPowerStatusQuery;
  // Ongoing power change request. Used for standby and wakeup requests.
  struct PowerChangeRequest;

  // Callback for TV power status responses.
  void OnTvPowerResponse(QueryId id,
                         base::FilePath device_path,
                         TvPowerStatus result);

  // If all responses for a given query has been received, this method will
  // invoke the query's callback and return true. False is returned when not
  // all of the responses have been received and thus the query has not
  // completed yet.
  bool MaybeRespondToTvsPowerStatusQuery(TvsPowerStatusQuery& query);

  // Create a new power change request and add it to power_change_requests_.
  // Returns the request ID.
  QueryId CreatePowerChangeRequest(PowerChangeSentCallback callback);

  // Callback when a standby or wakeup message has been sent to a device.
  void OnPowerChangeSent(QueryId id, base::FilePath device_path);

  // If messages have been sent to all devices, run the callback and return
  // true. Otherwise return false.
  bool MaybePowerChangeRequestComplete(PowerChangeRequest& request);

  // Called when udev reports that new device has been added.
  void OnDeviceAdded(const base::FilePath& device_path);

  // Called when udev reports that a device has been removed.
  void OnDeviceRemoved(const base::FilePath& device_path);

  // Enumerates and adds all existing devices.
  void EnumerateAndAddExistingDevices();

  // Creates new handler for a device with a given path.
  void AddNewDevice(const base::FilePath& path);

  // Factory of CEC device handlers.
  const CecDeviceFactory& cec_factory_;

  // Id to be used for next query.
  QueryId next_query_id_ = 0;

  // Id to be used for the next power change request.
  QueryId next_power_change_id_ = 0;

  // Ongoing power status queries.
  std::map<QueryId, TvsPowerStatusQuery> tv_power_status_queries_;

  // Ongoing power change requests.
  std::map<QueryId, PowerChangeRequest> power_change_requests_;

  // List of currently opened CEC devices.
  std::map<base::FilePath, std::unique_ptr<CecDevice>> devices_;

  // Udev object used to communicate with libudev.
  std::unique_ptr<Udev> udev_;

  base::WeakPtrFactory<CecManager> weak_factory_{this};
};

}  // namespace cecservice

#endif  // CECSERVICE_CEC_MANAGER_H_
