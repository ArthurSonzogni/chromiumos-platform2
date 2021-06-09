// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/key_reader.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/scoped_file.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>

namespace minios {

// Key values.
const int kKeyUp = 103;
const int kKeyDown = 108;
const int kKeyEnter = 28;
const int kKeyVolUp = 115;
const int kKeyVolDown = 114;
const int kKeyPower = 116;

// Key state parameters.
const int kFdsMax = 10;
const int kKeyMax = 200;

namespace {
constexpr char kDevInputEvent[] = "/dev/input";
constexpr char kEventDevName[] = "*event*";
constexpr char kXkbPathName[] = "/usr/share/X11/xkb";

// Offset between xkb layout codes and ev key codes.
constexpr int kXkbOffset = 8;

// Determines if the given |bit| is set in the |bitmask| array.
bool TestBit(const int bit, const uint8_t* bitmask) {
  return (bitmask[bit / 8] >> (bit % 8)) & 1;
}

bool IsUsbDevice(const int fd) {
  struct input_id id;
  if (ioctl(fd, EVIOCGID, &id) == -1) {
    PLOG(ERROR) << "Failed to ioctl to determine device bus";
    return false;
  }

  return id.bustype == BUS_USB;
}

bool IsKeyboardDevice(const int fd) {
  uint8_t evtype_bitmask[EV_MAX / 8 + 1];
  if (ioctl(fd, EVIOCGBIT(0, sizeof(evtype_bitmask)), evtype_bitmask) == -1) {
    PLOG(ERROR) << "Failed to ioctl to determine supported event types";
    return false;
  }

  // The device is a "keyboard" if it supports EV_KEY events. Though, it is not
  // necessarily a real keyboard; EV_KEY events could also be e.g. volume
  // up/down buttons on a device.
  return TestBit(EV_KEY, evtype_bitmask);
}

}  // namespace

KeyReader::KeyReader(bool include_usb)
    : include_usb_(include_usb),
      root_("/"),
      use_only_evwaitkey_(true),
      delegate_(nullptr) {}

KeyReader::KeyReader(bool include_usb, std::string vpd_region)
    : backspace_counter_(0),
      return_pressed_(false),
      include_usb_(include_usb),
      vpd_region_(vpd_region),
      root_("/"),
      use_only_evwaitkey_(false) {
  user_input_.reserve(kMaxInputLength);
}

KeyReader::~KeyReader() {
  // Release xkb references.
  if (ctx_ != nullptr) {
    xkb_state_unref(state_);
    xkb_keymap_unref(keymap_);
    xkb_context_unref(ctx_);
  }
}

bool KeyReader::Init(const std::vector<int>& valid_keys) {
  keys_ = valid_keys;
  if (!GetValidFds(/*check_supported_keys=*/true)) {
    LOG(ERROR) << "No valid input devices found.";
    return false;
  }
  if (!EpollCreate(&epfd_)) {
    PLOG(ERROR) << " EpollCreate failed, cannot watch epfd.";
    return false;
  }
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      epfd_.get(),
      base::BindRepeating(&KeyReader::OnKeyEvent, base::Unretained(this)));
  if (!watcher_) {
    LOG(ERROR) << "Failed to watch epoll fd.";
    return false;
  }
  return true;
}

bool KeyReader::SupportsAllKeys(const int fd) {
  uint8_t key_bitmask[KEY_MAX / 8 + 1];
  if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bitmask)), key_bitmask) == -1) {
    PLOG(ERROR) << "Failed to ioctl to determine supported key events";
    return false;
  }

  for (const auto& key : keys_) {
    if (!TestBit(key, key_bitmask))
      return false;
  }
  return true;
}

