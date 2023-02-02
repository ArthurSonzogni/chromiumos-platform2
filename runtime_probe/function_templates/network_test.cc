// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>
#include <brillo/variant_dictionary.h>
#include <dbus/shill/dbus-constants.h>
#include <gtest/gtest.h>

#include "runtime_probe/function_templates/network.h"
#include "runtime_probe/utils/function_test_utils.h"
#include "shill/dbus-constants.h"

namespace runtime_probe {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

constexpr auto kNetworkDirPath("/sys/class/net");

constexpr auto kBusTypePci("pci");
constexpr auto kBusTypeSdio("sdio");
constexpr auto kBusTypeUsb("usb");

class MockNetworkFunction : public NetworkFunction {
  using NetworkFunction::NetworkFunction;

 public:
  NAME_PROBE_FUNCTION("mock_network");

  MOCK_METHOD(std::optional<std::string>,
              GetNetworkType,
              (),
              (const, override));
};

class NetworkFunctionTest : public BaseFunctionTest {
 protected:
  // Set up |network_fields| for network device.
  // For example:
  //   SetNetworkDevice("pci", "wlan0",
  //                  {{"device", "0x1111"}, {"vendor", "0x2222"}});
  // The function will set "0x1111" to file /sys/class/net/wlan0/device
  // and "0x2222" to file /sys/class/net/wlan0/vendor.
  //
  // @param bus_type: Bus type of the network device.
  // @param interface: Interface name under /sys/class/net/ .
  // @param network_fields: A vector of <field name, field value> indicates
  // information to be probed.
  // @param usb_id: Set only when |bus_type| is "usb". Path name of the
  // directory under sys/bus/usb/devices/ where |network_fields| is put at.
  void SetNetworkDevice(
      const std::string& bus_type,
      const std::string& interface,
      const std::vector<std::pair<std::string, std::string>>& network_fields,
      const std::string& usb_id = "") {
    std::string field_path;
    if (bus_type == kBusTypeUsb) {
      SetDirectory({"sys/bus/usb/devices", usb_id, "0:0"});
      SetSymbolicLink({"/sys/bus/usb/devices", usb_id, "0:0"},
                      {kNetworkDirPath, interface, "device"});
      field_path = base::StringPrintf("sys/bus/usb/devices/%s", usb_id.c_str());
    } else {
      field_path = base::StringPrintf("%s/%s/device", kNetworkDirPath,
                                      interface.c_str());
    }

    // The symbolic link is for the probe function to get the bus type.
    SetDirectory({"sys/bus", bus_type});
    SetSymbolicLink({"/sys/bus", bus_type},
                    {kNetworkDirPath, interface, "device/subsystem"});

    // Set up network information that is going to be probed.
    for (const auto& [field, value] : network_fields) {
      SetFile({field_path, field}, value);
    }
  }
};

TEST_F(NetworkFunctionTest, ProbeNetworkPci) {
  const base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<MockNetworkFunction>(probe_statement);

  SetNetworkDevice(kBusTypePci, "wlan0",
                   {{"device", "0x1111"}, {"vendor", "0x2222"}});

  brillo::VariantDictionary device0_props = {
      {shill::kInterfaceProperty, std::string("wlan0")},
      {shill::kTypeProperty, std::string(shill::kTypeWifi)}};
  mock_context()->SetShillProxies({{"/dev/0", device0_props}});

  EXPECT_CALL(*probe_function, GetNetworkType()).WillOnce(Return(std::nullopt));
  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "bus_type": "pci",
        "path": "%s",
        "pci_device_id": "0x1111",
        "pci_vendor_id": "0x2222",
        "type": "wireless"
      }
    ]
  )JSON",
      GetPathUnderRoot({kNetworkDirPath, "wlan0"}).value().c_str()));
  EXPECT_EQ(result, ans);
}

TEST_F(NetworkFunctionTest, GetPciRevisionIdFromConfig) {
  const base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<MockNetworkFunction>(probe_statement);

  SetNetworkDevice(kBusTypePci, "wlan0",
                   {{"device", "0x1111"}, {"vendor", "0x2222"}});

  brillo::VariantDictionary device0_props = {
      {shill::kInterfaceProperty, std::string("wlan0")},
      {shill::kTypeProperty, std::string(shill::kTypeWifi)}};
  mock_context()->SetShillProxies({{"/dev/0", device0_props}});

  EXPECT_CALL(*probe_function, GetNetworkType()).WillOnce(Return(std::nullopt));

  // The revision is at offset 8 of the binary file.
  std::vector<uint8_t> config_buffer{0x00, 0x01, 0x02, 0x03, 0x04,
                                     0x05, 0x06, 0x07, 0x08, 0x09};
  SetFile({kNetworkDirPath, "wlan0/device/config"},
          base::span<uint8_t>(config_buffer));

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "bus_type": "pci",
        "path": "%s",
        "pci_device_id": "0x1111",
        "pci_vendor_id": "0x2222",
        "pci_revision": "0x08",
        "type": "wireless"
      }
    ]
  )JSON",
      GetPathUnderRoot({kNetworkDirPath, "wlan0"}).value().c_str()));
  EXPECT_EQ(result, ans);
}

