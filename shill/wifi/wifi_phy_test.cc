// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_phy.h"

#include <linux/nl80211.h>

#include <algorithm>
#include <iterator>
#include <vector>

#include <base/test/mock_callback.h>
#include <chromeos/net-base/attribute_list.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/net-base/netlink_attribute.h>
#include <chromeos/net-base/netlink_packet.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/mock_hotspot_device.h"
#include "shill/wifi/mock_p2p_device.h"
#include "shill/wifi/mock_wake_on_wifi.h"
#include "shill/wifi/mock_wifi.h"

using testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Test;

namespace shill {

namespace {
// NL80211_CMD_NEW_WIPHY message which indicates support for the following
// interface types: IBSS, managed, AP, monitor, P2P-client, P2P-GO, P2P-device.
const uint8_t kNewWiphyNlMsg_IfTypes[] = {
    0x6C, 0x00, 0x00, 0x00, 0x13, 0x00, 0x01, 0x00, 0x0D, 0x00, 0x00, 0x00,
    0x0D, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x02, 0x00, 0x70, 0x68, 0x79, 0x37,
    0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x09, 0x00, 0x09, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0A, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x2E, 0x00, 0x0F, 0x00, 0x00, 0x00};

// Bytes representing a NL80211_CMD_NEW_WIPHY message reporting the WiFi
// capabilities of a NIC with wiphy index |kWiFiPhyIndex| which supports
// operating bands with the frequencies specified in
// |kNewWiphyNlMsg_AllFrequencies|.
// Note that this message is marked as part of multi-message PHY dump so you
// need to signal to WiFiPhy the end of it via PhyDumpComplete() call.
const uint8_t kNewWiphyNlMsg[] = {
    0x38, 0x0C, 0x00, 0x00, 0x14, 0x00, 0x03, 0x00, 0x0D, 0x00, 0x00, 0x00,
    0x1D, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x02, 0x00, 0x70, 0x68, 0x79, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x16, 0x00, 0xF8, 0x01, 0x00, 0x00,
    0x28, 0x01, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x6C, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00,
    0x14, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x71, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00, 0x14, 0x00, 0x02, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x76, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x6C, 0x07, 0x00, 0x00, 0x14, 0x00, 0x03, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x7B, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00,
    0x14, 0x00, 0x04, 0x00, 0x08, 0x00, 0x01, 0x00, 0x80, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00, 0x14, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x85, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x6C, 0x07, 0x00, 0x00, 0x14, 0x00, 0x06, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x8A, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00,
    0x14, 0x00, 0x07, 0x00, 0x08, 0x00, 0x01, 0x00, 0x8F, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00, 0x14, 0x00, 0x08, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x94, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x6C, 0x07, 0x00, 0x00, 0x14, 0x00, 0x09, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x99, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00,
    0x14, 0x00, 0x0A, 0x00, 0x08, 0x00, 0x01, 0x00, 0x9E, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00, 0x18, 0x00, 0x0B, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xA3, 0x09, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00, 0x18, 0x00, 0x0C, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xA8, 0x09, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6C, 0x07, 0x00, 0x00, 0x18, 0x00, 0x0D, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xB4, 0x09, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00, 0xA0, 0x00, 0x02, 0x00,
    0x0C, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x37, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00, 0x10, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x6E, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00,
    0x0C, 0x00, 0x04, 0x00, 0x08, 0x00, 0x01, 0x00, 0x3C, 0x00, 0x00, 0x00,
    0x0C, 0x00, 0x05, 0x00, 0x08, 0x00, 0x01, 0x00, 0x5A, 0x00, 0x00, 0x00,
    0x0C, 0x00, 0x06, 0x00, 0x08, 0x00, 0x01, 0x00, 0x78, 0x00, 0x00, 0x00,
    0x0C, 0x00, 0x07, 0x00, 0x08, 0x00, 0x01, 0x00, 0xB4, 0x00, 0x00, 0x00,
    0x0C, 0x00, 0x08, 0x00, 0x08, 0x00, 0x01, 0x00, 0xF0, 0x00, 0x00, 0x00,
    0x0C, 0x00, 0x09, 0x00, 0x08, 0x00, 0x01, 0x00, 0x68, 0x01, 0x00, 0x00,
    0x0C, 0x00, 0x0A, 0x00, 0x08, 0x00, 0x01, 0x00, 0xE0, 0x01, 0x00, 0x00,
    0x0C, 0x00, 0x0B, 0x00, 0x08, 0x00, 0x01, 0x00, 0x1C, 0x02, 0x00, 0x00,
    0x14, 0x00, 0x03, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x04, 0x00,
    0xEF, 0x11, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x06, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04, 0x03, 0x01, 0x00,
    0x70, 0x02, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x3C, 0x14, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x18, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x50, 0x14, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x64, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00, 0x14, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x78, 0x14, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x04, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x8C, 0x14, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xA0, 0x14, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x1C, 0x00, 0x06, 0x00, 0x08, 0x00, 0x01, 0x00, 0xB4, 0x14, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x07, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xC8, 0x14, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x08, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x7C, 0x15, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x1C, 0x00, 0x09, 0x00, 0x08, 0x00, 0x01, 0x00, 0x90, 0x15, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x0A, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xA4, 0x15, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x0B, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xB8, 0x15, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x1C, 0x00, 0x0C, 0x00, 0x08, 0x00, 0x01, 0x00, 0xCC, 0x15, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x0D, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xE0, 0x15, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x0E, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xF4, 0x15, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x1C, 0x00, 0x0F, 0x00, 0x08, 0x00, 0x01, 0x00, 0x08, 0x16, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x10, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x1C, 0x16, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00, 0x1C, 0x00, 0x11, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x30, 0x16, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x1C, 0x00, 0x12, 0x00, 0x08, 0x00, 0x01, 0x00, 0x44, 0x16, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xD0, 0x07, 0x00, 0x00, 0x14, 0x00, 0x13, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x71, 0x16, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x18, 0x00, 0x14, 0x00, 0x08, 0x00, 0x01, 0x00, 0x85, 0x16, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x18, 0x00, 0x15, 0x00, 0x08, 0x00, 0x01, 0x00, 0x99, 0x16, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x18, 0x00, 0x16, 0x00, 0x08, 0x00, 0x01, 0x00, 0xAD, 0x16, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x18, 0x00, 0x17, 0x00, 0x08, 0x00, 0x01, 0x00, 0xC1, 0x16, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x08, 0x00, 0x06, 0x00, 0xD0, 0x07, 0x00, 0x00,
    0x64, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x3C, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x5A, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x78, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x03, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xB4, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x04, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xF0, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x05, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x68, 0x01, 0x00, 0x00, 0x0C, 0x00, 0x06, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xE0, 0x01, 0x00, 0x00, 0x0C, 0x00, 0x07, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x1C, 0x02, 0x00, 0x00, 0x14, 0x00, 0x03, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x04, 0x00, 0xEF, 0x11, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x44, 0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x05, 0x00, 0x05, 0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x09, 0x00, 0x09, 0x00, 0x00, 0x00, 0x05, 0x00, 0x2B, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x2E, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xD4, 0x00, 0x32, 0x00, 0x08, 0x00, 0x01, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x06, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00,
    0x0B, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x00, 0x0F, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x05, 0x00, 0x13, 0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x19, 0x00, 0x00, 0x00, 0x08, 0x00, 0x07, 0x00, 0x25, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x08, 0x00, 0x26, 0x00, 0x00, 0x00, 0x08, 0x00, 0x09, 0x00,
    0x27, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0A, 0x00, 0x28, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x0B, 0x00, 0x2B, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0C, 0x00,
    0x37, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0D, 0x00, 0x39, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x0E, 0x00, 0x3B, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0F, 0x00,
    0x43, 0x00, 0x00, 0x00, 0x08, 0x00, 0x10, 0x00, 0x31, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x11, 0x00, 0x41, 0x00, 0x00, 0x00, 0x08, 0x00, 0x12, 0x00,
    0x42, 0x00, 0x00, 0x00, 0x08, 0x00, 0x13, 0x00, 0x52, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x14, 0x00, 0x51, 0x00, 0x00, 0x00, 0x08, 0x00, 0x15, 0x00,
    0x54, 0x00, 0x00, 0x00, 0x08, 0x00, 0x16, 0x00, 0x57, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x17, 0x00, 0x55, 0x00, 0x00, 0x00, 0x08, 0x00, 0x18, 0x00,
    0x2D, 0x00, 0x00, 0x00, 0x08, 0x00, 0x19, 0x00, 0x2E, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x1A, 0x00, 0x30, 0x00, 0x00, 0x00, 0x06, 0x00, 0x38, 0x00,
    0xD1, 0x08, 0x00, 0x00, 0x18, 0x00, 0x39, 0x00, 0x01, 0xAC, 0x0F, 0x00,
    0x05, 0xAC, 0x0F, 0x00, 0x02, 0xAC, 0x0F, 0x00, 0x04, 0xAC, 0x0F, 0x00,
    0x06, 0xAC, 0x0F, 0x00, 0x05, 0x00, 0x3D, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x3E, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x3F, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x00, 0x40, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x05, 0x00, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x59, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xAC, 0x03, 0x63, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x01, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xB0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xC0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xE0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xF0, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x02, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xB0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xC0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xE0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xF0, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x03, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xB0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xC0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xE0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xF0, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x04, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xB0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xC0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xE0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xF0, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x04, 0x00, 0x06, 0x00, 0x84, 0x00, 0x07, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xA0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xB0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xD0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xE0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x84, 0x00, 0x08, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xA0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xB0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xD0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xE0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x84, 0x00, 0x09, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xA0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xB0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xD0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xE0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x14, 0x01, 0x64, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xD0, 0x00, 0x00, 0x00, 0x14, 0x00, 0x02, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00,
    0x3C, 0x00, 0x03, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xA0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xB0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xC0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00,
    0x3C, 0x00, 0x04, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xA0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xB0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xC0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x04, 0x00, 0x06, 0x00, 0x1C, 0x00, 0x07, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xB0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xC0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x08, 0x00, 0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x09, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xB0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xC0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xD0, 0x00, 0x00, 0x00, 0x04, 0x00, 0x66, 0x00,
    0x04, 0x00, 0x68, 0x00, 0x08, 0x00, 0x69, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x6A, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x6C, 0x00,
    0x08, 0x00, 0x6F, 0x00, 0x88, 0x13, 0x00, 0x00, 0x08, 0x00, 0x71, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x72, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x50, 0x00, 0x78, 0x00, 0x4C, 0x00, 0x01, 0x00, 0x38, 0x00, 0x01, 0x00,
    0x1C, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00, 0x00,
    0x10, 0x00, 0x02, 0x00, 0x04, 0x00, 0x02, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x04, 0x00, 0x08, 0x00, 0x18, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x02, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x09, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00, 0x08, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x79, 0x00,
    0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x05, 0x00, 0x7B, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x8B, 0x00,
    0x04, 0x00, 0x8C, 0x00, 0x08, 0x00, 0x8F, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x1E, 0x00, 0x94, 0x00, 0x42, 0x08, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// In the above kNewWiphyNlMsg packet the following frequencies are present:
const WiFiPhy::Frequencies kNewWiphyNlMsg_AllFrequencies = {
    {0,
     {
         {.value = 2412,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2417,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2422,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2427,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2432,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2437,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2442,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2447,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2452,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2457,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.value = 2462,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
          .value = 2467,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
          .value = 2472,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 1900}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
          .value = 2484,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
     }},
    {1,
     {
         {.value = 5180,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
          .value = 5200,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.value = 5220,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.value = 5240,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5260,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5280,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5300,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5320,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5500,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5520,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5540,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5560,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5580,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5600,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5620,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5640,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5660,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5680,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR |
                   1 << NL80211_FREQUENCY_ATTR_RADAR,
          .value = 5700,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.value = 5745,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
          .value = 5765,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
          .value = 5785,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
          .value = 5805,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
         {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
          .value = 5825,
          .attributes = {{NL80211_FREQUENCY_ATTR_MAX_TX_POWER, 2000}}},
     }}};

// Bytes representing a NL80211_CMD_NEW_WIPHY message which includes the
// attribute NL80211_ATTR_INTERFACE_COMBINATIONS. The combination in this
// message supports single channel on a single interface. The full combinations
// attribute of this message looks like this:
//
// valid interface combinations:
//     * #{ P2P-client } <= 1, #{ managed, AP, P2P-GO } <= 1, #{ P2P-device }
//     <= 1, total <= 3, #channels <= 1
const uint8_t kNewSingleChannelNoAPSTAConcurrencyNlMsg[] = {
    0xac, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xf6, 0x31, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x02, 0x00, 0x70, 0x68, 0x79, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x2e, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x79, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x06, 0x00,
    0x70, 0x00, 0x78, 0x00, 0x6c, 0x00, 0x01, 0x00, 0x48, 0x00, 0x01, 0x00,
    0x14, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0x08, 0x00, 0x1c, 0x00, 0x02, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x02, 0x00,
    0x04, 0x00, 0x02, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x09, 0x00,
    0x14, 0x00, 0x03, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x08, 0x00, 0x04, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Bytes representing a NL80211_CMD_NEW_WIPHY message which includes the
// attribute NL80211_ATTR_INTERFACE_COMBINATIONS. The combination in this
// message supports single channel on a single interface. The full combinations
// attribute of this message looks like this:
//
// valid interface combinations:
//     * #{ managed } <= 1, #{ AP, P2P-client, P2P-GO } <= 1, #{ P2P-device }
//     <= 1, total <= 3, #channels <= 1
const uint8_t kNewSingleChannelConcurrencyNlMsg[] = {
    0xac, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xf6, 0x31, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x02, 0x00, 0x70, 0x68, 0x79, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x2e, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x79, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x06, 0x00,
    0x70, 0x00, 0x78, 0x00, 0x6c, 0x00, 0x01, 0x00, 0x48, 0x00, 0x01, 0x00,
    0x14, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1c, 0x00, 0x02, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x02, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x08, 0x00, 0x04, 0x00, 0x09, 0x00,
    0x14, 0x00, 0x03, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x08, 0x00, 0x04, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Bytes representing a NL80211_CMD_NEW_WIPHY message which includes the
// attribute NL80211_ATTR_INTERFACE_COMBINATIONS. The combination in this
// message supports multiple channels on multiple interfaces. The full
// combinations attribute of this message looks like this:
//
// valid interface combinations:
//     * #{ managed } <= 2, #{ AP, P2P-client, P2P-GO } <= 2, #{ P2P-device }
//       <= 1, total <= 4, #channels <= 1
//     * #{ managed } <= 2, #{ P2P-client } <= 2, #{ AP, P2P-GO } <= 1,
//       #{ P2P-device } <= 1, total <= 4, #channels <= 2
//     * #{ managed } <= 1, #{ IBSS } <= 1,
//       total <= 2, #channels <= 1

const uint8_t kNewMultiChannelConcurrencyNlMsg[] = {
    0x72, 0x01, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xf6, 0x31, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x02, 0x00, 0x70, 0x68, 0x79, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x2e, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x79, 0x00, 0x04, 0x00, 0x06, 0x00, 0x3c, 0x01, 0x78, 0x00,
    0x6c, 0x00, 0x01, 0x00, 0x48, 0x00, 0x01, 0x00, 0x14, 0x00, 0x01, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00,
    0x04, 0x00, 0x02, 0x00, 0x1c, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x02, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x08, 0x00, 0x04, 0x00, 0x09, 0x00, 0x14, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00,
    0x04, 0x00, 0x0a, 0x00, 0x08, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7c, 0x00, 0x02, 0x00, 0x58, 0x00, 0x01, 0x00, 0x14, 0x00, 0x01, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00,
    0x04, 0x00, 0x02, 0x00, 0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0x08, 0x00,
    0x18, 0x00, 0x03, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x02, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x09, 0x00,
    0x14, 0x00, 0x04, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x08, 0x00, 0x04, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x03, 0x00, 0x2c, 0x00, 0x01, 0x00,
    0x14, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0x02, 0x00, 0x14, 0x00, 0x02, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00,
    0x04, 0x00, 0x01, 0x00, 0x08, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00};

uint32_t kWiFiPhyIndex = 0;

// WiFi frequency constants
constexpr uint32_t kLBStartFreq = 2412;
constexpr uint32_t kChan11Freq = 2462;
constexpr uint32_t kHBStartFreq = 5160;
constexpr uint32_t kHBEndFreq = 5980;

constexpr net_base::MacAddress kMacAddress0(0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff);
constexpr net_base::MacAddress kMacAddress1(0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa);
}  // namespace

class WiFiPhyTest : public ::testing::Test {
 public:
  WiFiPhyTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        wifi_phy_(kWiFiPhyIndex) {}
  ~WiFiPhyTest() override = default;

 protected:
  EventDispatcherForTest dispatcher_;
  MockControl control_interface_;
  NiceMock<MockMetrics> metrics_;
  MockManager manager_;
  WiFiPhy wifi_phy_;
  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      event_cb_;

  MockManager* manager() { return &manager_; }

  void AddWiFiDevice(WiFiConstRefPtr device) {
    wifi_phy_.AddWiFiDevice(device);
  }

  void DeleteWiFiDevice(WiFiConstRefPtr device) {
    wifi_phy_.DeleteWiFiDevice(device->link_name());
  }

  bool HasWiFiDevice(WiFiConstRefPtr device) {
    return base::Contains(wifi_phy_.wifi_devices_, device);
  }

  void ChangeDeviceState(WiFiConstRefPtr device) {
    wifi_phy_.WiFiDeviceStateChanged(device);
  }

  void PhyDumpComplete() { wifi_phy_.PhyDumpComplete(); }

  void OnNewWiphy(const Nl80211Message& nl80211_message) {
    wifi_phy_.OnNewWiphy(nl80211_message);
  }

  void AddSupportedIface(nl80211_iftype iftype) {
    wifi_phy_.supported_ifaces_.insert(iftype);
  }

  bool SupportsIftype(nl80211_iftype iftype) {
    return wifi_phy_.SupportsIftype(iftype);
  }

  void ParseInterfaceTypes(const Nl80211Message& nl80211_message) {
    wifi_phy_.ParseInterfaceTypes(nl80211_message);
  }

  void ParseConcurrency(const Nl80211Message& nl80211_message) {
    wifi_phy_.ParseConcurrency(nl80211_message);
  }

  uint32_t SupportsConcurrency(std::multiset<nl80211_iftype> iface_types) {
    return wifi_phy_.SupportsConcurrency(iface_types);
  }

  WiFiPhy::RemovalCandidateSet GetAllCandidates(
      std::vector<WiFiPhy::ConcurrentIface> ifaces) {
    return WiFiPhy::GetAllCandidates(ifaces);
  }

  std::optional<std::multiset<nl80211_iftype>> RequestNewIface(
      nl80211_iftype desired_type, WiFiPhy::Priority priority) {
    return wifi_phy_.RequestNewIface(desired_type, priority);
  }

  void AddActiveIfaces(std::vector<WiFiPhy::ConcurrentIface> ifaces) {
    for (auto iface : ifaces) {
      switch (iface.iftype) {
        case NL80211_IFTYPE_STATION:
          MockWiFi* wifi_device;
          wifi_device = new MockWiFi(&manager_, "wlan0", kMacAddress1, 0,
                                     kWiFiPhyIndex, new MockWakeOnWiFi());
          wifi_device->SetPriority(iface.priority);
          wifi_phy_.wifi_devices_.insert(wifi_device);
          break;
        case NL80211_IFTYPE_P2P_CLIENT:
          wifi_phy_.wifi_local_devices_.insert(new MockP2PDevice(
              &manager_, LocalDevice::IfaceType::kP2PClient, "wlan0", 0, 0,
              iface.priority, event_cb_.Get()));
          break;
        case NL80211_IFTYPE_P2P_GO:
          wifi_phy_.wifi_local_devices_.insert(new MockP2PDevice(
              &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, 0,
              iface.priority, event_cb_.Get()));
          break;
        case NL80211_IFTYPE_AP:
          wifi_phy_.wifi_local_devices_.insert(
              new MockHotspotDevice(&manager_, "wlan0", "ap0", kMacAddress1, 0,
                                    iface.priority, event_cb_.Get()));
          break;
        default:
          FAIL() << "Tried to create unsupported device type: " << iface.iftype;
      }
    }
  }

  void ClearActiveIfaces() {
    wifi_phy_.wifi_devices_.clear();
    wifi_phy_.wifi_local_devices_.clear();
  }

  void AssertConcurrencySorted() {
    auto current_comb = wifi_phy_.concurrency_combs_.begin();
    if (current_comb == wifi_phy_.concurrency_combs_.end()) {
      return;
    }
    auto next_comb = ++wifi_phy_.concurrency_combs_.begin();
    while (next_comb != wifi_phy_.concurrency_combs_.end()) {
      ASSERT_TRUE(current_comb->num_channels >= next_comb->num_channels);
      current_comb++;
      next_comb++;
    }
  }

  void AssertConcurrencyCombinationsAreEqual(ConcurrencyCombination lhs,
                                             ConcurrencyCombination rhs) {
    ASSERT_EQ(lhs.max_num, rhs.max_num);
    ASSERT_EQ(lhs.num_channels, rhs.num_channels);
    ASSERT_EQ(lhs.limits.size(), rhs.limits.size());

    for (uint i = 0; i < lhs.limits.size(); i++) {
      AssertIfaceLimitsAreEqual(lhs.limits[i], rhs.limits[i]);
    }
  }

  void AssertIfaceLimitsAreEqual(IfaceLimit lhs, IfaceLimit rhs) {
    ASSERT_EQ(lhs.max, rhs.max);
    ASSERT_EQ(lhs.iftypes.size(), rhs.iftypes.size());
    for (uint i = 0; i < lhs.iftypes.size(); i++) {
      ASSERT_EQ(lhs.iftypes[i], rhs.iftypes[i]);
    }
  }

  void AssertPhyConcurrencyIsEqualTo(ConcurrencyCombinationSet combs) {
    ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), combs.size());
    auto lhs_iter = wifi_phy_.concurrency_combs_.begin();
    auto rhs_iter = combs.begin();
    while (lhs_iter != wifi_phy_.concurrency_combs_.end()) {
      AssertConcurrencyCombinationsAreEqual(*lhs_iter, *rhs_iter);
      lhs_iter++;
      rhs_iter++;
    }
  }

  void AssertRemovalCandidateSetOrder(
      WiFiPhy::RemovalCandidateSet candidates,
      std::vector<WiFiPhy::RemovalCandidate> expected_order) {
    ASSERT_EQ(candidates.size(), expected_order.size());
    uint32_t idx = 0;
    for (auto candidate : candidates) {
      ASSERT_EQ(candidate, expected_order[idx]);
      idx++;
    }
  }

  void AssertApStaConcurrency(bool support) {
    ASSERT_EQ(wifi_phy_.SupportAPSTAConcurrency(), support);
  }

  struct ConcurrencyTestCase {
    std::vector<WiFiPhy::ConcurrentIface>
        present_ifaces;                  // Types already reserved.
    WiFiPhy::ConcurrentIface new_iface;  // Type to check.
    std::optional<std::multiset<nl80211_iftype>>
        expected_response;  // Expected response from RequestNewIface.
  };

  void TestInterfaceCombinations(std::vector<ConcurrencyTestCase> test_cases,
                                 ConcurrencyCombinationSet combs) {
    wifi_phy_.concurrency_combs_ = combs;
    for (auto tc : test_cases) {
      AddActiveIfaces(tc.present_ifaces);
      std::optional<std::multiset<nl80211_iftype>> response =
          RequestNewIface(tc.new_iface.iftype, tc.new_iface.priority);
      if (response != tc.expected_response) {
        LOG(INFO) << "Present ifaces: ";
        for (auto iface : tc.present_ifaces) {
          LOG(INFO) << "\tType: " << iface.iftype
                    << ", Priority: " << iface.priority;
        }
        LOG(INFO) << "Requested iface: ";
        LOG(INFO) << "\tType: " << tc.new_iface.iftype
                  << ", Priority: " << tc.new_iface.priority;
        // Technically redundant with the above "if" statement, but the macro
        // is useful for neat logging of a failed equality check.
        EXPECT_EQ(response, tc.expected_response);
      }
      ClearActiveIfaces();
    }
  }

  const WiFiPhy::Frequencies& frequencies() { return wifi_phy_.frequencies_; }

  void SetFrequencies(WiFiPhy::Frequencies& frequencies) {
    wifi_phy_.frequencies_ = frequencies;
  }
};

TEST_F(WiFiPhyTest, AddAndDeleteDevices) {
  scoped_refptr<MockWiFi> device0 = new NiceMock<MockWiFi>(
      &manager_, "null0", kMacAddress0, 0, kWiFiPhyIndex, new MockWakeOnWiFi());
  scoped_refptr<MockWiFi> device1 = new NiceMock<MockWiFi>(
      &manager_, "null1", kMacAddress1, 0, kWiFiPhyIndex, new MockWakeOnWiFi());

  EXPECT_FALSE(HasWiFiDevice(device0));
  EXPECT_FALSE(HasWiFiDevice(device1));

  AddWiFiDevice(device0);
  EXPECT_TRUE(HasWiFiDevice(device0));
  EXPECT_FALSE(HasWiFiDevice(device1));

  AddWiFiDevice(device1);
  EXPECT_TRUE(HasWiFiDevice(device0));
  EXPECT_TRUE(HasWiFiDevice(device1));

  DeleteWiFiDevice(device0);
  EXPECT_FALSE(HasWiFiDevice(device0));
  EXPECT_TRUE(HasWiFiDevice(device1));

  DeleteWiFiDevice(device1);
  EXPECT_FALSE(HasWiFiDevice(device0));
  EXPECT_FALSE(HasWiFiDevice(device1));
}

TEST_F(WiFiPhyTest, AddDeviceTwice) {
  scoped_refptr<MockWiFi> device = new NiceMock<MockWiFi>(
      &manager_, "null0", kMacAddress0, 0, kWiFiPhyIndex, new MockWakeOnWiFi());

  AddWiFiDevice(device);
  EXPECT_TRUE(HasWiFiDevice(device));

  // Adding the same device a second time should be a no-op.
  AddWiFiDevice(device);
  EXPECT_TRUE(HasWiFiDevice(device));

  // The device should be gone after one delete.
  DeleteWiFiDevice(device);
  EXPECT_FALSE(HasWiFiDevice(device));
}

TEST_F(WiFiPhyTest, DeleteDeviceTwice) {
  scoped_refptr<MockWiFi> device = new NiceMock<MockWiFi>(
      &manager_, "null0", kMacAddress0, 0, kWiFiPhyIndex, new MockWakeOnWiFi());

  AddWiFiDevice(device);
  EXPECT_TRUE(HasWiFiDevice(device));

  DeleteWiFiDevice(device);
  EXPECT_FALSE(HasWiFiDevice(device));

  // Deleting a device a second time should be a no-op.
  DeleteWiFiDevice(device);
  EXPECT_FALSE(HasWiFiDevice(device));
}

TEST_F(WiFiPhyTest, OnNewWiphy_CheckFreqs) {
  NewWiphyMessage msg;
  net_base::NetlinkPacket packet(kNewWiphyNlMsg);
  msg.InitFromPacketWithContext(&packet, Nl80211Message::Context());
  OnNewWiphy(msg);
  PhyDumpComplete();
  EXPECT_EQ(kNewWiphyNlMsg_AllFrequencies, frequencies());
}

TEST_F(WiFiPhyTest, OnNewWiphy_KeepLastFreqs) {
  NewWiphyMessage msg1;
  net_base::NetlinkPacket packet1(kNewWiphyNlMsg);
  msg1.InitFromPacketWithContext(&packet1, Nl80211Message::Context());

  // Modify flags and attributes for the frequencies reported in the message.
  net_base::AttributeListRefPtr bands_list;
  EXPECT_TRUE(msg1.attributes()->GetNestedAttributeList(
      NL80211_ATTR_WIPHY_BANDS, &bands_list));
  net_base::AttributeIdIterator bands_iter(*bands_list);
  for (; !bands_iter.AtEnd(); bands_iter.Advance()) {
    net_base::AttributeListRefPtr band_attrs;
    if (bands_list->GetNestedAttributeList(bands_iter.GetId(), &band_attrs)) {
      net_base::AttributeListRefPtr freqs_list;
      if (!band_attrs->GetNestedAttributeList(NL80211_BAND_ATTR_FREQS,
                                              &freqs_list)) {
        continue;
      }
      net_base::AttributeIdIterator freqs_iter(*freqs_list);
      for (; !freqs_iter.AtEnd(); freqs_iter.Advance()) {
        net_base::AttributeListRefPtr freq_attrs;
        if (freqs_list->GetNestedAttributeList(freqs_iter.GetId(),
                                               &freq_attrs)) {
          uint32_t value;
          for (auto attr = net_base::AttributeIdIterator(*freq_attrs);
               !attr.AtEnd(); attr.Advance()) {
            if (attr.GetType() == net_base::NetlinkAttribute::kTypeFlag) {
              freq_attrs->SetFlagAttributeValue(attr.GetId(), false);
            } else {
              EXPECT_EQ(attr.GetType(), net_base::NetlinkAttribute::kTypeU32);
              if (attr.GetId() == NL80211_FREQUENCY_ATTR_FREQ) {
                continue;
              }
              freq_attrs->GetU32AttributeValue(attr.GetId(), &value);
              freq_attrs->SetU32AttributeValue(attr.GetId(), value ^ -1U);
            }
          }
        }
      }
    }
  }

  EXPECT_NE(kNewWiphyNlMsg_AllFrequencies, frequencies());
  OnNewWiphy(msg1);
  // Now parse the original packet and observe that the attributes get
  // overwritten with proper values, each frequency is visible only once and the
  // frequencies get "public" visibility.
  NewWiphyMessage msg2;
  net_base::NetlinkPacket packet2(kNewWiphyNlMsg);
  msg2.InitFromPacketWithContext(&packet2, Nl80211Message::Context());
  OnNewWiphy(msg2);
  PhyDumpComplete();
  EXPECT_EQ(kNewWiphyNlMsg_AllFrequencies, frequencies());
}

TEST_F(WiFiPhyTest, SupportsIftype) {
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_AP));
  AddSupportedIface(NL80211_IFTYPE_AP);
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_AP));
}