bool KeyReader::GetValidFds(bool check_supported_keys) {
  fds_.clear();
  base::FileEnumerator file_enumerator(base::FilePath(kDevInputEvent), true,
                                       base::FileEnumerator::FILES,
                                       FILE_PATH_LITERAL(kEventDevName));

  for (base::FilePath dir_path = file_enumerator.Next(); !dir_path.empty();
       dir_path = file_enumerator.Next()) {
    base::ScopedFD fd(open(dir_path.value().c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.is_valid()) {
      continue;
    }

    if ((include_usb_ || !IsUsbDevice(fd.get())) &&
        IsKeyboardDevice(fd.get())) {
      if (!check_supported_keys || SupportsAllKeys(fd.get())) {
        fds_.push_back(std::move(fd));
      }
    }
  }
  return !fds_.empty();
}

bool KeyReader::EpollCreate(base::ScopedFD* epfd) {
  *epfd = base::ScopedFD(epoll_create1(EPOLL_CLOEXEC));
  if (epfd->get() < 0) {
    PLOG(ERROR) << "Epoll_create failed";
    return false;
  }

  for (int i = 0; i < fds_.size(); ++i) {
    struct epoll_event ep_event {
      .events = EPOLLIN, .data.u32 = static_cast<uint32_t>(i),
    };
    if (epoll_ctl(epfd->get(), EPOLL_CTL_ADD, fds_[i].get(), &ep_event) < 0) {
      PLOG(ERROR) << "Epoll_ctl failed";
      return false;
    }
  }
  return true;
}

bool KeyReader::GetEpEvent(int epfd, struct input_event* ev, int* index) {
  struct epoll_event ep_event;
  if (epoll_wait(epfd, &ep_event, 1, -1) <= 0) {
    PLOG(ERROR) << "epoll_wait failed";
    return false;
  }
  *index = ep_event.data.u32;
  if (read(fds_[*index].get(), ev, sizeof(*ev)) != sizeof(*ev)) {
    PLOG(ERROR) << "Could not read event";
    return false;
  }
  return true;
}

void KeyReader::OnKeyEvent() {
  struct input_event ev;
  int index = 0;
  if (!GetEpEvent(epfd_.get(), &ev, &index)) {
    PLOG(ERROR) << "Could not get event";
    return;
  }
  if (ev.type != EV_KEY || ev.code > KEY_MAX) {
    return;
  }

  if (std::find(keys_.begin(), keys_.end(), ev.code) == keys_.end()) {
    return;
  }

  if (!delegate_) {
    LOG(ERROR) << "Delegate not initialized.";
    return;
  }

  delegate_->OnKeyPress(index, ev.code, (ev.value == 0));
}

bool KeyReader::SetKeyboardContext() {
  // Set xkb layout and get keymap.
  ctx_ = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES);
  if (!ctx_) {
    LOG(ERROR) << "Unable to get new xkb context.";
    return false;
  }
  if (!xkb_context_include_path_append(ctx_, kXkbPathName)) {
    LOG(ERROR) << "Cannot add path " << kXkbPathName << " to context.";
    return false;
  }
  std::string country_code;
  if (!MapRegionToKeyboard(&country_code)) {
    LOG(WARNING)
        << "Could not find xkb layout for given region. Defaulting to US.";
    country_code = "us";
  }
  names_ = {.layout = country_code.c_str()};
  keymap_ =
      xkb_keymap_new_from_names(ctx_, &names_, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (keymap_ == nullptr) {
    LOG(ERROR) << "No matching keyboard for " << country_code
               << ". Make sure the two letter country code is valid.";
    return false;
  }
  state_ = xkb_state_new(keymap_);
  if (!state_) {
    LOG(ERROR) << "Unable to get xkbstate for " << country_code;
    return false;
  }
  return true;
}

bool KeyReader::InputSetUp() {
  if (use_only_evwaitkey_) {
    LOG(ERROR) << "Please construct the class with include_usb and "
                  "country_code in order to correctly use this function.";
    return false;
  }

  if (!GetValidFds(/*check_supported_keys=*/false)) {
    LOG(ERROR) << "No valid input devices found.";
    return false;
  }

  if (!EpollCreate(&epfd_)) {
    return false;
  }

  if (!SetKeyboardContext()) {
    return false;
  }
  return true;
}

bool KeyReader::GetChar(const struct input_event& ev, bool* tab_toggle) {
  xkb_keycode_t keycode = ev.code + kXkbOffset;
  xkb_keysym_t sym = xkb_state_key_get_one_sym(state_, keycode);
  if (ev.value == 0) {
    // Key up event.
    if (sym == XKB_KEY_Return && return_pressed_) {
      // Only end if RETURN key press was already recorded.
      return false;
    } else if (sym == XKB_KEY_Tab) {
      *tab_toggle = !(*tab_toggle);
    }

    // Put char representation in buffer.
    int size = xkb_state_key_get_utf8(state_, keycode, nullptr, 0) + 1;
    std::vector<char> buff(size);
    xkb_state_key_get_utf8(state_, keycode, buff.data(), size);

    if (sym == XKB_KEY_BackSpace && !user_input_.empty()) {
      user_input_.pop_back();
    } else if (isprint(buff[0]) && user_input_.size() < kMaxInputLength) {
      // Only printable ASCII characters stored in output.
      user_input_.push_back(buff[0]);
    }
    xkb_state_update_key(state_, keycode, XKB_KEY_UP);
  } else if (ev.value == 1) {
    // Key down event.
    if (sym == XKB_KEY_Return)
      return_pressed_ = true;

    xkb_state_update_key(state_, keycode, XKB_KEY_DOWN);

  } else if (ev.value == 2) {
    // Long press or repeating key event.
    if (sym == XKB_KEY_BackSpace && !user_input_.empty() &&
        ++backspace_counter_ >= kBackspaceSensitivity) {
      // Remove characters until empty.
      user_input_.pop_back();
      backspace_counter_ = 0;
    }
  }
  return true;
}

bool KeyReader::GetUserInput(bool* enter,
                             bool* tab_toggle,
                             std::string* user_input) {
  struct input_event ev;
  int index = 0;
  if (!GetEpEvent(epfd_.get(), &ev, &index)) {
    PLOG(ERROR) << "Could not get event";
    return false;
  }

  if (ev.type != EV_KEY || ev.code > KEY_MAX) {
    return true;
  }
  // Take in ev event and add to user input as appropriate.
  // Returns false to indicate enter was pressed.
  if (!GetChar(ev, tab_toggle)) {
    *enter = true;
  }
  *user_input = user_input_;
  return true;
}

bool KeyReader::MapRegionToKeyboard(std::string* xkb_layout) {
  std::string cros_region_json;
  if (!ReadFileToString(root_.Append("usr/share/misc/cros-regions.json"),
                        &cros_region_json)) {
    PLOG(ERROR) << "Could not read JSON mapping from cros-regions.json.";
    return false;
  }

  base::JSONReader::ValueWithError json_output =
      base::JSONReader::ReadAndReturnValueWithError(cros_region_json);
  if (!json_output.value || !json_output.value->is_dict()) {
    LOG(ERROR) << "Could not read json. " << json_output.error_message;
    return false;
  }

  // Look up mapping between vpd region and xkb keyboard layout.
  base::Value* region_info = json_output.value->FindDictKey(vpd_region_);
  if (!region_info) {
    LOG(ERROR) << "Region " << vpd_region_ << " not found.";
    return false;
  }

  const base::Value* kKeyboard = region_info->FindListKey("keyboards");
  if (!kKeyboard || kKeyboard->GetList().empty()) {
    LOG(ERROR) << "Could not retrieve keyboards for given region "
               << vpd_region_
               << ". Available region information: " << *region_info;
    return false;
  }

  // Always use the first keyboard in the list.
  const auto keyboard_parts =
      base::SplitString(kKeyboard->GetList()[0].GetString(), ":",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (keyboard_parts.size() < 2) {
    LOG(ERROR) << "Could not parse keyboard information for region  "
               << vpd_region_;
    return false;
  }
  *xkb_layout = keyboard_parts[1];
  return true;
}

bool KeyReader::GetCharForTest(const struct input_event& ev) {
  bool tab_key = false;
  return GetChar(ev, &tab_key);
}

std::string KeyReader::GetUserInputForTest() {
  return user_input_;
}

}  // namespace minios