TEST_F(NetworkFunctionTest, GetPciRevisionIdFromConfigFailed) {
  const base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<MockNetworkFunction>(probe_statement);

  SetNetworkDevice(kBusTypePci, "wlan0",
                   {{"device", "0x1111"}, {"vendor", "0x2222"}});

  brillo::VariantDictionary device0_props = {
      {shill::kInterfaceProperty, std::string("wlan0")},
      {shill::kTypeProperty, std::string(shill::kTypeWifi)}};
  mock_context()->SetShillProxies({{"/dev/0", device0_props}});

  EXPECT_CALL(*probe_function, GetNetworkType()).WillOnce(Return(std::nullopt));

  // Fail to read the binary file at offset 8.
  std::vector<uint8_t> config_buffer{0x00, 0x01, 0x02, 0x03, 0x04};
  SetFile({kNetworkDirPath, "wlan0/device/config"},
          base::span<uint8_t>(config_buffer));

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "bus_type": "pci",
        "path": "%s",
        "pci_device_id": "0x1111",
        "pci_vendor_id": "0x2222",
        "type": "wireless"
      }
    ]
  )JSON",
      GetPathUnderRoot({kNetworkDirPath, "wlan0"}).value().c_str()));
  EXPECT_EQ(result, ans);
}

TEST_F(NetworkFunctionTest, ProbeNetworkSdio) {
  const base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<MockNetworkFunction>(probe_statement);

  SetNetworkDevice(kBusTypeSdio, "wlan0",
                   {{"device", "0x1111"}, {"vendor", "0x2222"}});

  brillo::VariantDictionary device0_props = {
      {shill::kInterfaceProperty, std::string("wlan0")},
      {shill::kTypeProperty, std::string(shill::kTypeWifi)}};
  mock_context()->SetShillProxies({{"/dev/0", device0_props}});

  EXPECT_CALL(*probe_function, GetNetworkType()).WillOnce(Return(std::nullopt));
  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "bus_type": "sdio",
        "path": "%s",
        "sdio_device_id": "0x1111",
        "sdio_vendor_id": "0x2222",
        "type": "wireless"
      }
    ]
  )JSON",
      GetPathUnderRoot({kNetworkDirPath, "wlan0"}).value().c_str()));
  EXPECT_EQ(result, ans);
}

TEST_F(NetworkFunctionTest, ProbeNetworkUsb) {
  const base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<MockNetworkFunction>(probe_statement);

  SetNetworkDevice(kBusTypeUsb, "wlan0",
                   {{"idProduct", "0x1111"}, {"idVendor", "0x2222"}}, "0");

  brillo::VariantDictionary device0_props = {
      {shill::kInterfaceProperty, std::string("wlan0")},
      {shill::kTypeProperty, std::string(shill::kTypeWifi)}};
  mock_context()->SetShillProxies({{"/dev/0", device0_props}});

  EXPECT_CALL(*probe_function, GetNetworkType()).WillOnce(Return(std::nullopt));
  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "bus_type": "usb",
        "path": "%s",
        "usb_product_id": "0x1111",
        "usb_vendor_id": "0x2222",
        "type": "wireless"
      }
    ]
  )JSON",
      GetPathUnderRoot({kNetworkDirPath, "wlan0"}).value().c_str()));
  EXPECT_EQ(result, ans);
}