TEST_F(WiFiPhyTest, ParseInterfaceTypes) {
  NewWiphyMessage msg;
  net_base::NetlinkPacket packet(kNewWiphyNlMsg_IfTypes);
  msg.InitFromPacketWithContext(&packet, Nl80211Message::Context());
  ParseInterfaceTypes(msg);
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_ADHOC));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_STATION));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_AP));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_MONITOR));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_P2P_CLIENT));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_P2P_GO));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_P2P_DEVICE));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_AP_VLAN));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_WDS));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_MESH_POINT));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_OCB));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_NAN));
  EXPECT_TRUE(wifi_phy_.SupportAPMode());
  EXPECT_TRUE(wifi_phy_.SupportP2PMode());

  EXPECT_TRUE(SupportsConcurrency({NL80211_IFTYPE_STATION}));
  EXPECT_TRUE(SupportsConcurrency({NL80211_IFTYPE_AP}));
  EXPECT_TRUE(SupportsConcurrency({NL80211_IFTYPE_MONITOR}));
  EXPECT_TRUE(SupportsConcurrency({NL80211_IFTYPE_P2P_CLIENT}));
  EXPECT_TRUE(SupportsConcurrency({NL80211_IFTYPE_P2P_GO}));
  EXPECT_TRUE(SupportsConcurrency({NL80211_IFTYPE_P2P_DEVICE}));
  EXPECT_FALSE(
      SupportsConcurrency({NL80211_IFTYPE_STATION, NL80211_IFTYPE_STATION}));
  EXPECT_FALSE(SupportsConcurrency({NL80211_IFTYPE_AP_VLAN}));
  EXPECT_FALSE(SupportsConcurrency({NL80211_IFTYPE_WDS}));
  EXPECT_FALSE(SupportsConcurrency({NL80211_IFTYPE_MESH_POINT}));
  EXPECT_FALSE(SupportsConcurrency({NL80211_IFTYPE_OCB}));
  EXPECT_FALSE(SupportsConcurrency({NL80211_IFTYPE_NAN}));
}

