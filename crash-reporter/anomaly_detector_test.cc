// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/anomaly_detector.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/anomaly_detector_test_utils.h"
#include "crash-reporter/util.h"

namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Return;

using ::anomaly::CryptohomeParser;
using ::anomaly::DlcServiceParser;
using ::anomaly::HermesParser;
using ::anomaly::KernelParser;
using ::anomaly::ModemfwdParser;
using ::anomaly::ParserRun;
using ::anomaly::ParserTest;
using ::anomaly::SELinuxParser;
using ::anomaly::ServiceParser;
using ::anomaly::SessionManagerParser;
using ::anomaly::ShillParser;
using ::anomaly::SuspendParser;
using ::anomaly::TcsdParser;
using ::anomaly::TerminaParser;

const ParserRun simple_run;
const ParserRun empty{.expected_size = 0};

}  // namespace

TEST(AnomalyDetectorTest, KernelAth10kSNOCError) {
  ParserRun wifi_error = {
      .expected_text =
          "[393652.069986] ath10k_snoc 18800000.wifi: "
          "firmware crashed! (guid 7c8da1e6-f8fe-4665-8257-5a476a7bc786)\n"
          "[393652.070050] ath10k_snoc 18800000.wifi: "
          "wcn3990 hw1.0 target 0x00000008 chip_id 0x00000000 sub 0000:0000\n"
          "[393652.070086] ath10k_snoc 18800000.wifi: "
          "kconfig debug 1 debugfs 1 tracing 0 dfs 0 testmode 1\n"
          "[393652.070124] ath10k_snoc 18800000.wifi: "
          "firmware ver 1.0.0.922 api 5 features "
          "wowlan,mfp,mgmt-tx-by-reference"
          ",non-bmi,single-chan-info-per-channel crc32 3f19f7c1\n"
          "[393652.070158] ath10k_snoc 18800000.wifi: "
          "board_file api 2 bmi_id N/A crc32 00000000\n"
          "[393652.070195] ath10k_snoc 18800000.wifi: "
          "htt-ver 3.86 wmi-op 4 htt-op 3 cal file max-sta 32 raw 0 hwcrypto "
          "1\n",
      .expected_flags = {{"--kernel_ath10k_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_ATH10K_SNOC", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelAth10kSDIOError) {
  ParserRun wifi_error = {
      .expected_text =
          "[10108611.994407] ath10k_sdio mmc1:0001:1: "
          "firmware crashed! (guid bfc44e6c-4cef-425b-b9ca-5530c650d0a3)\n"
          "[10108611.994442] ath10k_sdio mmc1:0001:1: "
          "qca6174 hw3.2 sdio target 0x05030000 chip_id 0x00000000 sub "
          "0000:0000\n"
          "[10108611.994457] ath10k_sdio mmc1:0001:1: "
          "kconfig debug 1 debugfs 1 tracing 1 dfs 0 testmode 1\n"
          "[10108611.996680] ath10k_sdio mmc1:0001:1: "
          "firmware ver WLAN.RMH.4.4.1-00077 api 6 features wowlan,ignore-otp "
          "crc32 a48b7c2f\n"
          "[10108611.999858] ath10k_sdio mmc1:0001:1: "
          "board_file api 2 bmi_id 0:4 crc32 fe1026b8\n"
          "[10108611.999887] ath10k_sdio mmc1:0001:1: "
          "htt-ver 3.86 wmi-op 4 htt-op 3 cal otp max-sta 32 raw 0 hwcrypto "
          "1\n",
      .expected_flags = {{"--kernel_ath10k_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_ATH10K_SDIO", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelAth10kPCIError) {
  ParserRun wifi_error = {
      .expected_text =
          "[ 1582.994118] ath10k_pci 0000:01:00.0: "
          "firmware crashed! (guid cad1f078-23d2-4cfe-a58a-1e9d353acb7e)\n"
          "[ 1582.994133] ath10k_pci 0000:01:00.0: "
          "qca6174 hw3.2 target 0x05030000 chip_id 0x00340aff sub 17aa:0827\n"
          "[ 1582.994141] ath10k_pci 0000:01:00.0: "
          "kconfig debug 1 debugfs 1 tracing 1 dfs 0 testmode 1\n"
          "[ 1582.995552] ath10k_pci 0000:01:00.0: "
          "firmware ver WLAN.RM.4.4.1-00157-QCARMSWPZ-1 api 6 features "
          "wowlan,ignore-otp,mfp crc32 90eebefb\n"
          "[ 1582.996924] ath10k_pci 0000:01:00.0: "
          "board_file api 2 bmi_id N/A crc32 bebf3597\n"
          "[ 1582.996936] ath10k_pci 0000:01:00.0: "
          "htt-ver 3.60 wmi-op 4 htt-op 3 cal otp max-sta 32 raw 0 hwcrypto "
          "1\n",
      .expected_flags = {{"--kernel_ath10k_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_ATH10K_PCI", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelAth10kErrorNoEnd) {
  ParserRun wifi_error = {
      .expected_text =
          "[393652.069986] ath10k_snoc 18800000.wifi: firmware crashed! "
          "(guid 7c8da1e6-f8fe-4665-8257-5a476a7bc786)\n"
          "[393652.070050] ath10k_snoc 18800000.wifi: wcn3990 hw1.0 target "
          "0x00000008 chip_id 0x00000000 sub 0000:0000\n"
          "[393652.070086] ath10k_snoc 18800000.wifi: kconfig debug 1 debugfs "
          "1 tracing 0 dfs 0 testmode 1\n"
          "[393652.070124] ath10k_snoc 18800000.wifi: firmware ver 1.0.0.922 "
          "api 5 features wowlan,mfp,mgmt-tx-by-reference,non-bmi,single-chan"
          "-info-per-channel crc32 3f19f7c1\n"
          "[393652.070158] ath10k_snoc 18800000.wifi: board_file api 2 bmi_id"
          " N/A crc32 00000000\n",
      .expected_flags = {{"--kernel_ath10k_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_ATH10K_NO_END", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelAth11kError) {
  ParserRun wifi_error = {
      .expected_text =
          "[   88.311695] ath11k_pci 0000:01:00.0: firmware crashed:"
          " MHI_CB_EE_RDDM\n"
          "[   88.324206] ieee80211 phy0: Hardware restart was requested\n"
          "[   88.655410] mhi mhi0: Requested to power ON\n"
          "[   88.655549] mhi mhi0: Power on setup success\n"
          "[   89.006232] mhi mhi0: Wait for device to enter SBL"
          " or Mission mode\n"
          "[   89.636634] ath11k_pci 0000:01:00.0: chip_id "
          "0x12 chip_family 0xb board_id 0xff soc_id 0x400c1211\n"
          "[   89.636640] ath11k_pci 0000:01:00.0: fw_version 0x110b196e"
          " fw_build_timestamp 2022-12-22 12:54 fw_build_id "
          "QC_IMAGE_VERSION_STRING=WLAN.HSP.1.1-03125-"
          "QCAHSPSWPL_V1_V2_SILICONZ_LITE-3.6510.23\n",
      .expected_flags = {{"--kernel_ath11k_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_ATH11K", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelAth11WrongTag) {
  ParserRun wifi_error = {
      .expected_text =
          "[   72.930992] ath11k_pci 0000:01:00.0: firmware crashed: "
          "MHI_CB_EE_RDDM\n"
          "[   72.940206] ieee80211 phy0: Hardware restart was requested\n"
          "[   73.272757] mhi mhi0: Requested to power ON\n"
          "[   73.272872] mhi mhi0: Power on setup success\n"
          "[   73.624227] mhi mhi0: Wait for device to enter SBL or Mission "
          "mode\n"
          "[   74.255350] ath11k_pci 0000:01:00.0: chip_id 0x12 chip_family "
          "0xb board_id 0xff soc_id 0x400c1211\n",
      .expected_flags = {{"--kernel_ath11k_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_ATH11K_WRONG_TAG", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelAth11ErrorNoEnd) {
  ParserRun wifi_error = {
      .expected_text =
          "[   88.311695] ath11k_pci 0000:01:00.0: firmware crashed:"
          " MHI_CB_EE_RDDM\n"
          "[   88.324206] ieee80211 phy0: Hardware restart was requested\n"
          "[   88.655410] mhi mhi0: Requested to power ON\n"
          "[   88.655549] mhi mhi0: Power on setup success\n"
          "[   89.006232] mhi mhi0: Wait for device to enter SBL"
          " or Mission mode\n"
          "[   89.636634] ath11k_pci 0000:01:00.0: chip_id "
          "0x12 chip_family 0xb board_id 0xff soc_id 0x400c1211\n"
          "[   89.674574] ath11k_pci 0000:01:00.0: "
          "Last interrupt received for each CE:\n"
          "[   89.674580] ath11k_pci 0000:01:00.0: "
          "CE_id 0 pipe_num 0 76901ms before\n"
          "[   89.674582] ath11k_pci 0000:01:00.0: "
          "CE_id 1 pipe_num 1 76661ms before\n"
          "[   89.674583] ath11k_pci 0000:01:00.0: "
          "CE_id 2 pipe_num 2 1781ms before\n",
      .expected_flags = {{"--kernel_ath11k_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_ATH11K_NO_END", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelIwlwifiErrorLmacUmac) {
  ParserRun wifi_error = {
      .expected_text =
          "[15883.337352] iwlwifi 0000:00:0c.0: Loaded firmware version: "
          "46.b20aefee.0\n"
          "[15883.337355] iwlwifi 0000:00:0c.0: 0x00000084 | "
          "NMI_INTERRUPT_UNKNOWN\n"
          "[15883.337357] iwlwifi 0000:00:0c.0: 0x000022F0 | trm_hw_status0\n"
          "[15883.337359] iwlwifi 0000:00:0c.0: 0x00000000 | trm_hw_status1\n"
          "[15883.337362] iwlwifi 0000:00:0c.0: 0x0048751E | branchlink2\n"
          "[15883.337364] iwlwifi 0000:00:0c.0: 0x00479236 | interruptlink1\n"
          "[15883.337366] iwlwifi 0000:00:0c.0: 0x0000AE00 | interruptlink2\n"
          "[15883.337369] iwlwifi 0000:00:0c.0: 0x0001A2D6 | data1\n"
          "[15883.337371] iwlwifi 0000:00:0c.0: 0xFF000000 | data2\n"
          "[15883.337373] iwlwifi 0000:00:0c.0: 0xF0000000 | data3\n"
          "[15883.337376] iwlwifi 0000:00:0c.0: 0x00000000 | beacon time\n"
          "[15883.337378] iwlwifi 0000:00:0c.0: 0x158DE6F7 | tsf low\n"
          "[15883.337380] iwlwifi 0000:00:0c.0: 0x00000000 | tsf hi\n"
          "[15883.337383] iwlwifi 0000:00:0c.0: 0x00000000 | time gp1\n"
          "[15883.337385] iwlwifi 0000:00:0c.0: 0x158DE6F9 | time gp2\n"
          "[15883.337388] iwlwifi 0000:00:0c.0: 0x00000001 | uCode revision "
          "type\n"
          "[15883.337390] iwlwifi 0000:00:0c.0: 0x0000002E | uCode version "
          "major\n"
          "[15883.337392] iwlwifi 0000:00:0c.0: 0xB20AEFEE | uCode version "
          "minor\n"
          "[15883.337394] iwlwifi 0000:00:0c.0: 0x00000312 | hw version\n"
          "[15883.337397] iwlwifi 0000:00:0c.0: 0x00C89008 | board version\n"
          "[15883.337399] iwlwifi 0000:00:0c.0: 0x007B019C | hcmd\n"
          "[15883.337401] iwlwifi 0000:00:0c.0: 0x00022000 | isr0\n"
          "[15883.337404] iwlwifi 0000:00:0c.0: 0x00000000 | isr1\n"
          "[15883.337406] iwlwifi 0000:00:0c.0: 0x08001802 | isr2\n"
          "[15883.337408] iwlwifi 0000:00:0c.0: 0x40400180 | isr3\n"
          "[15883.337411] iwlwifi 0000:00:0c.0: 0x00000000 | isr4\n"
          "[15883.337413] iwlwifi 0000:00:0c.0: 0x007B019C | last cmd Id\n"
          "[15883.337415] iwlwifi 0000:00:0c.0: 0x0001A2D6 | wait_event\n"
          "[15883.337417] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_control\n"
          "[15883.337420] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_duration\n"
          "[15883.337422] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_mhvalid\n"
          "[15883.337424] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_addr_match\n"
          "[15883.337427] iwlwifi 0000:00:0c.0: 0x0000008F | lmpm_pmg_sel\n"
          "[15883.337429] iwlwifi 0000:00:0c.0: 0x24021230 | timestamp\n"
          "[15883.337432] iwlwifi 0000:00:0c.0: 0x0000B0D8 | flow_handler\n"
          "[15883.337464] iwlwifi 0000:00:0c.0: Start IWL Error Log Dump:\n"
          "[15883.337467] iwlwifi 0000:00:0c.0: Transport status: "
          "0x00000100, count: 7\n"
          "[15883.337470] iwlwifi 0000:00:0c.0: 0x20000066 | "
          "NMI_INTERRUPT_HOST\n"
          "[15883.337472] iwlwifi 0000:00:0c.0: 0x00000000 | umac branchlink1\n"
          "[15883.337475] iwlwifi 0000:00:0c.0: 0xC008821A | umac branchlink2\n"
          "[15883.337477] iwlwifi 0000:00:0c.0: 0x00000000 | umac "
          "interruptlink1\n"
          "[15883.337479] iwlwifi 0000:00:0c.0: 0x8044FBD2 | umac "
          "interruptlink2\n"
          "[15883.337481] iwlwifi 0000:00:0c.0: 0x01000000 | umac data1\n"
          "[15883.337484] iwlwifi 0000:00:0c.0: 0x8044FBD2 | umac data2\n"
          "[15883.337486] iwlwifi 0000:00:0c.0: 0xDEADBEEF | umac data3\n"
          "[15883.337488] iwlwifi 0000:00:0c.0: 0x0000002E | umac major\n"
          "[15883.337490] iwlwifi 0000:00:0c.0: 0xB20AEFEE | umac minor\n"
          "[15883.337493] iwlwifi 0000:00:0c.0: 0x158DE6F4 | frame pointer\n"
          "[15883.337511] iwlwifi 0000:00:0c.0: 0xC088627C | stack pointer\n"
          "[15883.337514] iwlwifi 0000:00:0c.0: 0x007B019C | last host cmd\n"
          "[15883.337516] iwlwifi 0000:00:0c.0: 0x00000000 | isr status reg\n",
      .expected_flags = {{"--kernel_iwlwifi_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_IWLWIFI_LMAC_UMAC", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelIwlwifiErrorLmacTwoSpace) {
  ParserRun wifi_error = {
      .expected_text =
          "[79553.430924] iwlwifi 0000:02:00.0: Loaded firmware version: "
          "29.116a852a.0 7265D-29.ucode\n"
          "[79553.430930] iwlwifi 0000:02:00.0: 0x00000084 | "
          "NMI_INTERRUPT_UNKNOWN       \n"
          "[79553.430935] iwlwifi 0000:02:00.0: 0x00A002F0 | trm_hw_status0\n"
          "[79553.430939] iwlwifi 0000:02:00.0: 0x00000000 | trm_hw_status1\n"
          "[79553.430944] iwlwifi 0000:02:00.0: 0x00043D6C | branchlink2\n"
          "[79553.430948] iwlwifi 0000:02:00.0: 0x0004AFD6 | interruptlink1\n"
          "[79553.430953] iwlwifi 0000:02:00.0: 0x0004AFD6 | interruptlink2\n"
          "[79553.430957] iwlwifi 0000:02:00.0: 0x00000000 | data1\n"
          "[79553.430961] iwlwifi 0000:02:00.0: 0x00000080 | data2\n"
          "[79553.430966] iwlwifi 0000:02:00.0: 0x07230000 | data3\n"
          "[79553.430970] iwlwifi 0000:02:00.0: 0x1E00B95C | beacon time\n"
          "[79553.430975] iwlwifi 0000:02:00.0: 0xE6A38917 | tsf low\n"
          "[79553.430979] iwlwifi 0000:02:00.0: 0x00000011 | tsf hi\n"
          "[79553.430983] iwlwifi 0000:02:00.0: 0x00000000 | time gp1\n"
          "[79553.430988] iwlwifi 0000:02:00.0: 0x8540E3A4 | time gp2\n"
          "[79553.430992] iwlwifi 0000:02:00.0: 0x00000001 | uCode revision "
          "type\n"
          "[79553.430996] iwlwifi 0000:02:00.0: 0x0000001D | uCode version "
          "major\n"
          "[79553.431013] iwlwifi 0000:02:00.0: 0x116A852A | uCode version "
          "minor\n"
          "[79553.431017] iwlwifi 0000:02:00.0: 0x00000210 | hw version\n"
          "[79553.431021] iwlwifi 0000:02:00.0: 0x00489200 | board version\n"
          "[79553.431025] iwlwifi 0000:02:00.0: 0x0000001C | hcmd\n"
          "[79553.431030] iwlwifi 0000:02:00.0: 0x00022000 | isr0\n"
          "[79553.431034] iwlwifi 0000:02:00.0: 0x00000000 | isr1\n"
          "[79553.431039] iwlwifi 0000:02:00.0: 0x0000000A | isr2\n"
          "[79553.431043] iwlwifi 0000:02:00.0: 0x0041D4C0 | isr3\n"
          "[79553.431047] iwlwifi 0000:02:00.0: 0x00000000 | isr4\n"
          "[79553.431052] iwlwifi 0000:02:00.0: 0x00230151 | last cmd Id\n"
          "[79553.431056] iwlwifi 0000:02:00.0: 0x00000000 | wait_event\n"
          "[79553.431060] iwlwifi 0000:02:00.0: 0x0000A8CB | l2p_control\n"
          "[79553.431082] iwlwifi 0000:02:00.0: 0x00000020 | l2p_duration\n"
          "[79553.431086] iwlwifi 0000:02:00.0: 0x00000003 | l2p_mhvalid\n"
          "[79553.431091] iwlwifi 0000:02:00.0: 0x000000CE | l2p_addr_match\n"
          "[79553.431095] iwlwifi 0000:02:00.0: 0x00000005 | lmpm_pmg_sel\n"
          "[79553.431100] iwlwifi 0000:02:00.0: 0x07071159 | timestamp\n"
          "[79553.431104] iwlwifi 0000:02:00.0: 0x00340010 | flow_handler\n",
      .expected_flags = {{"--kernel_iwlwifi_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_IWLWIFI_LMAC_TWO_SPACE", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelIwlwifiDriverError) {
  ParserRun wifi_error = {
      .expected_text =
          "2020-09-01T11:03:11.221374-07:00 ERR kernel: [ 2448.183332] iwlwifi "
          "0000:01:00.0: Loaded firmware version: 17.bfb58538.0 7260-17.ucode\n"
          "2020-09-01T11:03:11.221401-07:00 ERR kernel: [ 2448.183344] iwlwifi "
          "0000:01:00.0: 0x00000000 | ADVANCED_SYSASSERT\n"
          "2020-09-01T11:03:11.221407-07:00 ERR kernel: [ 2448.183349] iwlwifi "
          "0000:01:00.0: 0x00000000 | trm_hw_status0\n"
          "2020-09-01T11:03:11.221409-07:00 ERR kernel: [ 2448.183353] iwlwifi "
          "0000:01:00.0: 0x00000000 | trm_hw_status1\n"
          "2020-09-01T11:03:11.221412-07:00 ERR kernel: [ 2448.183357] iwlwifi "
          "0000:01:00.0: 0x00000000 | branchlink2\n"
          "2020-09-01T11:03:11.221415-07:00 ERR kernel: [ 2448.183361] iwlwifi "
          "0000:01:00.0: 0x00000000 | interruptlink1\n"
          "2020-09-01T11:03:11.221417-07:00 ERR kernel: [ 2448.183365] iwlwifi "
          "0000:01:00.0: 0x00000000 | interruptlink2\n"
          "2020-09-01T11:03:11.221420-07:00 ERR kernel: [ 2448.183368] iwlwifi "
          "0000:01:00.0: 0x00000000 | data1\n"
          "2020-09-01T11:03:11.221422-07:00 ERR kernel: [ 2448.183372] iwlwifi "
          "0000:01:00.0: 0x00000000 | data2\n"
          "2020-09-01T11:03:11.221425-07:00 ERR kernel: [ 2448.183376] iwlwifi "
          "0000:01:00.0: 0x00000000 | data3\n"
          "2020-09-01T11:03:11.221427-07:00 ERR kernel: [ 2448.183380] iwlwifi "
          "0000:01:00.0: 0x00000000 | beacon time\n"
          "2020-09-01T11:03:11.221429-07:00 ERR kernel: [ 2448.183384] iwlwifi "
          "0000:01:00.0: 0x00000000 | tsf low\n"
          "2020-09-01T11:03:11.221432-07:00 ERR kernel: [ 2448.183388] iwlwifi "
          "0000:01:00.0: 0x00000000 | tsf hi\n"
          "2020-09-01T11:03:11.221434-07:00 ERR kernel: [ 2448.183392] iwlwifi "
          "0000:01:00.0: 0x00000000 | time gp1\n"
          "2020-09-01T11:03:11.221436-07:00 ERR kernel: [ 2448.183396] iwlwifi "
          "0000:01:00.0: 0x00000000 | time gp2\n"
          "2020-09-01T11:03:11.221438-07:00 ERR kernel: [ 2448.183400] iwlwifi "
          "0000:01:00.0: 0x00000000 | uCode revision type\n"
          "2020-09-01T11:03:11.221440-07:00 ERR kernel: [ 2448.183404] iwlwifi "
          "0000:01:00.0: 0x00000000 | uCode version major\n"
          "2020-09-01T11:03:11.221443-07:00 ERR kernel: [ 2448.183408] iwlwifi "
          "0000:01:00.0: 0x00000000 | uCode version minor\n"
          "2020-09-01T11:03:11.221445-07:00 ERR kernel: [ 2448.183412] iwlwifi "
          "0000:01:00.0: 0x00000000 | hw version\n"
          "2020-09-01T11:03:11.221447-07:00 ERR kernel: [ 2448.183416] iwlwifi "
          "0000:01:00.0: 0x00000000 | board version\n"
          "2020-09-01T11:03:11.221449-07:00 ERR kernel: [ 2448.183419] iwlwifi "
          "0000:01:00.0: 0x00000000 | hcmd\n"
          "2020-09-01T11:03:11.221451-07:00 ERR kernel: [ 2448.183423] iwlwifi "
          "0000:01:00.0: 0x00000000 | isr0\n"
          "2020-09-01T11:03:11.221453-07:00 ERR kernel: [ 2448.183427] iwlwifi "
          "0000:01:00.0: 0x00000000 | isr1\n"
          "2020-09-01T11:03:11.221455-07:00 ERR kernel: [ 2448.183431] iwlwifi "
          "0000:01:00.0: 0x00000000 | isr2\n"
          "2020-09-01T11:03:11.221457-07:00 ERR kernel: [ 2448.183435] iwlwifi "
          "0000:01:00.0: 0x00000000 | isr3\n"
          "2020-09-01T11:03:11.221459-07:00 ERR kernel: [ 2448.183439] iwlwifi "
          "0000:01:00.0: 0x00000000 | isr4\n"
          "2020-09-01T11:03:11.221461-07:00 ERR kernel: [ 2448.183442] iwlwifi "
          "0000:01:00.0: 0x00000000 | last cmd Id\n"
          "2020-09-01T11:03:11.221464-07:00 ERR kernel: [ 2448.183446] iwlwifi "
          "0000:01:00.0: 0x00000000 | wait_event\n"
          "2020-09-01T11:03:11.221466-07:00 ERR kernel: [ 2448.183450] iwlwifi "
          "0000:01:00.0: 0x00000000 | l2p_control\n"
          "2020-09-01T11:03:11.221468-07:00 ERR kernel: [ 2448.183454] iwlwifi "
          "0000:01:00.0: 0x00000000 | l2p_duration\n"
          "2020-09-01T11:03:11.221470-07:00 ERR kernel: [ 2448.183458] iwlwifi "
          "0000:01:00.0: 0x00000000 | l2p_mhvalid\n"
          "2020-09-01T11:03:11.221472-07:00 ERR kernel: [ 2448.183461] iwlwifi "
          "0000:01:00.0: 0x00000000 | l2p_addr_match\n"
          "2020-09-01T11:03:11.221474-07:00 ERR kernel: [ 2448.183465] iwlwifi "
          "0000:01:00.0: 0x00000000 | lmpm_pmg_sel\n"
          "2020-09-01T11:03:11.221475-07:00 ERR kernel: [ 2448.183469] iwlwifi "
          "0000:01:00.0: 0x00000000 | timestamp\n"
          "2020-09-01T11:03:11.221478-07:00 ERR kernel: [ 2448.183473] iwlwifi "
          "0000:01:00.0: 0x00000000 | flow_handler\n",
      .expected_flags = {{"--kernel_iwlwifi_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_IWLWIFI_DRIVER_ERROR", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelIwlwifiErrorLmac) {
  ParserRun wifi_error = {
      .expected_text =
          "[15883.337352] iwlwifi 0000:00:0c.0: Loaded firmware version: "
          "46.b20aefee.0\n"
          "[15883.337355] iwlwifi 0000:00:0c.0: 0x00000084 | "
          "NMI_INTERRUPT_UNKNOWN\n"
          "[15883.337357] iwlwifi 0000:00:0c.0: 0x000022F0 | trm_hw_status0\n"
          "[15883.337359] iwlwifi 0000:00:0c.0: 0x00000000 | trm_hw_status1\n"
          "[15883.337362] iwlwifi 0000:00:0c.0: 0x0048751E | branchlink2\n"
          "[15883.337364] iwlwifi 0000:00:0c.0: 0x00479236 | interruptlink1\n"
          "[15883.337366] iwlwifi 0000:00:0c.0: 0x0000AE00 | interruptlink2\n"
          "[15883.337369] iwlwifi 0000:00:0c.0: 0x0001A2D6 | data1\n"
          "[15883.337371] iwlwifi 0000:00:0c.0: 0xFF000000 | data2\n"
          "[15883.337373] iwlwifi 0000:00:0c.0: 0xF0000000 | data3\n"
          "[15883.337376] iwlwifi 0000:00:0c.0: 0x00000000 | beacon time\n"
          "[15883.337378] iwlwifi 0000:00:0c.0: 0x158DE6F7 | tsf low\n"
          "[15883.337380] iwlwifi 0000:00:0c.0: 0x00000000 | tsf hi\n"
          "[15883.337383] iwlwifi 0000:00:0c.0: 0x00000000 | time gp1\n"
          "[15883.337385] iwlwifi 0000:00:0c.0: 0x158DE6F9 | time gp2\n"
          "[15883.337388] iwlwifi 0000:00:0c.0: 0x00000001 | uCode revision "
          "type\n"
          "[15883.337390] iwlwifi 0000:00:0c.0: 0x0000002E | uCode version "
          "major\n"
          "[15883.337392] iwlwifi 0000:00:0c.0: 0xB20AEFEE | uCode version "
          "minor\n"
          "[15883.337394] iwlwifi 0000:00:0c.0: 0x00000312 | hw version\n"
          "[15883.337397] iwlwifi 0000:00:0c.0: 0x00C89008 | board version\n"
          "[15883.337399] iwlwifi 0000:00:0c.0: 0x007B019C | hcmd\n"
          "[15883.337401] iwlwifi 0000:00:0c.0: 0x00022000 | isr0\n"
          "[15883.337404] iwlwifi 0000:00:0c.0: 0x00000000 | isr1\n"
          "[15883.337406] iwlwifi 0000:00:0c.0: 0x08001802 | isr2\n"
          "[15883.337408] iwlwifi 0000:00:0c.0: 0x40400180 | isr3\n"
          "[15883.337411] iwlwifi 0000:00:0c.0: 0x00000000 | isr4\n"
          "[15883.337413] iwlwifi 0000:00:0c.0: 0x007B019C | last cmd Id\n"
          "[15883.337415] iwlwifi 0000:00:0c.0: 0x0001A2D6 | wait_event\n"
          "[15883.337417] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_control\n"
          "[15883.337420] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_duration\n"
          "[15883.337422] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_mhvalid\n"
          "[15883.337424] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_addr_match\n"
          "[15883.337427] iwlwifi 0000:00:0c.0: 0x0000008F | lmpm_pmg_sel\n"
          "[15883.337429] iwlwifi 0000:00:0c.0: 0x24021230 | timestamp\n"
          "[15883.337432] iwlwifi 0000:00:0c.0: 0x0000B0D8 | flow_handler\n",
      .expected_flags = {{"--kernel_iwlwifi_error", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_IWLWIFI_LMAC", {wifi_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelKFENCE) {
  ParserRun kfence_error = {
      .expected_text =
          "[  210.911352] BUG: KFENCE: use-after-free read in "
          "lkdtm_READ_AFTER_FREE+0xac/0x13c\n"
          "\n"
          "[  210.911412] Use-after-free read at 0x00000000c8aa61de (in "
          "kfence-#117):\n"
          "[  210.911446]  lkdtm_READ_AFTER_FREE+0xac/0x13c\n"
          "[  210.911486]  lkdtm_do_action+0x24/0x58\n"
          "[  210.911531]  direct_entry+0x1e8/0x25c\n"
          "[  210.911574]  full_proxy_write+0x74/0xa4\n"
          "[  210.911621]  vfs_write+0xe8/0x3dc\n"
          "[  210.911665]  __arm64_sys_write+0x70/0xf0\n"
          "[  210.911705]  invoke_syscall+0x4c/0xe8\n"
          "[  210.911745]  el0_svc_common+0xa0/0x184\n"
          "[  210.911782]  do_el0_svc+0x30/0x90\n"
          "[  210.911818]  el0_svc+0x20/0x50\n"
          "[  210.911858]  el0t_64_sync_handler+0x20/0x110\n"
          "[  210.911896]  el0t_64_sync+0x1a4/0x1a8\n"
          "\n"
          "[  210.911951] kfence-#117: 0x00000000b84a828c-0x00000000cde259c3, "
          "size=1024, cache=kmalloc-1k\n"
          "\n"
          "[  210.911990] allocated by task 5582 on cpu 2 at 210.911150s:\n"
          "[  210.912053]  lkdtm_READ_AFTER_FREE+0x30/0x13c\n"
          "[  210.912090]  lkdtm_do_action+0x24/0x58\n"
          "[  210.912131]  direct_entry+0x1e8/0x25c\n"
          "[  210.912171]  full_proxy_write+0x74/0xa4\n"
          "[  210.912211]  vfs_write+0xe8/0x3dc\n"
          "[  210.912248]  __arm64_sys_write+0x70/0xf0\n"
          "[  210.912286]  invoke_syscall+0x4c/0xe8\n"
          "[  210.912321]  el0_svc_common+0xa0/0x184\n"
          "[  210.912357]  do_el0_svc+0x30/0x90\n"
          "[  210.912391]  el0_svc+0x20/0x50\n"
          "[  210.912425]  el0t_64_sync_handler+0x20/0x110\n"
          "[  210.912462]  el0t_64_sync+0x1a4/0x1a8\n"
          "\n"
          "[  210.912512] freed by task 5582 on cpu 2 at 210.911281s:\n"
          "[  210.912568]  lkdtm_READ_AFTER_FREE+0xa0/0x13c\n"
          "[  210.912603]  lkdtm_do_action+0x24/0x58\n"
          "[  210.912644]  direct_entry+0x1e8/0x25c\n"
          "[  210.912684]  full_proxy_write+0x74/0xa4\n"
          "[  210.912724]  vfs_write+0xe8/0x3dc\n"
          "[  210.912762]  __arm64_sys_write+0x70/0xf0\n"
          "[  210.912800]  invoke_syscall+0x4c/0xe8\n"
          "[  210.912835]  el0_svc_common+0xa0/0x184\n"
          "[  210.912872]  do_el0_svc+0x30/0x90\n"
          "[  210.912908]  el0_svc+0x20/0x50\n"
          "[  210.912944]  el0t_64_sync_handler+0x20/0x110\n"
          "[  210.912981]  el0t_64_sync+0x1a4/0x1a8\n"
          "\n"
          "[  210.913038] CPU: 2 PID: 5582 Comm: bash Not tainted "
          "5.15.136-20794-g9bd4a6db2ccd-dirty #158 "
          "28594e572a412942e6407c45649761f186265000\n"
          "[  210.913088] Hardware name: Google Lazor (rev1 - 2) with LTE "
          "(DT)\n",
      .expected_flags = {{"--kernel_kfence"}}};
  KernelParser parser(true);
  ParserTest("TEST_KFENCE", {kfence_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelKASAN) {
  ParserRun kasan_error = {
      .expected_text =
          "[   79.065901] BUG: KASAN: slab-use-after-free in "
          "lkdtm_READ_AFTER_FREE+0x214/0x220\n"
          "[   79.065939] Read of size 4 at addr ffff8881143a9810 by task "
          "bash/5320\n"
          "\n"
          "[   79.065992] CPU: 0 PID: 5320 Comm: bash Tainted: G     U         "
          "    6.6.99-kasan-lockdep-08987-g33318fe92a65 #1 "
          "0ca2767c6653d594af11243f58d688481752f0dd\n"
          "[   79.066027] Hardware name: Google Crota/Crota, BIOS "
          "Google_Crota.16425.0.0 09/19/2025\n"
          "[   79.066046] Call Trace:\n"
          "[   79.066065]  <TASK>\n"
          "[   79.066083]  dump_stack_lvl+0x69/0xa0\n"
          "[   79.066105]  print_address_description+0x51/0x1e0\n"
          "[   79.066139]  print_report+0x4e/0x60\n"
          "[   79.066159]  kasan_report+0x106/0x140\n"
          "[   79.066179]  ? lkdtm_READ_AFTER_FREE+0x214/0x220\n"
          "[   79.066214]  lkdtm_READ_AFTER_FREE+0x214/0x220\n"
          "[   79.066233]  lkdtm_do_action+0x40/0x60\n"
          "[   79.066267]  direct_entry+0x21c/0x270\n"
          "[   79.066287]  full_proxy_write+0xe5/0x170\n"
          "[   79.066307]  __x64_sys_write+0x2c3/0xc50\n"
          "[   79.066341]  ? ktime_get_coarse_real_ts64+0xd7/0x180\n"
          "[   79.066362]  ? ktime_get_coarse_real_ts64+0xd7/0x180\n"
          "[   79.066397]  do_syscall_64+0x6b/0xa0\n"
          "[   79.066417]  ? exc_page_fault+0x8e/0xf0\n"
          "[   79.066436]  ? lockdep_hardirqs_on+0x9d/0x150\n"
          "[   79.066470]  ? exc_page_fault+0x8e/0xf0\n"
          "[   79.066489]  entry_SYSCALL_64_after_hwframe+0x73/0xdd\n"
          "[   79.066524] RIP: 0033:0x7925e4dff972\n"
          "[   79.066544] Code: 48 89 e5 53 48 83 ec 08 64 48 8b 1c 25 10 00 "
          "00 00 8b 83 08 03 00 00 80 3d 9a 47 15 00 00 75 04 a8 01 74 14 48 "
          "8b 45 10 0f 05 <48> 8b 5d f8 c9 c3 0f 1f 84 00 00 00 00 00 a8 10 75 "
          "e8 41 51 4c 8d\n"
          "[   79.066592] RSP: 002b:00007fff1628f790 EFLAGS: 00000202 "
          "ORIG_RAX: 0000000000000001\n"
          "[   79.066614] RAX: ffffffffffffffda RBX: 00007925e4d4a740 RCX: "
          "00007925e4dff972\n"
          "[   79.066648] RDX: 0000000000000010 RSI: 0000578cd38a67f0 RDI: "
          "0000000000000001\n"
          "[   79.066682] RBP: 00007fff1628f7a0 R08: 0000000000000000 R09: "
          "0000000000000000\n"
          "[   79.066702] R10: 0000000000000000 R11: 0000000000000202 R12: "
          "0000000000000010\n"
          "[   79.066736] R13: 0000578cd38a67f0 R14: 00007925e4f4d5c0 R15: "
          "00007925e4f4aea0\n"
          "[   79.066773]  </TASK>\n"
          "\n"
          "[   79.066811] Allocated by task 5320:\n"
          "[   79.066831]  stack_trace_save+0x4b/0x70\n"
          "[   79.066868]  kasan_set_track+0x4d/0x80\n"
          "[   79.066889]  __kasan_kmalloc+0x75/0x90\n"
          "[   79.066909]  lkdtm_READ_AFTER_FREE+0x50/0x220\n"
          "[   79.066944]  lkdtm_do_action+0x40/0x60\n"
          "[   79.066965]  direct_entry+0x21c/0x270\n"
          "[   79.066985]  full_proxy_write+0xe5/0x170\n"
          "[   79.067020]  __x64_sys_write+0x2c3/0xc50\n"
          "[   79.067041]  do_syscall_64+0x6b/0xa0\n"
          "[   79.067061]  entry_SYSCALL_64_after_hwframe+0x73/0xdd\n"
          "\n"
          "[   79.067116] Freed by task 5320:\n"
          "[   79.067136]  stack_trace_save+0x4b/0x70\n"
          "[   79.067171]  kasan_set_track+0x4d/0x80\n"
          "[   79.067191]  kasan_save_free_info+0x2e/0x50\n"
          "[   79.067211]  ____kasan_slab_free+0xf7/0x170\n"
          "[   79.067247]  __kmem_cache_free+0xf2/0x950\n"
          "[   79.067267]  lkdtm_READ_AFTER_FREE+0x112/0x220\n"
          "[   79.067301]  lkdtm_do_action+0x40/0x60\n"
          "[   79.067320]  direct_entry+0x21c/0x270\n"
          "[   79.067340]  full_proxy_write+0xe5/0x170\n"
          "[   79.067374]  __x64_sys_write+0x2c3/0xc50\n"
          "[   79.067394]  do_syscall_64+0x6b/0xa0\n"
          "[   79.067414]  entry_SYSCALL_64_after_hwframe+0x73/0xdd\n"
          "\n"
          "[   79.067468] The buggy address belongs to the object at "
          "ffff8881143a9800\n"
          "                which belongs to the cache kmalloc-1k of size 1024\n"
          "[   79.067503] The buggy address is located 16 bytes inside of\n"
          "                freed 1024-byte region [ffff8881143a9800, "
          "ffff8881143a9c00)\n"
          "\n"
          "[   79.067558] The buggy address belongs to the physical page:\n"
          "[   79.067593] page:000000006bb0be1e refcount:1 mapcount:0 "
          "mapping:0000000000000000 index:0x0 pfn:0x1143a8\n"
          "[   79.067629] head:000000006bb0be1e order:3 entire_mapcount:0 "
          "nr_pages_mapped:0 pincount:0\n"
          "[   79.067664] flags: 0x8000000000000840(slab|head|zone=2)\n"
          "[   79.067685] page_type: 0xffffffff()\n"
          "[   79.067705] raw: 8000000000000840 ffff88810004d600 "
          "ffffea00096d3a00 dead000000000002\n"
          "[   79.067740] raw: 0000000000000000 0000000080100010 "
          "00000001ffffffff 0000000000000000\n"
          "[   79.067774] page dumped because: kasan: bad access detected\n"
          "\n"
          "[   79.067812] Memory state around the buggy address:\n"
          "[   79.067847]  ffff8881143a9700: fc fc fc fc fc fc fc fc fc fc fc "
          "fc fc fc fc fc\n"
          "[   79.067881]  ffff8881143a9780: fc fc fc fc fc fc fc fc fc fc fc "
          "fc fc fc fc fc\n"
          "[   79.067901] >ffff8881143a9800: fa fb fb fb fb fb fb fb fb fb fb "
          "fb fb fb fb fb\n"
          "[   79.067935]                          ^\n"
          "[   79.067954]  ffff8881143a9880: fb fb fb fb fb fb fb fb fb fb fb "
          "fb fb fb fb fb\n"
          "[   79.067989]  ffff8881143a9900: fb fb fb fb fb fb fb fb fb fb fb "
          "fb fb fb fb fb\n",
      .expected_flags = {{"--kernel_kasan"}}};
  KernelParser parser(true);
  ParserTest("TEST_KASAN", {kasan_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelLockdebugCircular) {
  ParserRun lockdebug_error = {
      .expected_text =
          "[   44.421050] "
          "======================================================\n"
          "[   44.421081] WARNING: possible circular locking dependency "
          "detected\n"
          "[   44.421113] 5.15.180-kasan-lockdep-24494-gc7c1b8d2fd39 #1 Not "
          "tainted\n"
          "[   44.421151] "
          "------------------------------------------------------\n"
          "[   44.421180] chrome/1993 is trying to acquire lock:\n"
          "[   44.421215] ffffffc0132e22a0 (&kctx->reg_lock){+.+.}-{3:3}, at: "
          "kbase_gpu_vm_lock+0x30/0x40\n"
          "[   44.421345] \n"
          "[   44.421345] but task is already holding lock:\n"
          "[   44.421373] ffffff80d304b128 (&mm->mmap_lock){++++}-{3:3}, at: "
          "vm_mmap_pgoff+0xf4/0x224\n"
          "[   44.421482] \n"
          "[   44.421482] which lock already depends on the new lock.\n"
          "[   44.421482] \n"
          "[   44.421510] \n"
          "[   44.421510] the existing dependency chain (in reverse order) "
          "is:\n"
          "[   44.421540] \n"
          "[   44.421540] -> #6 (&mm->mmap_lock){++++}-{3:3}:\n"
          "[   44.421620]        __might_fault+0xc4/0x2b0\n"
          "[   44.421672]        filldir64+0x254/0x9cc\n"
          "[   44.421726]        dcache_readdir+0x3a4/0x47c\n"
          "[   44.421776]        iterate_dir+0x188/0x4e0\n"
          "[   44.421826]        __arm64_sys_getdents64+0xd8/0x514\n"
          "[   44.421878]        invoke_syscall+0xe0/0x268\n"
          "[   44.421926]        el0_svc_common+0x234/0x3c4\n"
          "[   44.421971]        do_el0_svc+0x9c/0x19c\n"
          "[   44.422015]        el0_svc+0x5c/0xbc\n"
          "[   44.422061]        el0t_64_sync_handler+0x98/0xac\n"
          "[   44.422107]        el0t_64_sync+0x1a4/0x1a8\n"
          "[   44.422153] \n"
          "[   44.422153] -> #5 (&sb->s_type->i_mutex_key#3){++++}-{3:3}:\n"
          "[   44.422247]        down_write+0xb8/0x2e8\n"
          "[   44.422299]        simple_recursive_removal+0x94/0x6b0\n"
          "[   44.422348]        debugfs_remove+0x68/0x90\n"
          "[   44.422396]        _regulator_put+0x100/0x464\n"
          "[   44.422447]        regulator_put+0x3c/0x54\n"
          "[   44.422495]        devm_regulator_release+0x44/0x54\n"
          "[   44.422544]        devres_release_all+0x15c/0x1dc\n"
          "[   44.422592]        really_probe+0x4ac/0xae0\n"
          "[   44.422639]        __driver_probe_device+0x188/0x318\n"
          "[   44.422686]        driver_probe_device+0x80/0x340\n"
          "[   44.422732]        __device_attach_driver+0x27c/0x4c0\n"
          "[   44.422779]        bus_for_each_drv+0x118/0x18c\n"
          "[   44.422830]        __device_attach+0x268/0x35c\n"
          "[   44.422875]        device_initial_probe+0x2c/0x3c\n"
          "[   44.422920]        bus_probe_device+0xc4/0x1cc\n"
          "[   44.422970]        deferred_probe_work_func+0x1b4/0x240\n"
          "[   44.423017]        process_one_work+0x5cc/0x1b2c\n"
          "[   44.423063]        worker_thread+0xa3c/0xf74\n"
          "[   44.423107]        kthread+0x378/0x458\n"
          "[   44.423158]        ret_from_fork+0x10/0x20\n"
          "[   44.423204] \n"
          "[   44.423204] -> #4 (regulator_list_mutex){+.+.}-{3:3}:\n"
          "[   44.423287]        __mutex_lock_common+0xd4/0x16c8\n"
          "[   44.423340]        mutex_lock_nested+0x9c/0xb0\n"
          "[   44.423389]        regulator_lock_dependent+0x60/0x56c\n"
          "[   44.423442]        regulator_enable+0x70/0xf0\n"
          "[   44.423491]        kbase_pm_callback_power_on+0x118/0x8e4\n"
          "[   44.423546]        kbase_pm_init_hw+0xf4/0xf00\n"
          "[   44.423592]        kbase_hwaccess_pm_powerup+0x58/0x2ec\n"
          "[   44.423647]        kbase_backend_late_init+0x74/0x1b0\n"
          "[   44.423699]        kbase_device_init+0xc4/0x238\n"
          "[   44.423746]        kbase_platform_device_probe+0xd4/0x220\n"
          "[   44.423797]        platform_probe+0x144/0x1bc\n"
          "[   44.423842]        really_probe+0x274/0xae0\n"
          "[   44.423887]        __driver_probe_device+0x188/0x318\n"
          "[   44.423934]        driver_probe_device+0x80/0x340\n"
          "[   44.423980]        __device_attach_driver+0x27c/0x4c0\n"
          "[   44.424027]        bus_for_each_drv+0x118/0x18c\n"
          "[   44.424076]        __device_attach_async_helper+0x180/0x204\n"
          "[   44.424125]        async_run_entry_fn+0xa0/0x3bc\n"
          "[   44.424180]        process_one_work+0x5cc/0x1b2c\n"
          "[   44.424225]        worker_thread+0x454/0xf74\n"
          "[   44.424269]        kthread+0x378/0x458\n"
          "[   44.424318]        ret_from_fork+0x10/0x20\n"
          "[   44.424364] \n"
          "[   44.424364] -> #3 (&kbdev->pm.lock){+.+.}-{3:3}:\n"
          "[   44.424446]        __mutex_lock_common+0xd4/0x16c8\n"
          "[   44.424498]        mutex_lock_nested+0x9c/0xb0\n"
          "[   44.424548]        kbase_hwaccess_pm_powerup+0x4c/0x2ec\n"
          "[   44.424601]        kbase_backend_late_init+0x74/0x1b0\n"
          "[   44.424653]        kbase_device_init+0xc4/0x238\n"
          "[   44.424699]        kbase_platform_device_probe+0xd4/0x220\n"
          "[   44.424749]        platform_probe+0x144/0x1bc\n"
          "[   44.424793]        really_probe+0x274/0xae0\n"
          "[   44.424839]        __driver_probe_device+0x188/0x318\n"
          "[   44.424885]        driver_probe_device+0x80/0x340\n"
          "[   44.424931]        __device_attach_driver+0x27c/0x4c0\n"
          "[   44.424979]        bus_for_each_drv+0x118/0x18c\n"
          "[   44.425028]        __device_attach_async_helper+0x180/0x204\n"
          "[   44.425076]        async_run_entry_fn+0xa0/0x3bc\n"
          "[   44.425129]        process_one_work+0x5cc/0x1b2c\n"
          "[   44.425174]        worker_thread+0x454/0xf74\n"
          "[   44.425217]        kthread+0x378/0x458\n"
          "[   44.425267]        ret_from_fork+0x10/0x20\n"
          "[   44.425311] \n"
          "[   44.425311] -> #2 (&jsdd->runpool_mutex){+.+.}-{3:3}:\n"
          "[   44.425393]        __mutex_lock_common+0xd4/0x16c8\n"
          "[   44.425444]        mutex_lock_nested+0x9c/0xb0\n"
          "[   44.425494]        "
          "kbase_pm_context_active_handle_suspend+0x4c/0x2b8\n"
          "[   44.425548]        kbase_js_sched+0x4bc/0x1c94\n"
          "[   44.425595]        "
          "kbasep_js_schedule_privileged_ctx+0x1a8/0x3a8\n"
          "[   44.425647]        kbasep_hwcnt_backend_jm_init+0x280/0x514\n"
          "[   44.425701]        "
          "kbasep_hwcnt_backend_jm_watchdog_init+0x1ec/0x470\n"
          "[   44.425758]        kbase_hwcnt_accumulator_acquire+0x13c/0x5f8\n"
          "[   44.425809]        "
          "kbase_hwcnt_virtualizer_client_create+0x8f8/0xed8\n"
          "[   44.425864]        kbase_ipa_attach_vinstr+0x774/0x8f8\n"
          "[   44.425912]        "
          "kbase_ipa_vinstr_common_model_init+0x370/0x434\n"
          "[   44.425961]        kbase_g52_r1_power_model_init+0x40/0x50\n"
          "[   44.426009]        kbase_ipa_init_model+0x194/0x4f8\n"
          "[   44.426054]        kbase_ipa_init+0x520/0x7b0\n"
          "[   44.426098]        kbase_devfreq_init+0x494/0x1c24\n"
          "[   44.426148]        kbase_backend_devfreq_init+0x28/0x8c\n"
          "[   44.426203]        kbase_backend_late_init+0xe0/0x1b0\n"
          "[   44.426255]        kbase_device_init+0xc4/0x238\n"
          "[   44.426300]        kbase_platform_device_probe+0xd4/0x220\n"
          "[   44.426351]        platform_probe+0x144/0x1bc\n"
          "[   44.426395]        really_probe+0x274/0xae0\n"
          "[   44.426440]        __driver_probe_device+0x188/0x318\n"
          "[   44.426487]        driver_probe_device+0x80/0x340\n"
          "[   44.426533]        __device_attach_driver+0x27c/0x4c0\n"
          "[   44.426580]        bus_for_each_drv+0x118/0x18c\n"
          "[   44.426629]        __device_attach_async_helper+0x180/0x204\n"
          "[   44.426678]        async_run_entry_fn+0xa0/0x3bc\n"
          "[   44.426730]        process_one_work+0x5cc/0x1b2c\n"
          "[   44.426775]        worker_thread+0x454/0xf74\n"
          "[   44.426819]        kthread+0x378/0x458\n"
          "[   44.426868]        ret_from_fork+0x10/0x20\n"
          "[   44.426913] \n"
          "[   44.426913] -> #1 (&jsdd->queue_mutex){+.+.}-{3:3}:\n"
          "[   44.426996]        __mutex_lock_common+0xd4/0x16c8\n"
          "[   44.427048]        mutex_lock_nested+0x9c/0xb0\n"
          "[   44.427098]        mmu_flush_invalidate+0x88/0x164\n"
          "[   44.427150]        "
          "mmu_flush_invalidate_insert_pages+0x150/0x2e0\n"
          "[   44.427203]        kbase_mmu_insert_pages+0xe4/0x124\n"
          "[   44.427254]        kbase_gpu_mmap+0x6c4/0xbc4\n"
          "[   44.427307]        kbase_mem_alloc+0xcf8/0x15e8\n"
          "[   44.427357]        kbasep_hwcnt_backend_jm_init+0x2ec/0x514\n"
          "[   44.427411]        "
          "kbasep_hwcnt_backend_jm_watchdog_init+0x1ec/0x470\n"
          "[   44.427467]        kbase_hwcnt_accumulator_acquire+0x13c/0x5f8\n"
          "[   44.427519]        "
          "kbase_hwcnt_virtualizer_client_create+0x8f8/0xed8\n"
          "[   44.427574]        kbase_ipa_attach_vinstr+0x774/0x8f8\n"
          "[   44.427621]        "
          "kbase_ipa_vinstr_common_model_init+0x370/0x434\n"
          "[   44.427671]        kbase_g52_r1_power_model_init+0x40/0x50\n"
          "[   44.427718]        kbase_ipa_init_model+0x194/0x4f8\n"
          "[   44.427763]        kbase_ipa_init+0x520/0x7b0\n"
          "[   44.427807]        kbase_devfreq_init+0x494/0x1c24\n"
          "[   44.427857]        kbase_backend_devfreq_init+0x28/0x8c\n"
          "[   44.427911]        kbase_backend_late_init+0xe0/0x1b0\n"
          "[   44.427963]        kbase_device_init+0xc4/0x238\n"
          "[   44.428009]        kbase_platform_device_probe+0xd4/0x220\n"
          "[   44.428060]        platform_probe+0x144/0x1bc\n"
          "[   44.428104]        really_probe+0x274/0xae0\n"
          "[   44.428150]        __driver_probe_device+0x188/0x318\n"
          "[   44.428197]        driver_probe_device+0x80/0x340\n"
          "[   44.428243]        __device_attach_driver+0x27c/0x4c0\n"
          "[   44.428290]        bus_for_each_drv+0x118/0x18c\n"
          "[   44.428339]        __device_attach_async_helper+0x180/0x204\n"
          "[   44.428387]        async_run_entry_fn+0xa0/0x3bc\n"
          "[   44.428440]        process_one_work+0x5cc/0x1b2c\n"
          "[   44.428485]        worker_thread+0x454/0xf74\n"
          "[   44.428528]        kthread+0x378/0x458\n"
          "[   44.428579]        ret_from_fork+0x10/0x20\n"
          "[   44.428623] \n"
          "[   44.428623] -> #0 (&kctx->reg_lock){+.+.}-{3:3}:\n"
          "[   44.428708]        __lock_acquire+0x285c/0x6bd8\n"
          "[   44.428763]        lock_acquire+0x1ec/0x65c\n"
          "[   44.428813]        __mutex_lock_common+0xd4/0x16c8\n"
          "[   44.428865]        mutex_lock_nested+0x9c/0xb0\n"
          "[   44.428915]        kbase_gpu_vm_lock+0x30/0x40\n"
          "[   44.428967]        kbase_context_get_unmapped_area+0x2cc/0x68c\n"
          "[   44.429021]        kbase_get_unmapped_area+0xe0/0x2bc\n"
          "[   44.429070]        do_mmap+0x2c8/0x86c4\n"
          "[   44.429119]        vm_mmap_pgoff+0x134/0x224\n"
          "[   44.429168]        ksys_mmap_pgoff+0xe0/0x19c\n"
          "[   44.429217]        __arm64_sys_mmap+0x100/0x118\n"
          "[   44.429270]        invoke_syscall+0xe0/0x268\n"
          "[   44.429316]        el0_svc_common+0x234/0x3c4\n"
          "[   44.429361]        do_el0_svc+0x9c/0x19c\n"
          "[   44.429405]        el0_svc+0x5c/0xbc\n"
          "[   44.429449]        el0t_64_sync_handler+0x98/0xac\n"
          "[   44.429496]        el0t_64_sync+0x1a4/0x1a8\n"
          "[   44.429541] \n"
          "[   44.429541] other info that might help us debug this:\n"
          "[   44.429541] \n"
          "[   44.429570] Chain exists of:\n"
          "[   44.429570]   &kctx->reg_lock --> &sb->s_type->i_mutex_key#3 --> "
          "&mm->mmap_lock\n"
          "[   44.429570] \n"
          "[   44.429683]  Possible unsafe locking scenario:\n"
          "[   44.429683] \n"
          "[   44.429710]        CPU0                    CPU1\n"
          "[   44.429737]        ----                    ----\n"
          "[   44.429764]   lock(&mm->mmap_lock);\n"
          "[   44.429811]                                "
          "lock(&sb->s_type->i_mutex_key#3);\n"
          "[   44.429874]                                "
          "lock(&mm->mmap_lock);\n"
          "[   44.429923]   lock(&kctx->reg_lock);\n"
          "[   44.429971] \n"
          "[   44.429971]  *** DEADLOCK ***\n"
          "[   44.429971] \n"
          "[   44.429997] 1 lock held by chrome/1993:\n"
          "[   44.430034]  #0: ffffff80d304b128 (&mm->mmap_lock){++++}-{3:3}, "
          "at: vm_mmap_pgoff+0xf4/0x224\n"
          "[   44.430154] \n"
          "[   44.430154] stack backtrace:\n"
          "[   44.430184] CPU: 5 PID: 1993 Comm: chrome Not tainted "
          "5.15.180-kasan-lockdep-24494-gc7c1b8d2fd39 #1 "
          "dfd77821ac6d43b47f4d5fd37b09b6d4b7a00cc6\n"
          "[   44.430245] Hardware name: Google Kyogre board (DT)\n"
          "[   44.430279] Call trace:\n"
          "[   44.430306]  dump_backtrace+0x0/0x3d0\n"
          "[   44.430357]  show_stack+0x34/0x50\n"
          "[   44.430405]  __dump_stack+0x30/0x40\n"
          "[   44.430453]  dump_stack_lvl+0xe0/0x134\n"
          "[   44.430500]  dump_stack+0x1c/0x4c\n"
          "[   44.430546]  print_circular_bug+0x41c/0x530\n"
          "[   44.430591]  check_noncircular+0x1dc/0x270\n"
          "[   44.430636]  __lock_acquire+0x285c/0x6bd8\n"
          "[   44.430689]  lock_acquire+0x1ec/0x65c\n"
          "[   44.430739]  __mutex_lock_common+0xd4/0x16c8\n"
          "[   44.430791]  mutex_lock_nested+0x9c/0xb0\n"
          "[   44.430841]  kbase_gpu_vm_lock+0x30/0x40\n"
          "[   44.430894]  kbase_context_get_unmapped_area+0x2cc/0x68c\n"
          "[   44.430950]  kbase_get_unmapped_area+0xe0/0x2bc\n"
          "[   44.430999]  do_mmap+0x2c8/0x86c4\n"
          "[   44.431047]  vm_mmap_pgoff+0x134/0x224\n"
          "[   44.431095]  ksys_mmap_pgoff+0xe0/0x19c\n"
          "[   44.431144]  __arm64_sys_mmap+0x100/0x118\n"
          "[   44.431196]  invoke_syscall+0xe0/0x268\n"
          "[   44.431242]  el0_svc_common+0x234/0x3c4\n"
          "[   44.431287]  do_el0_svc+0x9c/0x19c\n"
          "[   44.431331]  el0_svc+0x5c/0xbc\n"
          "[   44.431376]  el0t_64_sync_handler+0x98/0xac\n"
          "[   44.431423]  el0t_64_sync+0x1a4/0x1a8\n",
      .expected_flags = {{"--kernel_debug"}}};
  KernelParser parser(true);
  ParserTest("TEST_LOCKDEBUG_CIRCULAR", {lockdebug_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelLockdebugRecursive) {
  ParserRun lockdebug_error = {
      .expected_text =
          "[    4.626697] ============================================\n"
          "[    4.626725] WARNING: possible recursive locking detected\n"
          "[    4.626749] 6.1.145-kasan-lockdep-17509-g3e49662a061f #1 Not "
          "tainted\n"
          "[    4.626780] --------------------------------------------\n"
          "[    4.626802] swapper/0/1 is trying to acquire lock:\n"
          "[    4.626829] ffffff80c8102718 (&tz->lock){+.+.}-{3:3}, at: "
          "thermal_zone_get_temp+0x40/0x1e4\n"
          "[    4.626931] \n"
          "[    4.626931] but task is already holding lock:\n"
          "[    4.626952] ffffff80c8294718 (&tz->lock){+.+.}-{3:3}, at: "
          "thermal_zone_device_update+0xc4/0xed0\n"
          "[    4.627038] \n"
          "[    4.627038] other info that might help us debug this:\n"
          "[    4.627060]  Possible unsafe locking scenario:\n"
          "[    4.627060] \n"
          "[    4.627080]        CPU0\n"
          "[    4.627099]        ----\n"
          "[    4.627117]   lock(&tz->lock);\n"
          "[    4.627154]   lock(&tz->lock);\n"
          "[    4.627189] \n"
          "[    4.627189]  *** DEADLOCK ***\n"
          "[    4.627189] \n"
          "[    4.627209]  May be due to missing lock nesting notation\n"
          "[    4.627209] \n"
          "[    4.627229] 2 locks held by swapper/0/1:\n"
          "[    4.627257]  #0: ffffff80c1fa01c0 (&dev->mutex){....}-{3:3}, at: "
          "__driver_attach+0x3b8/0x5ec\n"
          "[    4.627355]  #1: ffffff80c8294718 (&tz->lock){+.+.}-{3:3}, at: "
          "thermal_zone_device_update+0xc4/0xed0\n"
          "[    4.627444] \n"
          "[    4.627444] stack backtrace:\n"
          "[    4.627467] CPU: 6 PID: 1 Comm: swapper/0 Not tainted "
          "6.1.145-kasan-lockdep-17509-g3e49662a061f #1 "
          "5f12191c255525f6bb3c0b71731126913aac0dda\n"
          "[    4.627512] Hardware name: Google Ciri sku0/unprovisioned board "
          "(DT)\n"
          "[    4.627539] Call trace:\n"
          "[    4.627559]  dump_backtrace+0x1c8/0x1f4\n"
          "[    4.627599]  show_stack+0x34/0x44\n"
          "[    4.627634]  __dump_stack+0x30/0x40\n"
          "[    4.627672]  dump_stack_lvl+0xe0/0x134\n"
          "[    4.627709]  dump_stack+0x1c/0x4c\n"
          "[    4.627745]  __lock_acquire+0x1718/0x635c\n"
          "[    4.627783]  lock_acquire+0x204/0x67c\n"
          "[    4.627819]  __mutex_lock_common+0x108/0x10ac\n"
          "[    4.627861]  mutex_lock_nested+0x40/0x4c\n"
          "[    4.627899]  thermal_zone_get_temp+0x40/0x1e4\n"
          "[    4.627933]  vtemp_get_temp+0xac/0x204\n"
          "[    4.627973]  __thermal_zone_get_temp+0x114/0x188\n"
          "[    4.628010]  thermal_zone_device_update+0x170/0xed0\n"
          "[    4.628044]  thermal_zone_device_enable+0x118/0x16c\n"
          "[    4.628078]  thermal_of_zone_register+0x8f0/0xbc4\n"
          "[    4.628116]  devm_thermal_of_zone_register+0x94/0x104\n"
          "[    4.628155]  vtemp_probe+0x184/0x250\n"
          "[    4.628194]  platform_probe+0x144/0x1bc\n"
          "[    4.628226]  really_probe+0x3a0/0xb8c\n"
          "[    4.628265]  __driver_probe_device+0x188/0x318\n"
          "[    4.628305]  driver_probe_device+0x80/0x32c\n"
          "[    4.628346]  __driver_attach+0x3c4/0x5ec\n"
          "[    4.628385]  bus_for_each_dev+0x10c/0x178\n"
          "[    4.628422]  driver_attach+0x54/0x64\n"
          "[    4.628461]  bus_add_driver+0x2e0/0x55c\n"
          "[    4.628498]  driver_register+0x208/0x380\n"
          "[    4.628540]  __platform_driver_register+0x74/0x88\n"
          "[    4.628585]  vtemp_driver_init+0x28/0x34\n"
          "[    4.628629]  do_one_initcall+0x204/0x958\n"
          "[    4.628673]  do_initcall_level+0x11c/0x164\n"
          "[    4.628717]  do_initcalls+0x60/0xb4\n"
          "[    4.628759]  do_basic_setup+0x94/0xa8\n"
          "[    4.628801]  kernel_init_freeable+0x34c/0x4e0\n"
          "[    4.628845]  kernel_init+0x2c/0x1e4\n"
          "[    4.628888]  ret_from_fork+0x10/0x20\n",
      .expected_flags = {{"--kernel_debug"}}};
  KernelParser parser(true);
  ParserTest("TEST_LOCKDEBUG_RECURSIVE", {lockdebug_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelLockdebugSuspiciousRCU) {
  ParserRun lockdebug_error = {
      .expected_text =
          "[   50.707951] =============================\n"
          "[   50.708590] WARNING: suspicious RCU usage\n"
          "[   50.708844] "
          "5.4.293-kasan-kasan_outline-lockdep-23841-g4a79117cd6ea #1 Tainted: "
          "G     U  W        \n"
          "[   50.709300] -----------------------------\n"
          "[   50.709553] kernel/sched/core.c:5119 suspicious "
          "rcu_dereference_check() usage!\n"
          "[   50.710023] \n"
          "[   50.710023] other info that might help us debug this:\n"
          "[   50.710023] \n"
          "[   50.710463] \n"
          "[   50.710463] rcu_scheduler_active = 2, debug_locks = 1\n"
          "[   50.710715] 1 lock held by swapper/0/0:\n"
          "[   50.711195]  #0: ffffffff83d37a80 (rcu_read_lock_sched){....}, "
          "at: rcu_lock_acquire+0x4/0x30\n"
          "[   50.711480] \n"
          "[   50.711480] stack backtrace:\n"
          "[   50.711928] CPU: 0 PID: 0 Comm: swapper/0 Tainted: G     U  W    "
          "     5.4.293-kasan-kasan_outline-lockdep-23841-g4a79117cd6ea #1\n"
          "[   50.712365] Hardware name: Google Nami/Nami, BIOS "
          "Google_Nami.10775.145.0 09/19/2019\n"
          "[   50.712807] Call Trace:\n"
          "[   50.713077]  dump_stack+0x109/0x199\n"
          "[   50.713376]  ? queue_core_balance+0x150/0x150\n"
          "[   50.713653]  sched_core_balance+0x1a7/0x16f0\n"
          "[   50.714152]  ? __balance_callback+0x1f/0xf0\n"
          "[   50.714443]  ? __balance_callback+0x1f/0xf0\n"
          "[   50.714883]  ? _raw_spin_lock_irqsave+0x99/0xc0\n"
          "[   50.715133]  ? queue_core_balance+0x150/0x150\n"
          "[   50.715339]  __balance_callback+0x6f/0xf0\n"
          "[   50.715712]  schedule_idle+0xe00/0x2290\n"
          "[   50.715900]  ? ktime_get+0x17f/0x1f0\n"
          "[   50.716259]  ? debug_smp_processor_id+0x50/0x200\n"
          "[   50.716468]  ? tick_nohz_idle_exit+0x13b/0x4f0\n"
          "[   50.716778]  cpu_startup_entry+0x53/0x3f0\n"
          "[   50.717269]  start_kernel+0x50d/0x58a\n"
          "[   50.717550]  secondary_startup_64+0xa5/0xb0\n",
      .expected_flags = {{"--kernel_debug"}}};
  KernelParser parser(true);
  ParserTest("TEST_LOCKDEBUG_SUSPICIOUS_RCU", {lockdebug_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelSMMU_FAULT) {
  ParserRun smmu_error = {
      .expected_text =
          "[   74.047205] arm-smmu 15000000.iommu: Unhandled context fault: "
          "fsr=0x402, iova=0x04367000, fsynr=0x30023, cbfrsynra=0x800, cb=5\n",
      .expected_flags = {{"--kernel_smmu_fault"}}};
  KernelParser parser(true);
  ParserTest("TEST_SMMU_FAULT", {smmu_error}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarning) {
  ParserRun second{.find_this = "ttm_bo_vm.c",
                   .replace_with = "file_one.c",
                   .expected_substr =
                       "0x19e/0x1ab [ttm]()\n[ 3955.309298] Modules linked in",
                   .expected_flags = {{"--kernel_warning", "--weight=10"}}};
  KernelParser parser(true);
  ParserTest("TEST_WARNING", {simple_run, second}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningNoDuplicate) {
  ParserRun identical_warning{.expected_size = 0};
  KernelParser parser(true);
  ParserTest("TEST_WARNING", {simple_run, identical_warning}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningHeader) {
  ParserRun warning_message{.expected_substr =
                                "Test Warning message asdfghjkl"};
  KernelParser parser(true);
  ParserTest("TEST_WARNING_HEADER", {warning_message}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningOld) {
  KernelParser parser(true);
  ParserTest("TEST_WARNING_OLD", {simple_run}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningOldARM64) {
  ParserRun unknown_function{.expected_substr = "-unknown-function\n"};
  KernelParser parser(true);
  ParserTest("TEST_WARNING_OLD_ARM64", {unknown_function}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningWifi) {
  ParserRun wifi_warning = {
      .find_this = "gpu/drm/ttm",
      .replace_with = "net/wireless",
      .expected_flags = {{"--kernel_wifi_warning", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_WARNING", {wifi_warning}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningWifiMac80211) {
  ParserRun wifi_warning = {
      .expected_flags = {{"--kernel_wifi_warning", "--weight=50"}}};
  KernelParser parser(true);
  ParserTest("TEST_WIFI_WARNING", {wifi_warning}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningSuspend_v4_14) {
  ParserRun suspend_warning = {
      .find_this = "gpu/drm/ttm",
      .replace_with = "idle",
      .expected_flags = {{"--kernel_suspend_warning", "--weight=10"}}};
  KernelParser parser(true);
  ParserTest("TEST_WARNING", {suspend_warning}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningSuspend_EC) {
  ParserRun suspend_warning = {
      .find_this = "gpu/drm/ttm/ttm_bo_vm.c",
      .replace_with = "platform/chrome/cros_ec.c",
      .expected_flags = {{"--kernel_suspend_warning", "--weight=10"}}};
  KernelParser parser(true);
  ParserTest("TEST_WARNING", {suspend_warning}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningNested) {
  ParserRun nested_warning = {
      .expected_text =
          "fd6f0171-ttm_bo_mmap+0x19e/0x1ab [ttm]()\n"
          "/mnt/host/source/src/third_party/kernel/v3.18/drivers/gpu/drm/ttm/"
          "ttm_bo_vm.c:265 ttm_bo_mmap+0x19e/0x1ab [ttm]()\n"
          "[ 3955.309298] Modules linked in: cfg80211 snd_seq_midi "
          "snd_seq_midi_event snd_rawmidi snd_seq ip6table_filter "
          "snd_seq_device cirrus ttm\n"
          "[ 3955.309298] CPU: 2 PID: 750 Comm: Chrome_ProcessL Not tainted "
          "3.18.0 #55\n"
          "[ 3955.309298] Hardware name: QEMU Standard PC (i440FX + PIIX, "
          "1996), BIOS Bochs 01/01/2011\n"
          "[ 3955.309298]  0000000000000000 0000000000be519d ffff8800afed7d50 "
          "ffffffff8829b327\n"
          "[ 3955.309298]  0000000000000000 0000000000000000 ffff8800afed7d90 "
          "ffffffff87c62c3c\n"
          "[ 3955.309298]  00007f8198b3d000 ffffffffc010ccc0 ffff8800a93e9c00 "
          "ffff8800bb139a40\n"
          "[ 3955.309298] Call Trace:\n"
          "[ 3955.309298]  [<ffffffff8829b327>] dump_stack+0x4e/0x71\n"
          "[ 3955.309298]  [<ffffffff87c62c3c>] "
          "warn_slowpath_common+0x81/0x9b\n"
          "[ 3955.309298]  [<ffffffffc010ccc0>] ? ttm_bo_mmap+0x19e/0x1ab "
          "[ttm]\n"
          "[ 3955.309298]  [<ffffffff87c62d3f>] warn_slowpath_null+0x1a/0x1c\n"
          "[ 3955.309298]  [<ffffffffc010ccc0>] ttm_bo_mmap+0x19e/0x1ab [ttm]\n"
          "[ 3955.309298]  [<ffffffff87c61534>] "
          "copy_process.part.41+0xf2b/0x179a\n"
          "[ 3955.309298]  [<ffffffff87c61f49>] do_fork+0xc9/0x2c0\n"
          "[ 3955.309298]  [<ffffffff8829feb3>] ? "
          "_raw_spin_unlock_irq+0xe/0x22\n"
          "[ 3955.309298]  [<ffffffff87c6f978>] ? "
          "__set_current_blocked+0x49/0x4e\n"
          "[ 3955.309298]  [<ffffffff87c621ba>] SyS_clone+0x16/0x18\n"
          "[ 3955.309298]  [<ffffffff882a0869>] stub_clone+0x69/0x90\n"
          "[ 3955.309298]  [<ffffffff882a055c>] ? "
          "system_call_fastpath+0x1c/0x21\n"
          "[ 3955.309298] ------------[ cut here ]------------\n"
          "[ 3955.309298] BARNING: CPU: 2 PID: 750 at "
          "/mnt/host/source/src/third_party/kernel/v3.18/drivers/gpu/drm/ttm/"
          "ttm_bo_vm.c:265 ttm_bo_mmap+0x19e/0x1ab [ttm]()\n"
          "[ 3955.309298] (\"BARNING\" above is intentional)\n"
          "[ 3955.309298] Modules linked in: cfg80211 snd_seq_midi "
          "snd_seq_midi_event snd_rawmidi snd_seq ip6table_filter "
          "snd_seq_device cirrus ttm\n"
          "[ 3955.309298] CPU: 2 PID: 750 Comm: Chrome_ProcessL Not tainted "
          "3.18.0 #55\n"
          "[ 3955.309298] Hardware name: QEMU Standard PC (i440FX + PIIX, "
          "1996), BIOS Bochs 01/01/2011\n"
          "[ 3955.309298]  0000000000000000 0000000000be519d ffff8800afed7d50 "
          "ffffffff8829b327\n"
          "[ 3955.309298]  0000000000000000 0000000000000000 ffff8800afed7d90 "
          "ffffffff87c62c3c\n"
          "[ 3955.309298]  00007f8198b3d000 ffffffffc010ccc0 ffff8800a93e9c00 "
          "ffff8800bb139a40\n"
          "[ 3955.309298] Call Trace:\n"
          "[ 3955.309298]  [<ffffffff8829b327>] dump_stack+0x4e/0x71\n"
          "[ 3955.309298]  [<ffffffff87c62c3c>] "
          "warn_slowpath_common+0x81/0x9b\n"
          "[ 3955.309298]  [<ffffffffc010ccc0>] ? ttm_bo_mmap+0x19e/0x1ab "
          "[ttm]\n"
          "[ 3955.309298] ---[ end trace 3a3ab835b30b8933 ]---\n",
      .expected_flags = {{"--kernel_warning", "--weight=10"}}};
  KernelParser parser(true);
  ParserTest("TEST_WARNING_NESTED", {nested_warning}, &parser);
}

TEST(AnomalyDetectorTest, CrashReporterCrash_LessThanOrEqual_v6_6) {
  ParserRun crash_reporter_crash = {
      .expected_flags = {{"--crash_reporter_crashed"}}};
  KernelParser parser(true);
  ParserTest("TEST_CR_CRASH_LTOE_6_6", {crash_reporter_crash}, &parser);
}

TEST(AnomalyDetectorTest, CrashReporterCrash_GreatThanOrEqual_v6_12) {
  ParserRun crash_reporter_crash = {
      .expected_flags = {{"--crash_reporter_crashed"}}};
  KernelParser parser(true);
  ParserTest("TEST_CR_CRASH_GTOE_6_12", {crash_reporter_crash}, &parser);
}

TEST(AnomalyDetectorTest, CrashReporterCrashRateLimit_LessThanOrEqual_v6_6) {
  ParserRun crash_reporter_crash = {
      .expected_flags = {{"--crash_reporter_crashed"}}};
  KernelParser parser(true);
  ParserTest("TEST_CR_CRASH_LTOE_6_6", {crash_reporter_crash, empty, empty},
             &parser);
}

TEST(AnomalyDetectorTest, CrashReporterCrashRateLimit_GreatThanOrEqual_v6_12) {
  ParserRun crash_reporter_crash = {
      .expected_flags = {{"--crash_reporter_crashed"}}};
  KernelParser parser(true);
  ParserTest("TEST_CR_CRASH_GTOE_6_12", {crash_reporter_crash, empty, empty},
             &parser);
}

TEST(AnomalyDetectorTest, ServiceFailure) {
  ParserRun one{.expected_substr = "-exit2-"};
  ParserRun two{.find_this = "crash-crash", .replace_with = "fresh-fresh"};
  ServiceParser parser(true);
  ParserTest("TEST_SERVICE_FAILURE", {one, two}, &parser);
}

TEST(AnomalyDetectorTest, ServiceFailureArc) {
  ParserRun service_failure = {
      .find_this = "crash-crash",
      .replace_with = "arc-crash",
      .expected_substr = "-exit2-arc-",
      .expected_flags = {{"--arc_service_failure=arc-crash"}}};
  ServiceParser parser(true);
  ParserTest("TEST_SERVICE_FAILURE", {service_failure}, &parser);
}

TEST(AnomalyDetectorTest, ServiceFailureCamera) {
  ParserRun service_failure = {.find_this = "crash-crash",
                               .replace_with = "cros-camera",
                               .expected_size = 0};
  ServiceParser parser(true);
  ParserTest("TEST_SERVICE_FAILURE", {service_failure}, &parser);
}

TEST(AnomalyDetectorTest, SELinuxViolation) {
  ParserRun selinux_violation = {
      .expected_substr =
          "-selinux-u:r:cros_init:s0-u:r:kernel:s0-module_request-init-",
      .expected_flags = {{"--selinux_violation", "--weight=100"}}};
  SELinuxParser parser(true);
  ParserTest("TEST_SELINUX", {selinux_violation}, &parser);
}

TEST(AnomalyDetectorTest, SELinuxViolationPermissive) {
  ParserRun selinux_violation = {.find_this = "permissive=0",
                                 .replace_with = "permissive=1",
                                 .expected_size = 0};
  SELinuxParser parser(true);
  ParserTest("TEST_SELINUX", {selinux_violation}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningSuspend_v4_19_up) {
  ParserRun suspend_warning = {
      .expected_flags = {{"--kernel_suspend_warning", "--weight=10"}}};
  KernelParser parser(true);
  ParserTest("TEST_SUSPEND_WARNING_LOWERCASE", {suspend_warning}, &parser);
}

TEST(AnomalyDetectorTest, KernelWarningSuspendNoDuplicate_v4_19_up) {
  ParserRun identical_warning{.expected_size = 0};
  KernelParser parser(true);
  ParserTest("TEST_SUSPEND_WARNING_LOWERCASE", {simple_run, identical_warning},
             &parser);
}

// Verify that we skip non-CrOS selinux violations
TEST(AnomalyDetectorTest, SELinuxViolationNonCros) {
  ParserRun selinux_violation = {
      .find_this = "cros_init", .replace_with = "init", .expected_size = 0};
  SELinuxParser parser(true);
  ParserTest("TEST_SELINUX", {selinux_violation}, &parser);
}

TEST(AnomalyDetectorTest, SuspendFailure) {
  ParserRun suspend_failure = {
      .expected_substr =
          "-suspend failure: device: dummy_dev step: suspend errno: -22",
      .expected_flags = {{"--suspend_failure"}}};
  SuspendParser parser(true);
  ParserTest("TEST_SUSPEND_FAILURE", {suspend_failure}, &parser);
}

MATCHER_P2(SignalEq, interface, member, "") {
  return (arg->GetInterface() == interface && arg->GetMember() == member);
}

TEST(AnomalyDetectorTest, BTRFSExtentCorruption) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::MockBus> bus = new dbus::MockBus(std::move(options));

  auto obj_path = dbus::ObjectPath(anomaly_detector::kAnomalyEventServicePath);
  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus.get(), obj_path);

  EXPECT_CALL(*bus, GetExportedObject(Eq(obj_path)))
      .WillOnce(Return(exported_object.get()));
  EXPECT_CALL(*exported_object,
              SendSignal(SignalEq(
                  anomaly_detector::kAnomalyEventServiceInterface,
                  anomaly_detector::kAnomalyGuestFileCorruptionSignalName)))
      .Times(1);

  auto metrics = std::make_unique<NiceMock<MetricsLibraryMock>>();
  EXPECT_CALL(*metrics, SendCrosEventToUMA(_)).Times(0);

  TerminaParser parser(bus, std::move(metrics), /*testonly_send_all=*/true);

  parser.ParseLogEntryForBtrfs(
      3,
      "BTRFS warning (device vdb): csum failed root 5 ino 257 off 409600 csum "
      "0x76ad9387 expected csum 0xd8d34542 mirror 1");
}

TEST(AnomalyDetectorTest, BTRFSTreeCorruption) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::MockBus> bus = new dbus::MockBus(std::move(options));

  auto obj_path = dbus::ObjectPath(anomaly_detector::kAnomalyEventServicePath);
  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus.get(), obj_path);

  EXPECT_CALL(*bus, GetExportedObject(Eq(obj_path)))
      .Times(3)
      .WillRepeatedly(Return(exported_object.get()));
  EXPECT_CALL(*exported_object,
              SendSignal(SignalEq(
                  anomaly_detector::kAnomalyEventServiceInterface,
                  anomaly_detector::kAnomalyGuestFileCorruptionSignalName)))
      .Times(3);

  auto metrics = std::make_unique<NiceMock<MetricsLibraryMock>>();
  EXPECT_CALL(*metrics, SendCrosEventToUMA(_)).Times(0);

  TerminaParser parser(bus, std::move(metrics), /*testonly_send_all=*/true);

  // prior to 5.14
  parser.ParseLogEntryForBtrfs(
      3,
      "BTRFS warning (device vdb): vdb checksum verify failed "
      "on 122798080 wanted 4E5B4C99 found 5F261FEB level 0");

  // since 5.14
  parser.ParseLogEntryForBtrfs(
      3,
      "BTRFS warning (device vdb): checksum verify failed "
      "on 122798080 wanted 4E5B4C99 found 5F261FEB level 0");

  // since 6.0
  parser.ParseLogEntryForBtrfs(
      3,
      "BTRFS warning (device vdb): checksum verify failed "
      "on logical 122077184 mirror 1 wanted 0xd3d7da82 found 0x9ad50d66 "
      "level 0");
}

TEST(AnomalyDetectorTest, OomEvent) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::MockBus> bus = new dbus::MockBus(std::move(options));

  auto obj_path = dbus::ObjectPath(anomaly_detector::kAnomalyEventServicePath);
  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus.get(), obj_path);

  EXPECT_CALL(*bus, GetExportedObject(Eq(obj_path)))
      .WillOnce(Return(exported_object.get()));
  EXPECT_CALL(
      *exported_object,
      SendSignal(SignalEq(anomaly_detector::kAnomalyEventServiceInterface,
                          anomaly_detector::kAnomalyGuestOomEventSignalName)))
      .Times(1);

  auto metrics = std::make_unique<NiceMock<MetricsLibraryMock>>();
  EXPECT_CALL(*metrics, SendCrosEventToUMA("Crostini.OomEvent"))
      .WillOnce(Return(true));

  TerminaParser parser(bus, std::move(metrics), /*testonly_send_all=*/true);

  std::string oom_log =
      "Out of memory: Killed process 293 (python 3.6) total-vm:15633956kB, "
      "anon-rss:14596640kB, file-rss:4kB, shmem-rss:0kB, UID:0 "
      "pgtables:28628kB "
      "oom_score_adj:0";

  auto crash_report = parser.ParseLogEntryForOom(3, oom_log);

  EXPECT_THAT(crash_report->text,
              testing::HasSubstr("guest-oom-event-python_3_6"));
  EXPECT_THAT(crash_report->text, testing::HasSubstr(oom_log));

  std::vector<std::string> expected_flags = {"--guest_oom_event"};
  EXPECT_EQ(crash_report->flags, expected_flags);
}

TEST(AnomalyDetectorTest, CryptohomeMountFailure) {
  ParserRun cryptohome_mount_failure = {
      .expected_flags = {{"--mount_failure", "--mount_device=cryptohome"}}};
  CryptohomeParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CRYPTOHOME_MOUNT_FAILURE", {cryptohome_mount_failure},
             &parser);
}

TEST(AnomalyDetectorTest, CryptohomeIgnoreMountFailure) {
  ParserRun cryptohome_mount_failure = {.expected_size = 0};
  CryptohomeParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CRYPTOHOME_MOUNT_FAILURE_IGNORE", {cryptohome_mount_failure},
             &parser);
}

TEST(AnomalyDetectorTest, CryptohomeIgnoreFailedLogin) {
  ParserRun cryptohome_mount_failure = {.expected_size = 0};
  CryptohomeParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CRYPTOHOME_FAILED_LOGIN_IGNORE", {cryptohome_mount_failure},
             &parser);
}

TEST(AnomalyDetectorTest, CryptohomeRecoveryRequestFailure) {
  ParserRun cryptohome_recovery_failure = {
      .expected_substr = "GetRecoveryRequest-3-recovery-failure",
      .expected_flags = {{"--cryptohome_recovery_failure"}}};
  CryptohomeParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CRYPTOHOME_RECOVERY_REQUEST_FAILURE",
             {cryptohome_recovery_failure}, &parser);
}

TEST(AnomalyDetectorTest, CryptohomeRecoveryDeriveFailure) {
  ParserRun cryptohome_recovery_failure = {
      .expected_substr = "Derive-8-recovery-failure",
      .expected_flags = {{"--cryptohome_recovery_failure"}}};
  CryptohomeParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CRYPTOHOME_RECOVERY_DERIVE_FAILURE",
             {cryptohome_recovery_failure}, &parser);
}

TEST(AnomalyDetectorTest, CryptohomeRecoveryIgnoreFailure) {
  ParserRun no_failure = {.expected_size = 0};
  CryptohomeParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CRYPTOHOME_RECOVERY_NO_FAILURE", {no_failure}, &parser);
}

TEST(AnomalyDetectorTest, TcsdAuthFailure) {
  ParserRun tcsd_auth_failure = {.expected_text = "b349c715-auth failure\n",
                                 .expected_flags = {{"--auth_failure"}}};
  ParserTest<TcsdParser>("TEST_TCSD_AUTH_FAILURE", {tcsd_auth_failure});
}

TEST(AnomalyDetectorTest, TcsdAuthFailureBlocklist) {
  ParserRun tcsd_auth_failure = {.expected_size = 0};
  ParserTest<TcsdParser>("TEST_TCSD_AUTH_FAILURE_BLOCKLIST",
                         {tcsd_auth_failure});
}

TEST(AnomalyDetectorTest, CellularFailureMM) {
  ParserRun modem_failure = {
      .expected_substr = "Core.Failed",
      .expected_flags = {
          {"--modem_failure", base::StringPrintf("--weight=%d", 50)}}};
  ShillParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CELLULAR_FAILURE_MM", {modem_failure}, &parser);
}

TEST(AnomalyDetectorTest, CellularFailureEnable) {
  ParserRun enable_failure = {
      .expected_substr = "InProgress-enable",
      .expected_flags = {
          {"--modem_failure", base::StringPrintf("--weight=%d", 200)}}};
  ShillParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CELLULAR_FAILURE_ENABLE", {enable_failure}, &parser);
}

TEST(AnomalyDetectorTest, CellularFailureConnect) {
  ParserRun connect_failure = {
      .expected_substr = "auto-connect",
      .expected_flags = {
          {"--modem_failure", base::StringPrintf("--weight=%d", 5)}}};
  ShillParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CELLULAR_FAILURE_CONNECT", {connect_failure}, &parser);
}

TEST(AnomalyDetectorTest, CellularFailureBlocked) {
  ParserRun modem_failure = {.expected_size = 0};
  ShillParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CELLULAR_FAILURE_BLOCKED", {modem_failure}, &parser);
}

TEST(AnomalyDetectorTest, CellularFailureEntitlementCheck) {
  ParserRun entitlement_failure = {
      .expected_substr = "EntitlementCheckFailure-00101",
      .expected_flags = {
          {"--modem_failure", base::StringPrintf("--weight=%d", 1)}}};
  ShillParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CELLULAR_FAILURE_ENTITLEMENT_CHECK", {entitlement_failure},
             &parser);
}

TEST(AnomalyDetectorTest, CellularInvalidApnAnomaly) {
  ParserRun invalid_apn_anomaly = {
      .expected_substr = "InvalidApnAnomaly",
      .expected_flags = {
          {"--modem_failure", base::StringPrintf("--weight=%d", 1)}}};
  ShillParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_CELLULAR_INVALID_APN_ANOMALY", {invalid_apn_anomaly},
             &parser);
}

TEST(AnomalyDetectorTest, TetheringFailure) {
  ParserRun tethering_failure = {
      .expected_substr = "TetheringFailure",
      .expected_flags = {
          {"--tethering_failure", base::StringPrintf("--weight=%d", 2)}}};
  ShillParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_TETHERING_FAILURE", {tethering_failure}, &parser);
}

TEST(AnomalyDetectorTest, ESimInstallSendHttpsFailure) {
  ParserRun install_failure = {
      .expected_substr = "SendHttpsFailure",
      .expected_flags = {
          {"--hermes_failure", base::StringPrintf("--weight=%d", 5)}}};
  HermesParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_ESIM_INSTALL_SEND_HTTPS_FAILURE", {install_failure},
             &parser);
}

TEST(AnomalyDetectorTest, ESimInstallUnknownFailure) {
  ParserRun install_failure = {
      .expected_substr = "Unknown",
      .expected_flags = {
          {"--hermes_failure", base::StringPrintf("--weight=%d", 1)}}};
  HermesParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_ESIM_INSTALL_UNKNOWN_FAILURE", {install_failure}, &parser);
}

TEST(AnomalyDetectorTest, ESimInstallFailureBlocked) {
  ParserRun install_failure = {.expected_size = 0};
  HermesParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_ESIM_INSTALL_MALFORMED_RESPONSE", {install_failure},
             &parser);
}

TEST(AnomalyDetectorTest, CellularFailureModemfwd) {
  ParserRun modemfwd_failure = {
      .expected_substr = "dlcServiceReturnedErrorOnInstall",
      .expected_flags = {
          {"--modemfwd_failure", base::StringPrintf("--weight=%d", 50)}}};
  ModemfwdParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_MODEMFWD_FAILURE_ERROR", {modemfwd_failure}, &parser);
}

TEST(AnomalyDetectorTest, CellularFailureModemfwdSkipUpload) {
  ParserRun modemfwd_failure_non_cellular = {.expected_size = 0};
  ModemfwdParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_MODEMFWD_FAILURE_SKIP_UPLOAD_NON_LTE_SKU",
             {modemfwd_failure_non_cellular}, &parser);
  ParserTest("TEST_MODEMFWD_FAILURE_SKIP_UPLOAD_MODEM_NEVER_SEEN",
             {modemfwd_failure_non_cellular}, &parser);
}

TEST(AnomalyDetectorTest, BrowserHang) {
  ParserRun browser_hang = {
      .expected_substr = "browser_hang-20s",
      .expected_flags = {
          {"--browser_hang", base::StringPrintf("--weight=%d", 40)}}};
  SessionManagerParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_BROWSER_HANG", {browser_hang}, &parser);
}

TEST(AnomalyDetectorTest, DlcServiceFailure) {
  ParserRun dlc_service_failure = {
      .expected_substr = "failedInstallInUpdateEngine",
      .expected_flags = {
          {"--dlc_service_failure", base::StringPrintf("--weight=%d", 50)}}};
  DlcServiceParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_DLC_SERVICE_FAILURE", {dlc_service_failure}, &parser);
}

TEST(AnomalyDetectorTest, DlcServiceDbusDomainFailure) {
  ParserRun dlc_service_failure = {
      .expected_substr = "INTERNAL",
      .expected_flags = {
          {"--dlc_service_failure", base::StringPrintf("--weight=%d", 100)}}};
  DlcServiceParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_DLC_SERVICE_DBUS_DOMAIN_FAILURE", {dlc_service_failure},
             &parser);
}

TEST(AnomalyDetectorTest, DlcServiceDbusFailureInvalidDlc) {
  ParserRun dlc_service_failure = {
      .expected_substr = "INVALID_DLC",
      .expected_flags = {
          {"--dlc_service_failure", base::StringPrintf("--weight=%d", 10000)}}};
  DlcServiceParser parser(/*testonly_send_all=*/true);
  ParserTest("TEST_DLC_SERVICE_DBUS_FAILURE_INVALID_DLC", {dlc_service_failure},
             &parser);
}