TEST_F(NetworkFunctionTest, UnknownBusType) {
  const base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<MockNetworkFunction>(probe_statement);

  // The bus type is "unknown".
  SetNetworkDevice("unknown", "wlan0",
                   {{"device", "0x1111"}, {"vendor", "0x2222"}});

  brillo::VariantDictionary device0_props = {
      {shill::kInterfaceProperty, std::string("wlan0")},
      {shill::kTypeProperty, std::string(shill::kTypeWifi)}};
  mock_context()->SetShillProxies({{"/dev/0", device0_props}});

  EXPECT_CALL(*probe_function, GetNetworkType()).WillOnce(Return(std::nullopt));
  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(NetworkFunctionTest, NoRequiredFields) {
  const base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<MockNetworkFunction>(probe_statement);

  // No required field "vendor".
  SetNetworkDevice(kBusTypePci, "wlan0", {{"device", "0x1111"}});

  brillo::VariantDictionary device0_props = {
      {shill::kInterfaceProperty, std::string("wlan0")},
      {shill::kTypeProperty, std::string(shill::kTypeWifi)}};
  mock_context()->SetShillProxies({{"/dev/0", device0_props}});

  EXPECT_CALL(*probe_function, GetNetworkType()).WillOnce(Return(std::nullopt));
  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(NetworkFunctionTest, ProbeAllTypeNetwork) {
  const base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<MockNetworkFunction>(probe_statement);

  SetNetworkDevice(kBusTypePci, "wlan0",
                   {{"device", "0x1111"}, {"vendor", "0x2222"}});
  SetNetworkDevice(kBusTypePci, "eth0",
                   {{"device", "0x3333"}, {"vendor", "0x4444"}});
  SetNetworkDevice(kBusTypePci, "wwan0",
                   {{"device", "0x5555"}, {"vendor", "0x6666"}});

  brillo::VariantDictionary device0_props = {
      {shill::kInterfaceProperty, std::string("wlan0")},
      {shill::kTypeProperty, std::string(shill::kTypeWifi)}};
  brillo::VariantDictionary device1_props = {
      {shill::kInterfaceProperty, std::string("eth0")},
      {shill::kTypeProperty, std::string(shill::kTypeEthernet)}};
  brillo::VariantDictionary device2_props = {
      {shill::kInterfaceProperty, std::string("wwan0")},
      {shill::kTypeProperty, std::string(shill::kTypeCellular)}};
  mock_context()->SetShillProxies({{"/dev/0", device0_props},
                                   {"/dev/1", device1_props},
                                   {"/dev/2", device2_props}});

  // Probe all types of network.
  EXPECT_CALL(*probe_function, GetNetworkType()).WillOnce(Return(std::nullopt));
  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "bus_type": "pci",
        "path": "%s",
        "pci_device_id": "0x1111",
        "pci_vendor_id": "0x2222",
        "type": "wireless"
      },
      {
        "bus_type": "pci",
        "path": "%s",
        "pci_device_id": "0x3333",
        "pci_vendor_id": "0x4444",
        "type": "ethernet"
      },
      {
        "bus_type": "pci",
        "path": "%s",
        "pci_device_id": "0x5555",
        "pci_vendor_id": "0x6666",
        "type": "cellular"
      }
    ]
  )JSON",
      GetPathUnderRoot({kNetworkDirPath, "wlan0"}).value().c_str(),
      GetPathUnderRoot({kNetworkDirPath, "eth0"}).value().c_str(),
      GetPathUnderRoot({kNetworkDirPath, "wwan0"}).value().c_str()));
  EXPECT_EQ(result, ans);
}

TEST_F(NetworkFunctionTest, ProbeSpecificTypeNetwork) {
  const base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<MockNetworkFunction>(probe_statement);

  SetNetworkDevice(kBusTypePci, "wlan0",
                   {{"device", "0x1111"}, {"vendor", "0x2222"}});
  SetNetworkDevice(kBusTypePci, "eth0",
                   {{"device", "0x3333"}, {"vendor", "0x4444"}});
  SetNetworkDevice(kBusTypePci, "wwan0",
                   {{"device", "0x5555"}, {"vendor", "0x6666"}});

  brillo::VariantDictionary device0_props = {
      {shill::kInterfaceProperty, std::string("wlan0")},
      {shill::kTypeProperty, std::string(shill::kTypeWifi)}};
  brillo::VariantDictionary device1_props = {
      {shill::kInterfaceProperty, std::string("eth0")},
      {shill::kTypeProperty, std::string(shill::kTypeEthernet)}};
  brillo::VariantDictionary device2_props = {
      {shill::kInterfaceProperty, std::string("wwan0")},
      {shill::kTypeProperty, std::string(shill::kTypeCellular)}};
  mock_context()->SetShillProxies({{"/dev/0", device0_props},
                                   {"/dev/1", device1_props},
                                   {"/dev/2", device2_props}});

  // Probe only wireless network.
  EXPECT_CALL(*probe_function, GetNetworkType())
      .WillOnce(Return(shill::kTypeWifi));
  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "bus_type": "pci",
        "path": "%s",
        "pci_device_id": "0x1111",
        "pci_vendor_id": "0x2222",
        "type": "wireless"
      }
    ]
  )JSON",
      GetPathUnderRoot({kNetworkDirPath, "wlan0"}).value().c_str()));
  EXPECT_EQ(result, ans);
}

}  // namespace
}  // namespace runtime_probe