TEST_F(WiFiPhyTest, ParseNoAPSTAConcurrencySingleChannel) {
  NewWiphyMessage msg;
  net_base::NetlinkPacket packet(kNewSingleChannelNoAPSTAConcurrencyNlMsg);
  msg.InitFromPacketWithContext(&packet, Nl80211Message::Context());
  ParseConcurrency(msg);

  // These values align with those from
  // kNewSingleChannelNoAPSTAConcurrencyNlMsg. They must be declared inline
  // because the |nl80211_iftype|s are C values which can't be instantiated
  // outside a function context.
  ConcurrencyCombinationSet SingleChannelNoAPSTAConcurrencyCombinations{
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_CLIENT},
                                         .max = 1},
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_STATION, NL80211_IFTYPE_AP,
                                     NL80211_IFTYPE_P2P_GO},
                         .max = 1,
                     },
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                         .max = 1}},
          .max_num = 3,
          .num_channels = 1}};
  AssertPhyConcurrencyIsEqualTo(SingleChannelNoAPSTAConcurrencyCombinations);
  AssertApStaConcurrency(false);
}

TEST_F(WiFiPhyTest, ParseConcurrencySingleChannel) {
  NewWiphyMessage msg;
  net_base::NetlinkPacket packet(kNewSingleChannelConcurrencyNlMsg);
  msg.InitFromPacketWithContext(&packet, Nl80211Message::Context());
  ParseConcurrency(msg);

  // These values align with those from kNewSingleChannelConcurrencyNlMsg. They
  // must be declared inline because the |nl80211_iftype|s are C values
  // which can't be instantiated outside a function context.
  ConcurrencyCombinationSet SingleChannelConcurrencyCombinations{(
      struct ConcurrencyCombination){
      .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                     .max = 1},
                 (struct IfaceLimit){
                     .iftypes = {NL80211_IFTYPE_AP, NL80211_IFTYPE_P2P_CLIENT,
                                 NL80211_IFTYPE_P2P_GO},
                     .max = 1,
                 },
                 (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                     .max = 1}},
      .max_num = 3,
      .num_channels = 1}};
  AssertPhyConcurrencyIsEqualTo(SingleChannelConcurrencyCombinations);
  AssertApStaConcurrency(true);
}

