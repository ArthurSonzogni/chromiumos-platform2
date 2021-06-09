// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_KEY_READER_H_
#define MINIOS_KEY_READER_H_

#include <linux/input.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <xkbcommon/xkbcommon.h>

#include "minios/process_manager.h"

namespace minios {

constexpr int kMaxInputLength = 64;

// Increasing `kBackspaceSensitivity` will slow backspace speed.
constexpr int kBackspaceSensitivity = 2;

// Key values.
extern const int kKeyUp;
extern const int kKeyDown;
extern const int kKeyEnter;
// Key values for detachable.
extern const int kKeyVolUp;
extern const int kKeyVolDown;
extern const int kKeyPower;

// Key state parameters.
extern const int kFdsMax;
extern const int kKeyMax;

class KeyReader {
 public:
  // Default constructor can only access EvWaitForKeys.
  explicit KeyReader(bool include_usb);

  KeyReader(bool include_usb, std::string vpd_region);

  ~KeyReader();

  class Delegate {
   public:
    // Keeps track of key states based on the file descriptor index and whether
    // the key event is a key press or key release event as given by
    // `key_released`. `key_changed` is the ev code for the key. Only records
    // key state for valid keys.
    virtual void OnKeyPress(int fd_index,
                            int key_changed,
                            bool key_released) = 0;
  };

  // Initializes the `epfd_` and sets the callback. Listens for input keys based
  // on whether the device is detachable or not. Returns false on error.
  bool Init(const std::vector<int>& valid_keys);

  void SetDelegate(Delegate* delegate) { delegate_ = delegate; }

  // Creates the correct keyboard layout for a given country code.
  // Returns false for invalid keyboard layout, true otherwise.
  bool SetKeyboardContext();

  // Given a key code, does all the setup finding the available fds and events
  // and creating the proper keyboard layout.
  bool InputSetUp();

  // Sets 'enter' to true after return key press is recorded. Press tab
  // to toggle between showing and hiding passwords. Returns false on error.
  // This is a blocking call, and any active watchers must be disabled for the
  // duration of this function.
  bool GetUserInput(bool* enter, bool* tab_toggle, std::string* user_input);

  // Sets the watcher to the `epfd`, has a callback to `OnKeyEvent`.
  bool StartWatcher();

  // Stops the watcher.
  void StopWatcher();

  // Get XKB keyboard layout based on the VPD region. Return false on error.
  bool MapRegionToKeyboard(std::string* xkb_layout);

  // Wrapper that does not take in tab toggle key. Used for testing.
  bool GetCharForTest(const struct input_event& ev);

  // Returns the current key input as a string. Used for testing.
  std::string GetUserInputForTest();

  void SetRootForTest(const base::FilePath& test_root) { root_ = test_root; }

 private:
  // Checks whether all the keys in `keys_` are supported by the fd. Returns
  // false on failure.
  bool SupportsAllKeys(const int fd);

  // Checks all the valid files under `kDevInputEvent`, stores the valid
  // keyboard devices to `fds_`. Will check if all keys are supported if input
  // is true. Returns false if there are no available file descriptors.
  virtual bool GetValidFds(bool check_supported_keys);

  // Creates the epoll and gets event data. Sets epoll file descriptor and on
  // returns true on success.
  virtual bool EpollCreate(base::ScopedFD* epfd);

  // Waits for a valid key event and reads it into the input event struct. Sets
  // fd index and returns true on success.
  virtual bool GetEpEvent(int epfd, struct input_event* ev, int* index);

  // Get epoll event using `GetEpEvent`. If the event is a key event and is for
  // a valid key, it and calls the Delegate `OnKeyPress` function to modify the
  // screen and index.
  virtual void OnKeyEvent();

  // GetChar takes in an input event and adds to user input if the key press
  // is a valid, printable ASCII. Pressing tab key toggles boolean. Returns
  // false after enter key press, true otherwise.
  bool GetChar(const struct input_event& ev, bool* tab_toggle);

  std::string user_input_;
  // Counts and aggregates repeated backspace key events.
  int backspace_counter_;
  // Checks that enter key down was recorded before returning on key up.
  bool return_pressed_;
  // Whether or not to include USB connections when scanning for events.
  bool include_usb_;
  // Keyboard layout for xkb common;
  std::string vpd_region_;
  // Stores open event connections.
  std::vector<base::ScopedFD> fds_;
  // Stores epoll file descriptor.
  base::ScopedFD epfd_;
  // Default root directory.
  base::FilePath root_;

  // Watches the epoll file descriptor and calls `OnKeyEvent`.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

  // Allows class to only access the `EvWaitForKey` function. `GetInput` will
  // return false.
  bool use_only_evwaitkey_;

  // A list of keys to listen for on the blocking call.
  std::vector<int> keys_;

  Delegate* delegate_;

  // XKB common keyboard layout members.
  struct xkb_context* ctx_{nullptr};
  struct xkb_rule_names names_;
  struct xkb_keymap* keymap_{nullptr};
  struct xkb_state* state_{nullptr};
};

}  // namespace minios

#endif  // MINIOS_KEY_READER_H_
