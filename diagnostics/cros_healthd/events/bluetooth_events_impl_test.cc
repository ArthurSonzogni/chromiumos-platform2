// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"

#include <base/test/task_environment.h>
#include <brillo/variant_dictionary.h>
#include <dbus/object_path.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/events/event_observer_test_future.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Tests for the BluetoothEventsImpl class.
class BluetoothEventsImplTest : public testing::Test {
 protected:
  BluetoothEventsImplTest() = default;
  BluetoothEventsImplTest(const BluetoothEventsImplTest&) = delete;
  BluetoothEventsImplTest& operator=(const BluetoothEventsImplTest&) = delete;

  void SetUp() override {
    bluetooth_events_impl_.AddObserver(observer_.BindNewPendingRemote());
  }

  FakeBluezEventHub* fake_bluez_event_hub() const {
    return mock_context_.fake_bluez_event_hub();
  }

  FakeFlossEventHub* fake_floss_event_hub() const {
    return mock_context_.fake_floss_event_hub();
  }

  void WaitAndCheckEvent(mojom::BluetoothEventInfo::State state) {
    auto info = observer_.WaitForEvent();
    ASSERT_TRUE(info->is_bluetooth_event_info());
    const auto& bluetooth_event_info = info->get_bluetooth_event_info();
    EXPECT_EQ(bluetooth_event_info->state, state);
  }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  BluetoothEventsImpl bluetooth_events_impl_{&mock_context_};
  EventObserverTestFuture observer_;
};

}  // namespace

// Test that we can receive an adapter added event via Bluez proxy.
TEST_F(BluetoothEventsImplTest, ReceiveBluezAdapterAddedEvent) {
  fake_bluez_event_hub()->SendAdapterAdded();
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kAdapterAdded);
}

// Test that we can receive an adapter removed event via Bluez proxy.
TEST_F(BluetoothEventsImplTest, ReceiveBluezAdapterRemovedEvent) {
  fake_bluez_event_hub()->SendAdapterRemoved();
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kAdapterRemoved);
}

// Test that we can receive an adapter property changed event via Bluez proxy.
TEST_F(BluetoothEventsImplTest, ReceiveBluezAdapterPropertyChangedEvent) {
  fake_bluez_event_hub()->SendAdapterPropertyChanged();
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kAdapterPropertyChanged);
}

// Test that we can receive a device added event via Bluez proxy.
TEST_F(BluetoothEventsImplTest, ReceiveBluezDeviceAddedEvent) {
  fake_bluez_event_hub()->SendDeviceAdded();
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kDeviceAdded);
}

// Test that we can receive a device removed event via Bluez proxy.
TEST_F(BluetoothEventsImplTest, ReceiveBluezDeviceRemovedEvent) {
  fake_bluez_event_hub()->SendDeviceRemoved();
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kDeviceRemoved);
}

// Test that we can receive a device property changed event via Bluez proxy.
TEST_F(BluetoothEventsImplTest, ReceiveBluezDevicePropertyChangedEvent) {
  fake_bluez_event_hub()->SendDevicePropertyChanged();
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kDevicePropertyChanged);
}

// Test that we can receive an adapter added event via Floss proxy.
TEST_F(BluetoothEventsImplTest, ReceiveFlossAdapterAddedEvent) {
  fake_floss_event_hub()->SendAdapterAdded();
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kAdapterAdded);
}

// Test that we can receive an adapter removed event via Floss proxy.
TEST_F(BluetoothEventsImplTest, ReceiveFlossAdapterRemovedEvent) {
  fake_floss_event_hub()->SendAdapterRemoved(dbus::ObjectPath(""));
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kAdapterRemoved);
}

// Test that we can receive an adapter property changed event via Floss proxy.
TEST_F(BluetoothEventsImplTest, ReceiveFlossAdapterPropertyChangedEvent) {
  dbus::ObjectPath path{""};
  std::vector<uint32_t> properties = {0x0,  0x1,  0x2,  0x3,  0xA,  0xB,
                                      0x15, 0x18, 0x20, 0x50, 0XFE, 0xFF};
  for (const auto property : properties) {
    fake_floss_event_hub()->SendAdapterPropertyChanged(path, property);
  }
  fake_floss_event_hub()->SendAdapterDiscoveringChanged(path,
                                                        /*discovering=*/true);
  // One to check for adapter discovering change events.
  for (int i = 0; i < properties.size() + 1; ++i) {
    WaitAndCheckEvent(
        mojom::BluetoothEventInfo::State::kAdapterPropertyChanged);
  }
}

// Test that we can receive a device added event via Floss proxy.
TEST_F(BluetoothEventsImplTest, ReceiveFlossDeviceAddedEvent) {
  fake_floss_event_hub()->SendDeviceAdded(brillo::VariantDictionary());
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kDeviceAdded);
}

// Test that we can receive a device removed event via Floss proxy.
TEST_F(BluetoothEventsImplTest, ReceiveFlossDeviceRemovedEvent) {
  fake_floss_event_hub()->SendDeviceRemoved(brillo::VariantDictionary());
  WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kDeviceRemoved);
}

// Test that we can receive a device property changed event via Floss proxy.
TEST_F(BluetoothEventsImplTest, ReceiveFlossDevicePropertyChangedEvent) {
  brillo::VariantDictionary device;
  std::vector<uint32_t> properties = {0x0,  0x1,  0x2,  0x3,  0xA,  0xB,
                                      0x15, 0x18, 0x20, 0x50, 0XFE, 0xFF};
  fake_floss_event_hub()->SendDevicePropertiesChanged(device, properties);
  fake_floss_event_hub()->SendDeviceConnectedChanged(device,
                                                     /*connected=*/true);
  // One to check for device connected change events.
  for (int i = 0; i < properties.size() + 1; ++i) {
    WaitAndCheckEvent(mojom::BluetoothEventInfo::State::kDevicePropertyChanged);
  }
}

}  // namespace diagnostics