TEST_F(WiFiPhyTest, ParseConcurrencyMultiChannel) {
  NewWiphyMessage msg;
  net_base::NetlinkPacket packet(kNewMultiChannelConcurrencyNlMsg);
  msg.InitFromPacketWithContext(&packet, Nl80211Message::Context());
  ParseConcurrency(msg);
  AssertConcurrencySorted();

  // These values align with those from kNewMultiChannelConcurrencyNlMsg. They
  // must be declared inline because the |nl80211_iftype|s are C values
  // which can't be instantiated outside a function context.
  ConcurrencyCombinationSet MultiChannelConcurrencyCombinations{
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 2},
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_AP,
                                     NL80211_IFTYPE_P2P_CLIENT,
                                     NL80211_IFTYPE_P2P_GO},
                         .max = 2,
                     },
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                         .max = 1}},
          .max_num = 4,
          .num_channels = 1},
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 2},
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_P2P_CLIENT},
                         .max = 2,
                     },
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_AP, NL80211_IFTYPE_P2P_GO},
                         .max = 1,
                     },
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                         .max = 1}},
          .max_num = 4,
          .num_channels = 2},
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 1},
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_ADHOC},
                                         .max = 1}},
          .max_num = 2,
          .num_channels = 1},
  };
  AssertPhyConcurrencyIsEqualTo(MultiChannelConcurrencyCombinations);
  AssertApStaConcurrency(true);
}

