// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MODEM_H_
#define MODEMFWD_MODEM_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <dbus/bus.h>

#include "modemfwd/modem_helper.h"
#include "modemfwd/modem_helper_directory.h"
#include "shill/dbus-proxies.h"

namespace modemfwd {

class Modem : public base::RefCountedThreadSafe<Modem> {
 public:
  // Must be in sync with ModemManager's MMModemState enum.
  enum class State {
    FAILED = -1,
    UNKNOWN = 0,
    INITIALIZING = 1,
    LOCKED = 2,
    DISABLED = 3,
    DISABLING = 4,
    ENABLING = 5,
    ENABLED = 6,
    SEARCHING = 7,
    REGISTERED = 8,
    DISCONNECTING = 9,
    CONNECTING = 10,
    CONNECTED = 11,
  };

  // Must be in sync with ModemManager's MMModemPowerState enum.
  enum class PowerState { UNKNOWN = 0, OFF = 1, LOW = 2, ON = 3 };

  // True if this modem exists on the bus.
  virtual bool IsPresent() const = 0;

  // Get this modem's device ID.
  virtual std::string GetDeviceId() const = 0;

  // Get a unique identifier for this modem, such as an IMEI.
  virtual std::string GetEquipmentId() const = 0;

  // Get an ID for the carrier this modem is currently operating with,
  // or the empty string if there is none. Note that the ID is not
  // necessarily a readable name or e.g. MCC/MNC pair.
  virtual std::string GetCarrierId() const = 0;

  // Get the helper associated with this modem
  virtual ModemHelper* GetHelper() const = 0;

  // Information about this modem's installed firmware.
  virtual std::string GetMainFirmwareVersion() const = 0;
  virtual std::string GetOemFirmwareVersion() const = 0;
  virtual std::string GetCarrierFirmwareId() const = 0;
  virtual std::string GetCarrierFirmwareVersion() const = 0;
  virtual std::string GetAssocFirmwareVersion(std::string fw_tag) const = 0;

  // Tell ModemManager not to deal with this modem for a little while.
  virtual bool SetInhibited(bool inhibited) = 0;

  virtual bool FlashFirmwares(const std::vector<FirmwareConfig>& configs) = 0;

  // Run health checks on this modem
  virtual bool SupportsHealthCheck() const = 0;
  virtual bool CheckHealth() = 0;

  // Handle modem power state.
  virtual PowerState GetPowerState() const = 0;
  virtual bool UpdatePowerState(PowerState new_power_state) = 0;

  // Handle modem state.
  virtual State GetState() const = 0;
  virtual bool UpdateState(State new_state) = 0;

 protected:
  friend class base::RefCountedThreadSafe<Modem>;
  virtual ~Modem() = default;
};

scoped_refptr<Modem> CreateModem(
    dbus::Bus* bus,
    std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> device,
    ModemHelperDirectory* helper_directory);

scoped_refptr<Modem> CreateStubModem(const std::string& device_id,
                                     ModemHelperDirectory* helper_directory,
                                     bool use_real_fw_info);

std::ostream& operator<<(std::ostream& os, const Modem::State& rhs);

std::ostream& operator<<(std::ostream& os, const Modem::PowerState& rhs);

}  // namespace modemfwd

#endif  // MODEMFWD_MODEM_H_
