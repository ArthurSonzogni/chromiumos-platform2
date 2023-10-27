// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_CALLBACK_SERVICES_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_CALLBACK_SERVICES_H_

#include <string>
#include <vector>

#include <base/callback_list.h>
#include <base/memory/weak_ptr.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>

#include "diagnostics/dbus_bindings/floss_callback/org.chromium.bluetooth.BluetoothCallback.h"
#include "diagnostics/dbus_bindings/floss_callback/org.chromium.bluetooth.BluetoothConnectionCallback.h"
#include "diagnostics/dbus_bindings/floss_callback/org.chromium.bluetooth.ManagerCallback.h"
#include "diagnostics/dbus_bindings/floss_callback/org.chromium.bluetooth.ScannerCallback.h"

namespace diagnostics {

class FlossEventHub;

// Callback services for registering callbacks and receiving Floss events.
//
// Supports org.chromium.bluetooth.BluetoothCallback registration.
class BluetoothCallbackService
    : public org::chromium::bluetooth::BluetoothCallbackInterface,
      public org::chromium::bluetooth::BluetoothCallbackAdaptor {
 public:
  explicit BluetoothCallbackService(FlossEventHub* event_hub,
                                    const scoped_refptr<dbus::Bus>& bus,
                                    const dbus::ObjectPath& object_path,
                                    const dbus::ObjectPath& adapter_path);
  BluetoothCallbackService(const BluetoothCallbackService&) = delete;
  BluetoothCallbackService& operator=(const BluetoothCallbackService&) = delete;
  ~BluetoothCallbackService();

 private:
  // org::chromium::bluetooth::BluetoothCallbackInterface overrides:
  void OnAdapterPropertyChanged(uint32_t property) override;
  void OnAddressChanged(const std::string& address) override;
  void OnNameChanged(const std::string& name) override;
  void OnDiscoverableChanged(bool discoverable) override;
  void OnDiscoveringChanged(bool discovering) override;
  void OnDeviceFound(const brillo::VariantDictionary& device) override;
  void OnDeviceCleared(const brillo::VariantDictionary& device) override;
  void OnDevicePropertiesChanged(
      const brillo::VariantDictionary& device,
      const std::vector<uint32_t>& properties) override;
  void OnBondStateChanged(uint32_t bt_status,
                          const std::string& address,
                          uint32_t bond_state) override;
  void OnSspRequest(const brillo::VariantDictionary& device,
                    uint32_t cod,
                    uint32_t bt_ssp_variant,
                    uint32_t passkey) override;
  void OnSdpSearchComplete(
      const brillo::VariantDictionary& device,
      const std::vector<uint8_t>& searched_uuid,
      const std::vector<brillo::VariantDictionary>& sdp_records) override;

  // Unowned pointer. Used to send Bluetooth events.
  FlossEventHub* const event_hub_;

  // Object path of the adapter that register this callback.
  const dbus::ObjectPath adapter_path_;

  // D-Bus helper when registering callback service.
  brillo::dbus_utils::DBusObject dbus_object_;

  // Must be the last class member.
  base::WeakPtrFactory<BluetoothCallbackService> weak_ptr_factory_{this};
};

// Supports org.chromium.bluetooth.ManagerCallback registration.
class ManagerCallbackService
    : public org::chromium::bluetooth::ManagerCallbackInterface,
      public org::chromium::bluetooth::ManagerCallbackAdaptor {
 public:
  explicit ManagerCallbackService(FlossEventHub* event_hub,
                                  const scoped_refptr<dbus::Bus>& bus,
                                  const dbus::ObjectPath& object_path);
  ManagerCallbackService(const ManagerCallbackService&) = delete;
  ManagerCallbackService& operator=(const ManagerCallbackService&) = delete;
  ~ManagerCallbackService();

 private:
  // org::chromium::bluetooth::ManagerCallbackInterface overrides:
  void OnHciDeviceChanged(int32_t hci_interface, bool present) override;
  void OnHciEnabledChanged(int32_t hci_interface, bool enabled) override;
  void OnDefaultAdapterChanged(int32_t hci_interface) override;

  // Unowned pointer. Used to send Bluetooth events.
  FlossEventHub* const event_hub_;

  // D-Bus helper when registering callback service.
  brillo::dbus_utils::DBusObject dbus_object_;

  // Must be the last class member.
  base::WeakPtrFactory<ManagerCallbackService> weak_ptr_factory_{this};
};

// Supports org.chromium.bluetooth.BluetoothConnectionCallback registration.
class BluetoothConnectionCallbackService
    : public org::chromium::bluetooth::BluetoothConnectionCallbackInterface,
      public org::chromium::bluetooth::BluetoothConnectionCallbackAdaptor {
 public:
  explicit BluetoothConnectionCallbackService(
      FlossEventHub* event_hub,
      const scoped_refptr<dbus::Bus>& bus,
      const dbus::ObjectPath& object_path);
  BluetoothConnectionCallbackService(
      const BluetoothConnectionCallbackService&) = delete;
  BluetoothConnectionCallbackService& operator=(
      const BluetoothConnectionCallbackService&) = delete;
  ~BluetoothConnectionCallbackService();

 private:
  // org::chromium::bluetooth::BluetoothConnectionCallbackInterface overrides:
  void OnDeviceConnected(const brillo::VariantDictionary& device) override;
  void OnDeviceDisconnected(const brillo::VariantDictionary& device) override;

  // Unowned pointer. Used to send Bluetooth events.
  FlossEventHub* const event_hub_;

  // D-Bus helper when registering callback service.
  brillo::dbus_utils::DBusObject dbus_object_;

  // Must be the last class member.
  base::WeakPtrFactory<BluetoothConnectionCallbackService> weak_ptr_factory_{
      this};
};

// Supports org.chromium.bluetooth.ScannerCallback registration.
class ScannerCallbackService
    : public org::chromium::bluetooth::ScannerCallbackInterface,
      public org::chromium::bluetooth::ScannerCallbackAdaptor {
 public:
  explicit ScannerCallbackService(FlossEventHub* event_hub,
                                  const scoped_refptr<dbus::Bus>& bus,
                                  const dbus::ObjectPath& object_path);
  ScannerCallbackService(const ScannerCallbackService&) = delete;
  ScannerCallbackService& operator=(const ScannerCallbackService&) = delete;
  ~ScannerCallbackService();

 private:
  // org::chromium::bluetooth::ScannerCallbackInterface overrides:
  void OnScanResult(const brillo::VariantDictionary& scan_result) override;

  // Unowned pointer. Used to notify Bluetooth events.
  FlossEventHub* const event_hub_;

  // D-Bus helper when registering callback service.
  brillo::dbus_utils::DBusObject dbus_object_;

  // Must be the last class member.
  base::WeakPtrFactory<ScannerCallbackService> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_CALLBACK_SERVICES_H_