TEST_F(WiFiPhyTest, SelectFrequency_Empty) {
  WiFiPhy::Frequencies frequencies;

  WiFiBand band = WiFiBand::kLowBand;
  auto freq = wifi_phy_.SelectFrequency(band);
  EXPECT_FALSE(freq.has_value());
  band = WiFiBand::kHighBand;
  freq = wifi_phy_.SelectFrequency(band);
  EXPECT_FALSE(freq.has_value());
  band = WiFiBand::kAllBands;
  freq = wifi_phy_.SelectFrequency(band);
  EXPECT_FALSE(freq.has_value());
}

TEST_F(WiFiPhyTest, SelectFrequency_NoValidHB) {
  WiFiPhy::Frequencies frequencies = {
      {0,
       {
           {.value = 2412},  // Channel 1
           {.value = 2417},  // Channel 2
           {.value = 2422},  // Channel 3
           {.value = 2467},  // Channel 12
           {.value = 2472},  // Channel 13
       }},
      {1,
       {
           {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR, .value = 5200},
           {.flags = 1 << NL80211_FREQUENCY_ATTR_RADAR, .value = 5300},
       }}};
  SetFrequencies(frequencies);
  auto freq = wifi_phy_.SelectFrequency(WiFiBand::kAllBands);
  EXPECT_TRUE(freq.has_value());
  EXPECT_GE(freq, kLBStartFreq);
  EXPECT_LE(freq, kChan11Freq);  // Should avoid channel greater than channel 11
  EXPECT_TRUE(base::Contains(frequencies[0], uint32_t(freq.value()),
                             [](auto& f) { return f.value; }));
}

