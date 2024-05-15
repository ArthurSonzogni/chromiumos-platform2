// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "primary_io_manager/primary_io_manager.h"

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "dbus/mock_bus.h"
#include "primary_io_manager/fake_udev.h"

#include <gtest/gtest.h>

namespace primary_io_manager {

class PrimaryIoManagerTest : public testing::Test {
 public:
  PrimaryIoManagerTest() {
    manager_ = std::make_unique<PrimaryIoManager>(
        base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options{}),
        FakeUdevFactory());
  }
  PrimaryIoManagerTest(const PrimaryIoManagerTest&) = delete;
  PrimaryIoManagerTest& operator=(const PrimaryIoManagerTest&) = delete;

  ~PrimaryIoManagerTest() = default;

  std::unique_ptr<PrimaryIoManager> manager_;

 private:
  std::unique_ptr<FakeUdev> udev_mock_;
};

TEST_F(PrimaryIoManagerTest, EmptyManager) {
  ASSERT_EQ(manager_->io_devices_.size(), 0);

  ASSERT_EQ(manager_->primary_mouse_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_, nullptr);
}

TEST_F(PrimaryIoManagerTest, AddMouse) {
  manager_->AddMouse("/dev/usb/3-2", "cool mouse");

  ASSERT_EQ(manager_->io_devices_.size(), 1);

  ASSERT_NE(manager_->primary_mouse_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_, nullptr);
  ASSERT_EQ(manager_->primary_mouse_->mouse, PRIMARY);
  ASSERT_EQ(manager_->primary_mouse_->keyboard, NONE);
}

TEST_F(PrimaryIoManagerTest, AddKeyboard) {
  manager_->AddKeyboard("/dev/usb/3-2", "cool keyboard");

  ASSERT_EQ(manager_->io_devices_.size(), 1);

  ASSERT_EQ(manager_->primary_mouse_, nullptr);
  ASSERT_NE(manager_->primary_keyboard_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_->mouse, NONE);
  ASSERT_EQ(manager_->primary_keyboard_->keyboard, PRIMARY);
}

TEST_F(PrimaryIoManagerTest, AddKeyboardAndMouse) {
  manager_->AddKeyboard("/dev/usb/3-2", "cool keyboard");
  manager_->AddMouse("/dev/usb/3-1", "cool mouse");

  ASSERT_EQ(manager_->io_devices_.size(), 2);

  ASSERT_NE(manager_->primary_keyboard_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_->keyboard, PRIMARY);
  ASSERT_EQ(manager_->primary_keyboard_->mouse, NONE);

  ASSERT_NE(manager_->primary_mouse_, nullptr);
  ASSERT_EQ(manager_->primary_mouse_->mouse, PRIMARY);
  ASSERT_EQ(manager_->primary_mouse_->keyboard, NONE);
}

TEST_F(PrimaryIoManagerTest, AddKeyboardAndMouseSameDevice) {
  manager_->AddKeyboard("/dev/usb/3-1", "cool keyboard");
  manager_->AddMouse("/dev/usb/3-1", "cool mouse");

  ASSERT_EQ(manager_->io_devices_.size(), 1);

  ASSERT_NE(manager_->primary_keyboard_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_->keyboard, PRIMARY);
  ASSERT_EQ(manager_->primary_keyboard_->mouse, PRIMARY);

  ASSERT_NE(manager_->primary_mouse_, nullptr);
  ASSERT_EQ(manager_->primary_mouse_->keyboard, PRIMARY);
  ASSERT_EQ(manager_->primary_mouse_->mouse, PRIMARY);

  ASSERT_EQ(manager_->primary_keyboard_, manager_->primary_mouse_);
}

TEST_F(PrimaryIoManagerTest, AddAndRemoveDevices) {
  std::string mouse = "/dev/usb/3-1";
  std::string keyboard = "/dev/usb/3-2";
  manager_->AddKeyboard(keyboard, "cool keyboard");
  manager_->AddMouse(mouse, "cool mouse");
  manager_->RemoveDevice(keyboard);

  ASSERT_EQ(manager_->io_devices_.size(), 1);

  ASSERT_NE(manager_->primary_mouse_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_, nullptr);
  ASSERT_EQ(manager_->primary_mouse_->mouse, PRIMARY);
  ASSERT_EQ(manager_->primary_mouse_->keyboard, NONE);

  // just-removed device
  manager_->RemoveDevice(keyboard);
  // non-existent device
  manager_->RemoveDevice("/dev/usb/3-3");
}

TEST_F(PrimaryIoManagerTest, TwoMice_RemovePrimary) {
  std::string primary_mouse = "/dev/usb/3-1";
  std::string secondary_mouse = "/dev/usb/3-2";
  manager_->AddMouse(primary_mouse, "cool mouse");
  manager_->AddMouse(secondary_mouse, "cooler mouse");

  ASSERT_EQ(manager_->io_devices_.size(), 2);

  ASSERT_NE(manager_->primary_mouse_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_, nullptr);
  ASSERT_EQ(manager_->primary_mouse_->mouse, PRIMARY);
  ASSERT_EQ(manager_->primary_mouse_->keyboard, NONE);

  ASSERT_EQ(manager_->io_devices_.at(primary_mouse)->mouse, PRIMARY);
  ASSERT_EQ(manager_->io_devices_.at(secondary_mouse)->mouse, NONPRIMARY);

  manager_->RemoveDevice(primary_mouse);

  ASSERT_EQ(manager_->io_devices_.size(), 1);

  ASSERT_NE(manager_->primary_mouse_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_, nullptr);
  ASSERT_EQ(manager_->primary_mouse_->mouse, PRIMARY);
  ASSERT_EQ(manager_->primary_mouse_->keyboard, NONE);

  ASSERT_TRUE(manager_->io_devices_.contains(secondary_mouse));
  ASSERT_FALSE(manager_->io_devices_.contains(primary_mouse));
}

TEST_F(PrimaryIoManagerTest, TwoMice_RemoveSecondary) {
  std::string primary_mouse = "/dev/usb/3-1";
  std::string secondary_mouse = "/dev/usb/3-2";
  manager_->AddMouse(primary_mouse, "cool mouse");
  manager_->AddMouse(secondary_mouse, "cooler mouse");

  ASSERT_EQ(manager_->io_devices_.size(), 2);

  ASSERT_NE(manager_->primary_mouse_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_, nullptr);
  ASSERT_EQ(manager_->primary_mouse_->mouse, PRIMARY);
  ASSERT_EQ(manager_->primary_mouse_->keyboard, NONE);

  ASSERT_EQ(manager_->io_devices_.at(primary_mouse)->mouse, PRIMARY);
  ASSERT_EQ(manager_->io_devices_.at(secondary_mouse)->mouse, NONPRIMARY);

  manager_->RemoveDevice(secondary_mouse);

  ASSERT_EQ(manager_->io_devices_.size(), 1);

  ASSERT_NE(manager_->primary_mouse_, nullptr);
  ASSERT_EQ(manager_->primary_keyboard_, nullptr);
  ASSERT_EQ(manager_->primary_mouse_->mouse, PRIMARY);
  ASSERT_EQ(manager_->primary_mouse_->keyboard, NONE);

  ASSERT_FALSE(manager_->io_devices_.contains(secondary_mouse));
  ASSERT_TRUE(manager_->io_devices_.contains(primary_mouse));
}

}  // namespace primary_io_manager
