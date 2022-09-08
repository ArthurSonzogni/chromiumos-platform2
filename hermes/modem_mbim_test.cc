// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/modem_mbim.h"

#include <memory>

#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hermes/fake_euicc_manager.h"
#include "hermes/mock_executor.h"
#include "hermes/sgp_22.h"

using ::testing::_;

namespace hermes {

class MockModemManagerProxy : public ModemManagerProxyInterface {
 public:
  MOCK_METHOD(void,
              RegisterModemAppearedCallback,
              (base::OnceClosure cb),
              (override));
  MOCK_METHOD(void, WaitForModem, (base::OnceClosure cb), (override));

  MOCK_METHOD(std::string, GetPrimaryPort, (), (const, override));

  MOCK_METHOD(void, ScheduleUninhibit, (base::TimeDelta timeout), (override));
  MOCK_METHOD(void, WaitForModemAndInhibit, (ResultCallback cb), (override));
  MockModemManagerProxy() {
    ON_CALL(*this, WaitForModem).WillByDefault([](base::OnceClosure cb) {
      std::move(cb).Run();
    });
    ON_CALL(*this, GetPrimaryPort).WillByDefault([]() { return "wwan0"; });
  }
};

class FakeLibmbim : public LibmbimInterface {
 public:
  void MbimDeviceNew(GFile* file,
                     GCancellable* cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data) override {
    LOG(INFO) << __func__;
    callback(nullptr, nullptr, user_data);
  };

  MbimDevice* MbimDeviceNewFinish(GAsyncResult* res, GError** error) override {
    return new MbimDevice();
  };

  void MbimDeviceOpenFull(MbimDevice* self,
                          MbimDeviceOpenFlags flags,
                          guint timeout,
                          GCancellable* cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data) override {
    GTask* task;
    task = g_task_new(self, cancellable, nullptr, nullptr);
    g_task_return_boolean(task, TRUE);
    callback(reinterpret_cast<GObject*>(self),
             reinterpret_cast<GAsyncResult*>(task), user_data);
  }

  void MbimDeviceCommand(MbimDevice* self,
                         MbimMessage* message,
                         guint timeout,
                         GCancellable* cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data) override {
    callback(nullptr, nullptr, user_data);
  }

  MbimMessage* MbimDeviceCommandFinish(MbimDevice* self,
                                       GAsyncResult* res,
                                       GError** error) override {
    return mbim_message_new(nullptr, 0);
  }

  gboolean MbimMessageResponseGetResult(const MbimMessage* self,
                                        MbimMessageType expected,
                                        GError** error) override {
    return TRUE;
  }

  gboolean MbimMessageDeviceCapsResponseParse(
      const MbimMessage* message,
      MbimDeviceType* out_device_type,
      MbimCellularClass* out_cellular_class,
      MbimVoiceClass* out_voice_class,
      MbimSimClass* out_sim_class,
      MbimDataClass* out_data_class,
      MbimSmsCaps* out_sms_caps,
      MbimCtrlCaps* out_control_caps,
      guint32* out_max_sessions,
      gchar** out_custom_data_class,
      gchar** out_device_id,
      gchar** out_firmware_info,
      gchar** out_hardware_info,
      GError** error) override {
    g_strdup_printf("123");
    *out_device_id = g_strdup_printf("123");
    return TRUE;
  }

  gboolean MbimDeviceCheckMsMbimexVersion(
      MbimDevice* self,
      guint8 ms_mbimex_version_major,
      guint8 ms_mbimex_version_minor) override {
    return TRUE;
  }

  bool GetReadyState(MbimDevice* device,
                     bool is_notification,
                     MbimMessage* notification,
                     MbimSubscriberReadyState* ready_state) override {
    return true;
  }

  gboolean MbimMessageMsBasicConnectExtensionsSysCapsResponseParse(
      const MbimMessage* message,
      guint32* out_number_of_executors,
      guint32* out_number_of_slots,
      guint32* out_concurrency,
      guint64* out_modem_id,
      GError** error) override {
    *out_number_of_slots = 2;
    *out_number_of_executors = 1;
    return TRUE;
  }

  gboolean MbimMessageMsBasicConnectExtensionsDeviceSlotMappingsResponseParse(
      const MbimMessage* message,
      guint32* out_map_count,
      MbimSlotArray** out_slot_map,
      GError** error) override {
    *out_map_count = 1;
    MbimSlotArray* out;
    out = g_new0(MbimSlot*, 2);
    MbimSlot* mbim_slot = g_new0(MbimSlot, 1);
    mbim_slot->slot = 0;
    out[0] = mbim_slot;
    *out_slot_map = out;
    return TRUE;
  }

  gboolean MbimMessageMsBasicConnectExtensionsSlotInfoStatusResponseParse(
      const MbimMessage* message,
      guint32* out_slot_index,
      MbimUiccSlotState* out_state,
      GError** error) override {
    static guint32 slot_index = 0;
    *out_slot_index = slot_index;
    *out_state = slot_index == 0 ? MBIM_UICC_SLOT_STATE_EMPTY
                                 : MBIM_UICC_SLOT_STATE_ACTIVE_ESIM;
    slot_index++;
    return TRUE;
  }
};

class ModemMbimTest : public testing::Test {
 protected:
  void SetUp() override {
    modem_manager_proxy_ = std::make_unique<MockModemManagerProxy>();
    libmbim_ = std::make_unique<FakeLibmbim>();
  }
  void TearDown() override { modem_.reset(); }

  void CreateModem() {
    modem_ = ModemMbim::Create(nullptr, &executor_, std::move(libmbim_),
                               std::move(modem_manager_proxy_));
    ASSERT_NE(modem_, nullptr);
  }

  MockExecutor executor_;
  std::unique_ptr<ModemMbim> modem_;
  FakeEuiccManager euicc_manager_;
  std::unique_ptr<FakeLibmbim> libmbim_;
  std::unique_ptr<MockModemManagerProxy> modem_manager_proxy_;
};

// Initializes the modem and expects the initialization to be successful.
TEST_F(ModemMbimTest, Smoke) {
  int err = kModemMessageProcessingError;
  auto cb =
      base::BindOnce([](int* test_err, int err) { *test_err = err; }, &err);
  EXPECT_CALL(euicc_manager_, OnEuiccUpdated(_, _));
  EXPECT_CALL(*modem_manager_proxy_, WaitForModem(_));
  CreateModem();
  modem_->Initialize(&euicc_manager_, std::move(cb));
  EXPECT_EQ(err, kModemSuccess);
}

}  // namespace hermes