TEST_F(WiFiPhyTest, SelectFrequency_DualBandsAvailable) {
  WiFiPhy::Frequencies frequencies = {
      {0,
       {
           {.value = 2412},  // Channel 1
           {.value = 2417},  // Channel 2
           {.value = 2422},  // Channel 3
           {.value = 2467},  // Channel 12
           {.value = 2472},  // Channel 13
       }},
      {1,
       {
           {.value = 5180},  // Channel 36
           {.value = 5200},  // Channel 40
           {.value = 5220},  // Channel 44
           {.value = 5240},  // Channel 48
           {.flags = 1 << NL80211_FREQUENCY_ATTR_RADAR,
            .value = 5260},  // Channel 52
           {.flags = 1 << NL80211_FREQUENCY_ATTR_RADAR,
            .value = 5280},  // Channel 56
           {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
            .value = 5300},  // Channel 60
           {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
            .value = 5320},  // Channel 64
           {.flags = 1 << NL80211_FREQUENCY_ATTR_DISABLED,
            .value = 5340},  // Channel 68
           {.flags = 1 << NL80211_FREQUENCY_ATTR_DISABLED,
            .value = 5360},  // Channel 72
       }}};

  SetFrequencies(frequencies);
  WiFiBand band = WiFiBand::kLowBand;
  auto freq = wifi_phy_.SelectFrequency(band);
  EXPECT_TRUE(freq.has_value());
  EXPECT_GE(freq, kLBStartFreq);
  EXPECT_LE(freq, kChan11Freq);  // Should avoid channel greater than channel 11
  EXPECT_TRUE(base::Contains(frequencies[WiFiBandToNl(band)],
                             uint32_t(freq.value()),
                             [](auto& f) { return f.value; }));

  band = WiFiBand::kHighBand;
  freq = wifi_phy_.SelectFrequency(band);
  EXPECT_TRUE(freq.has_value());
  EXPECT_GE(freq, kHBStartFreq);
  EXPECT_LE(freq, kHBEndFreq);
  EXPECT_TRUE(base::Contains(frequencies[WiFiBandToNl(band)],
                             uint32_t(freq.value()),
                             [](auto& f) { return f.value; }));

  // For other preferences the selected frequency should be in 2.4 or 5GHz,
  // however with a valid 5GHz frequency it should be preferred.
  band = WiFiBand::kAllBands;
  freq = wifi_phy_.SelectFrequency(band);
  EXPECT_TRUE(freq.has_value());
  EXPECT_GE(freq, kHBStartFreq);
  EXPECT_LE(freq, kHBEndFreq);
  EXPECT_TRUE(base::Contains(frequencies[WiFiBandToNl(WiFiBand::kHighBand)],
                             uint32_t(freq.value()),
                             [](auto& f) { return f.value; }));
}

TEST_F(WiFiPhyTest, GetFrequencies) {
  WiFiPhy::Frequencies frequencies = {
      {0,
       {
           {.value = 2412},  // Channel 1
           {.value = 2417},  // Channel 2
           {.value = 2467},  // Channel 12
       }},
      {1,
       {
           {.value = 5180},  // Channel 36
           {.flags = 1 << NL80211_FREQUENCY_ATTR_RADAR,
            .value = 5260},  // Channel 52
           {.flags = 1 << NL80211_FREQUENCY_ATTR_NO_IR,
            .value = 5300},  // Channel 60
           {.flags = 1 << NL80211_FREQUENCY_ATTR_DISABLED,
            .value = 5340},  // Channel 68
           {.value = 5865},  // Channel 173
       }}};

  SetFrequencies(frequencies);
  auto freqs = wifi_phy_.GetFrequencies();
  EXPECT_FALSE(freqs.empty());
  EXPECT_TRUE(base::Contains(freqs, 2412));   // Channel 1
  EXPECT_TRUE(base::Contains(freqs, 2417));   // Channel 2
  EXPECT_FALSE(base::Contains(freqs, 2467));  // Channel 12, skip
  EXPECT_TRUE(base::Contains(freqs, 5180));   // Channel 36
  EXPECT_FALSE(base::Contains(freqs, 5260));  // Channel 52, RADAR
  EXPECT_FALSE(base::Contains(freqs, 5300));  // Channel 60, NO_IR
  EXPECT_FALSE(base::Contains(freqs, 5340));  // Channel 68, DISABLED
  EXPECT_FALSE(base::Contains(freqs, 5865));  // Channel 173, U-NII-4
}

TEST_F(WiFiPhyTest, ValidPriority) {
  for (int i = 0;
       i < static_cast<int32_t>(WiFiInterfacePriority::NUM_PRIORITIES); i++) {
    EXPECT_TRUE(WiFiPhy::Priority(i).IsValid())
        << i << " should be a vaild priority";
  }
  EXPECT_FALSE(WiFiPhy::Priority(
                   static_cast<int32_t>(WiFiInterfacePriority::NUM_PRIORITIES))
                   .IsValid())
      << static_cast<int32_t>(WiFiInterfacePriority::NUM_PRIORITIES)
      << " should be an invaild priority";
  EXPECT_FALSE(WiFiPhy::Priority(-1).IsValid())
      << -1 << " should be an invaild priority";
}

TEST_F(WiFiPhyTest, IfaceSorted) {
  WiFiPhy::RemovalCandidate c = {};
  c.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(0)});
  c.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(4)});
  c.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(3)});
  c.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(2)});
  c.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)});
  c.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)});

  auto iface = c.begin();
  auto iface_next = c.begin();
  iface_next++;
  while (iface_next != c.end()) {
    ASSERT_TRUE(iface->priority >= iface_next->priority);
    iface++;
    iface_next++;
  }
}

TEST_F(WiFiPhyTest, RemovalCandidateSet) {
  // Empty candidate is most preferable.
  std::vector<WiFiPhy::RemovalCandidate> expected_order = {};
  WiFiPhy::RemovalCandidate a = {};
  expected_order.push_back(a);

  // Less preferable than a because we have a additional interface.
  WiFiPhy::RemovalCandidate b = {};
  b.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)});
  expected_order.push_back(b);

  // Less preferable than a because we have a additional interface at the same
  // priority.
  WiFiPhy::RemovalCandidate c = {};
  c.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)});
  c.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)});
  expected_order.push_back(c);

  // Less preferable than c because despite having fewer interfaces, the
  // existing interface is higher priority.
  WiFiPhy::RemovalCandidate d = {};
  d.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(2)});
  expected_order.push_back(d);

  // Less preferable than d because we have an extra entry at a lower
  // priority than the maximum.
  WiFiPhy::RemovalCandidate e = {};
  e.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)});
  e.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(2)});
  expected_order.push_back(e);

  // Less preferable than f because we have an extra entry at the highest
  // priority.
  WiFiPhy::RemovalCandidate f = {};
  f.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(2)});
  f.insert({NL80211_IFTYPE_STATION, WiFiPhy::Priority(2)});
  expected_order.push_back(f);

  // Try inserting the candidates in the reverse of the expected order.
  WiFiPhy::RemovalCandidateSet reverse_candidates = {};
  reverse_candidates.insert(f);
  reverse_candidates.insert(e);
  reverse_candidates.insert(d);
  reverse_candidates.insert(c);
  reverse_candidates.insert(b);
  reverse_candidates.insert(a);
  AssertRemovalCandidateSetOrder(reverse_candidates, expected_order);

  // Try inserting the candidates in the expected order.
  WiFiPhy::RemovalCandidateSet ordered_candidates = {};
  ordered_candidates.insert(a);
  ordered_candidates.insert(b);
  ordered_candidates.insert(c);
  ordered_candidates.insert(d);
  ordered_candidates.insert(e);
  ordered_candidates.insert(f);
  AssertRemovalCandidateSetOrder(ordered_candidates, expected_order);

  // Try inserting the candidates in an arbitrary order.
  WiFiPhy::RemovalCandidateSet arbitrary_candidates = {};
  arbitrary_candidates.insert(c);
  arbitrary_candidates.insert(a);
  arbitrary_candidates.insert(f);
  arbitrary_candidates.insert(d);
  arbitrary_candidates.insert(b);
  arbitrary_candidates.insert(e);
  AssertRemovalCandidateSetOrder(arbitrary_candidates, expected_order);
}

TEST_F(WiFiPhyTest, SupportsConcurrency) {
  // These values align with those from kNewMultiChannelConcurrencyNlMsg. They
  // must be declared inline because the |nl80211_iftype|s are C values
  // which can't be instantiated outside a function context.

  wifi_phy_.concurrency_combs_ = {
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 2},
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_AP,
                                     NL80211_IFTYPE_P2P_CLIENT,
                                     NL80211_IFTYPE_P2P_GO},
                         .max = 2,
                     },
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                         .max = 1}},
          .max_num = 4,
          .num_channels = 1},
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 1},
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_P2P_CLIENT},
                         .max = 2,
                     },
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_AP, NL80211_IFTYPE_P2P_GO},
                         .max = 1,
                     },
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                         .max = 1}},
          .max_num = 4,
          .num_channels = 2},
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 1},
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_ADHOC},
                                         .max = 1}},
          .max_num = 2,
          .num_channels = 3},
  };

  // Supported by all combs, so we should pick the comb with the most channels.
  EXPECT_EQ(3, SupportsConcurrency({NL80211_IFTYPE_STATION}));

  // Supported by two combs, so we should pick the remaining comb with the most
  // channels.
  EXPECT_EQ(2, SupportsConcurrency({NL80211_IFTYPE_AP}));
  EXPECT_EQ(2,
            SupportsConcurrency({NL80211_IFTYPE_STATION, NL80211_IFTYPE_AP}));
  EXPECT_EQ(
      2, SupportsConcurrency({NL80211_IFTYPE_STATION, NL80211_IFTYPE_P2P_CLIENT,
                              NL80211_IFTYPE_P2P_CLIENT}));

  // Supported by only the comb with fewest channels.
  EXPECT_EQ(
      1, SupportsConcurrency({NL80211_IFTYPE_STATION, NL80211_IFTYPE_STATION}));
  EXPECT_EQ(1, SupportsConcurrency({NL80211_IFTYPE_AP, NL80211_IFTYPE_AP}));
  EXPECT_EQ(
      1, SupportsConcurrency({NL80211_IFTYPE_AP, NL80211_IFTYPE_AP,
                              NL80211_IFTYPE_STATION, NL80211_IFTYPE_STATION}));

  // Too many interfaces of a given type to be supported by any comb.
  EXPECT_EQ(0,
            SupportsConcurrency({NL80211_IFTYPE_STATION, NL80211_IFTYPE_STATION,
                                 NL80211_IFTYPE_STATION}));
  EXPECT_EQ(0, SupportsConcurrency({NL80211_IFTYPE_AP, NL80211_IFTYPE_AP,
                                    NL80211_IFTYPE_P2P_CLIENT}));

  // All the interfaces are supported by individual limits, but too may total
  // interfaces to fit inside max_num of any comb.
  EXPECT_EQ(0,
            SupportsConcurrency({NL80211_IFTYPE_AP, NL80211_IFTYPE_AP,
                                 NL80211_IFTYPE_STATION, NL80211_IFTYPE_STATION,
                                 NL80211_IFTYPE_P2P_DEVICE}));
}

TEST_F(WiFiPhyTest, InterfaceCombinations_LowPriorityRequest) {
  ConcurrencyCombinationSet combs = {
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 2},
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_P2P_CLIENT},
                         .max = 2,
                     },
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_AP, NL80211_IFTYPE_P2P_GO},
                         .max = 1,
                     },
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                         .max = 1}},
          .max_num = 4,
          .num_channels = 3},
  };

  std::vector<ConcurrencyTestCase> test_cases = {
      // 1 + 1 combinations.

      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      // 2 + 1 combinations.
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
       std::nullopt},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
  };

  TestInterfaceCombinations(test_cases, combs);
}

TEST_F(WiFiPhyTest, InterfaceCombinations_HighPriorityRequest) {
  ConcurrencyCombinationSet combs = {
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 2},
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_P2P_CLIENT},
                         .max = 2,
                     },
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_AP, NL80211_IFTYPE_P2P_GO},
                         .max = 1,
                     },
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                         .max = 1}},
          .max_num = 4,
          .num_channels = 3},
  };

  std::vector<ConcurrencyTestCase> test_cases = {
      // 1 + 1 combinations.

      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_AP}},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_AP}},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_P2P_GO}},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_P2P_GO}},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      // 2 + 1 combinations.
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_AP}},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_AP}},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_P2P_GO}},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_P2P_GO}},
      {{{NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_AP}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_AP}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},

      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_P2P_GO}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_P2P_GO}},
      {{{NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_P2P_GO, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
  };

  TestInterfaceCombinations(test_cases, combs);
}

TEST_F(WiFiPhyTest, InterfaceCombinations_MultipleCombs) {
  ConcurrencyCombinationSet combs = {
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 1},
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_AP,
                                     NL80211_IFTYPE_P2P_CLIENT,
                                     NL80211_IFTYPE_P2P_GO},
                         .max = 2,
                     },
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                         .max = 1}},
          .max_num = 3,
          .num_channels = 3},
      (struct ConcurrencyCombination){
          .limits = {(struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                         .max = 2},
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_P2P_CLIENT},
                         .max = 2,
                     },
                     (struct IfaceLimit){
                         .iftypes = {NL80211_IFTYPE_AP, NL80211_IFTYPE_P2P_GO},
                         .max = 1,
                     },
                     (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_P2P_DEVICE},
                                         .max = 1}},
          .max_num = 2,
          .num_channels = 2},
  };

  std::vector<ConcurrencyTestCase> test_cases = {
      // Only possible using the first comb.
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(1)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(5)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},

      // Only possible using the second comb.
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)},
       std::multiset<nl80211_iftype>{}},
      {{{NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{}},

      // The current configuration is only supported by the first comb, but the
      // desired configuration is only supported by the second comb.
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(2)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(2)},
        {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_AP, NL80211_IFTYPE_AP}},

      // AP interface has higher priority, but we take it down because taking
      // down the STA doesn't work.
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(2)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(2)},
        {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)}},
       {NL80211_IFTYPE_P2P_CLIENT, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_AP}},

      // Take down 2 lower priority interfaces instead of 1 with higher
      // priority.
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(2)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(2)},
        {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_AP, NL80211_IFTYPE_AP}},

      // Take down 1 interface instead of 2 with the same priority.
      {{{NL80211_IFTYPE_AP, WiFiPhy::Priority(2)},
        {NL80211_IFTYPE_AP, WiFiPhy::Priority(2)},
        {NL80211_IFTYPE_STATION, WiFiPhy::Priority(2)}},
       {NL80211_IFTYPE_STATION, WiFiPhy::Priority(5)},
       std::multiset<nl80211_iftype>{NL80211_IFTYPE_STATION}},
  };

  TestInterfaceCombinations(test_cases, combs);
}

TEST_F(WiFiPhyTest, GetAllCandidates) {
  WiFiPhy::ConcurrentIface a = {NL80211_IFTYPE_STATION, WiFiPhy::Priority(2)};
  WiFiPhy::ConcurrentIface b = {NL80211_IFTYPE_STATION, WiFiPhy::Priority(3)};
  WiFiPhy::ConcurrentIface c = {NL80211_IFTYPE_STATION, WiFiPhy::Priority(2)};
  WiFiPhy::ConcurrentIface d = {NL80211_IFTYPE_STATION, WiFiPhy::Priority(1)};

  std::vector<WiFiPhy::ConcurrentIface> ifaces = {a, b, c, d};

  WiFiPhy::RemovalCandidateSet candidates = GetAllCandidates(ifaces);

  std::vector<WiFiPhy::RemovalCandidate> expected_order;
  expected_order.push_back({{}});
  expected_order.push_back({{d}});
  expected_order.push_back({{c}});
  expected_order.push_back({{a}});
  expected_order.push_back({{c}, {d}});
  expected_order.push_back({{a}, {d}});
  expected_order.push_back({{c}, {a}});
  expected_order.push_back({{c}, {a}, {d}});
  expected_order.push_back({{b}});
  expected_order.push_back({{d}, {b}});
  expected_order.push_back({{c}, {b}});
  expected_order.push_back({{a}, {b}});
  expected_order.push_back({{c}, {d}, {b}});
  expected_order.push_back({{a}, {d}, {b}});
  expected_order.push_back({{c}, {a}, {b}});
  expected_order.push_back({{c}, {a}, {d}, {b}});

  AssertRemovalCandidateSetOrder(candidates, expected_order);
}
TEST_F(WiFiPhyTest, GetAllCandidates_empty) {
  std::vector<WiFiPhy::ConcurrentIface> ifaces = {};
  WiFiPhy::RemovalCandidateSet candidates = GetAllCandidates(ifaces);
  std::vector<WiFiPhy::RemovalCandidate> expected_order;
  expected_order.push_back({{}});
  AssertRemovalCandidateSetOrder(candidates, expected_order);
}

TEST_F(WiFiPhyTest, AddDefaultCombinationForType) {
  ConcurrencyCombinationSet defaultCombinationsForAPAndSTA = {
      (struct ConcurrencyCombination){
          .limits =
              {
                  (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_AP}, .max = 1},
              },
          .max_num = 1,
          .num_channels = 1},
      (struct ConcurrencyCombination){
          .limits =
              {
                  (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_STATION},
                                      .max = 1},
              },
          .max_num = 1,
          .num_channels = 1}};

  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 0);
  wifi_phy_.AddDefaultCombinationForType(NL80211_IFTYPE_AP);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 1);
  wifi_phy_.AddDefaultCombinationForType(NL80211_IFTYPE_AP);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 1);
  wifi_phy_.AddDefaultCombinationForType(NL80211_IFTYPE_STATION);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 2);
  wifi_phy_.AddDefaultCombinationForType(NL80211_IFTYPE_STATION);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 2);

  AssertPhyConcurrencyIsEqualTo(defaultCombinationsForAPAndSTA);
}

TEST_F(WiFiPhyTest, AddDefaultCombinationForType_SameTypeDifferentLimit) {
  // Ensure the default combination is still added, even if a different
  // combination including the same interface type already exists.
  struct ConcurrencyCombination comb = {
      .limits =
          {
              (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_AP}, .max = 2},
          },
      .max_num = 1,
      .num_channels = 1};
  wifi_phy_.concurrency_combs_.insert(comb);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 1);
  wifi_phy_.AddDefaultCombinationForType(NL80211_IFTYPE_AP);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 2);
}

TEST_F(WiFiPhyTest, AddDefaultCombinationForType_SameTypeDifferentMax) {
  // Ensure the default combination is still added, even if a different
  // combination including the same interface type already exists.
  struct ConcurrencyCombination comb = {
      .limits =
          {
              (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_AP}, .max = 1},
          },
      .max_num = 2,
      .num_channels = 1};
  wifi_phy_.concurrency_combs_.insert(comb);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 1);
  wifi_phy_.AddDefaultCombinationForType(NL80211_IFTYPE_AP);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 2);
}

TEST_F(WiFiPhyTest, AddDefaultCombinationForType_SameTypeDifferentChannels) {
  // Ensure the default combination is still added, even if a different
  // combination including the same interface type already exists.
  struct ConcurrencyCombination comb = {
      .limits =
          {
              (struct IfaceLimit){.iftypes = {NL80211_IFTYPE_AP}, .max = 1},
          },
      .max_num = 1,
      .num_channels = 2};
  wifi_phy_.concurrency_combs_.insert(comb);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 1);
  wifi_phy_.AddDefaultCombinationForType(NL80211_IFTYPE_AP);
  ASSERT_EQ(wifi_phy_.concurrency_combs_.size(), 2);
}

}  // namespace shill
