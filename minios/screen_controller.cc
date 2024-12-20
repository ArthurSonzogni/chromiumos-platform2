// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screen_controller.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/time/time.h>
#include <brillo/message_loops/message_loop.h>

#include "minios/error.h"
#include "minios/key_reader.h"
#include "minios/metrics_reporter.h"
#include "minios/recovery_installer.h"
#include "minios/screen_types.h"
#include "minios/screens/screen_debug_options.h"
#include "minios/screens/screen_download.h"
#include "minios/screens/screen_error.h"
#include "minios/screens/screen_language_dropdown.h"
#include "minios/screens/screen_log.h"
#include "minios/screens/screen_network.h"
#include "minios/screens/screen_permission.h"
#include "minios/screens/screen_welcome.h"
#include "minios/utils.h"

namespace minios {

namespace {

const base::FilePath kCurrentDisplayPath{"/run/frecon/current"};
const base::FilePath kUiDisplayPath{"/run/frecon/vt0"};

}  // namespace

ScreenController::ScreenController(
    std::shared_ptr<DrawInterface> draw_utils,
    std::shared_ptr<UpdateEngineProxy> update_engine_proxy,
    std::shared_ptr<NetworkManagerInterface> network_manager,
    std::shared_ptr<LogStoreManagerInterface> log_store_manager,
    std::shared_ptr<ProcessManagerInterface> process_manager)
    : draw_utils_(draw_utils),
      update_engine_proxy_(update_engine_proxy),
      network_manager_(network_manager),
      process_manager_(process_manager),
      key_reader_(
          KeyReader{/*include_usb=*/true, GetKeyboardLayout(process_manager_)}),
      log_store_manager_(log_store_manager) {}

bool ScreenController::Init() {
  if (!draw_utils_ || !draw_utils_->Init()) {
    LOG(ERROR) << AlertLogTag(kCategoryInit)
               << "Screen drawing utility not available. Cannot continue.";
    return false;
  }
  update_engine_proxy_->Init();

  std::vector<int> wait_keys = {KEY_UP, KEY_DOWN, KEY_ENTER, KEY_ESC};
  if (draw_utils_->IsDetachable()) {
    wait_keys = {KEY_VOLUMEDOWN, KEY_VOLUMEUP, KEY_POWER, KEY_ESC};
  }
  if (!key_reader_.Init(wait_keys)) {
    LOG(ERROR) << "Could not initialize key reader. Unable to continue.";
    return false;
  }

  key_reader_.SetDelegate(this);

  current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
  current_screen_->Show();

  frecon_screen_watcher_ = std::make_unique<base::FilePathWatcher>();
  frecon_screen_watcher_->Watch(
      kCurrentDisplayPath, base::FilePathWatcher::Type::kNonRecursive,
      base::BindRepeating(&::minios::ScreenController::OnDisplayChange,
                          base::Unretained(this)));

  if (log_store_manager_ &&
      !log_store_manager_->Init(std::make_unique<DiskUtil>(),
                                std::make_unique<crossystem::Crossystem>(),
                                std::make_unique<libstorage::Platform>())) {
    LOG(WARNING) << "Failed to initialize log store manager.";
    log_store_manager_ = nullptr;
  }

  return true;
}

std::unique_ptr<ScreenInterface> ScreenController::CreateScreen(
    ScreenType screen_type) {
  switch (screen_type) {
    case ScreenType::kWelcomeScreen:
      return std::make_unique<ScreenWelcome>(draw_utils_, this);
    case ScreenType::kNetworkDropDownScreen: {
      auto screen = std::make_unique<ScreenNetwork>(
          draw_utils_, network_manager_, &key_reader_, this);
      if (!seeded_ssid_.empty()) {
        screen->SeedCredentials(seeded_ssid_, seeded_passphrase_);
      }
      return screen;
    }
    case ScreenType::kLanguageDropDownScreen:
      return std::make_unique<ScreenLanguageDropdown>(draw_utils_, this);
    case ScreenType::kUserPermissionScreen:
      return std::make_unique<ScreenPermission>(draw_utils_, this);
    case ScreenType::kStartDownload:
      return std::make_unique<ScreenDownload>(
          std::make_unique<RecoveryInstaller>(process_manager_),
          update_engine_proxy_, draw_utils_,
          std::make_unique<MetricsReporter>(), log_store_manager_,
          process_manager_, this);
    case ScreenType::kDownloadError:
    case ScreenType::kNetworkError:
    case ScreenType::kPasswordError:
    case ScreenType::kConnectionError:
    case ScreenType::kGeneralError:
      return std::make_unique<ScreenError>(screen_type, draw_utils_, this);
    case ScreenType::kDebugOptionsScreen:
      return std::make_unique<ScreenDebugOptions>(draw_utils_,
                                                  log_store_manager_, this);
    case ScreenType::kLogScreen:
      return std::make_unique<ScreenLog>(draw_utils_, this);
    default:
      LOG(FATAL) << "Invalid screen.";
  }
}

void ScreenController::OnForward(ScreenInterface* screen) {
  switch (screen->GetType()) {
    case ScreenType::kWelcomeScreen:
      current_screen_ = CreateScreen(ScreenType::kNetworkDropDownScreen);
      break;
    case ScreenType::kNetworkDropDownScreen:
      current_screen_ = CreateScreen(ScreenType::kUserPermissionScreen);
      break;
    case ScreenType::kUserPermissionScreen:
      current_screen_ = CreateScreen(ScreenType::kStartDownload);
      break;
    case ScreenType::kDownloadError:
    case ScreenType::kNetworkError:
    case ScreenType::kPasswordError:
    case ScreenType::kConnectionError:
    case ScreenType::kGeneralError:
      // Show debug options and log screen. Save error screen to return to from
      // `kDebugOptionsScreen`.
      previous_screen_ = std::move(current_screen_);
      current_screen_ = CreateScreen(ScreenType::kDebugOptionsScreen);
      break;
    case ScreenType::kDebugOptionsScreen:
      current_screen_ = CreateScreen(ScreenType::kLogScreen);
      break;
    default:
      LOG(FATAL) << "Invalid screen.";
  }
  current_screen_->Show();
}

void ScreenController::OnBackward(ScreenInterface* screen) {
  switch (screen->GetType()) {
    case ScreenType::kWelcomeScreen:
    case ScreenType::kNetworkDropDownScreen:
    case ScreenType::kUserPermissionScreen:
      previous_screen_ = nullptr;
      current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
      break;
    case ScreenType::kPasswordError:
    case ScreenType::kNetworkError:
    case ScreenType::kConnectionError:
      // Return to network dropdown screen.
      previous_screen_ = nullptr;
      current_screen_ = CreateScreen(ScreenType::kNetworkDropDownScreen);
      break;
    case ScreenType::kDownloadError:
    case ScreenType::kGeneralError:
      // Return to beginning of the flow.
      previous_screen_ = nullptr;
      current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
      break;
    case ScreenType::kDebugOptionsScreen:
      // Back to previous screen, reset index.
      if (previous_screen_) {
        current_screen_ = std::move(previous_screen_);
        current_screen_->Reset();
      } else {
        // No screen saved. Go back to beginning.
        current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
      }
      break;
    case ScreenType::kLogScreen:
      // Back to debug options screen.
      current_screen_ = CreateScreen(ScreenType::kDebugOptionsScreen);
      break;
    case ScreenType::kLanguageDropDownScreen:
      if (previous_screen_) {
        current_screen_ = std::move(previous_screen_);
      } else {
        current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
      }
      break;
    default:
      LOG(FATAL) << "Invalid screen.";
  }
  current_screen_->Show();
}

void ScreenController::GoToScreen(ScreenType type, bool save_previous) {
  const auto current_type = current_screen_->GetType();
  if (current_type == ScreenType::kStartDownload) {
    // Return without changing screen if we have started recovery.
    LOG(ERROR) << "Screen change requested during download.";
    return;
  }
  if (save_previous) {
    previous_screen_ = std::move(current_screen_);
  }
  current_screen_ = CreateScreen(type);
  current_screen_->Show();
}

void ScreenController::OnError(ScreenType error_screen) {
  current_screen_->Reset();
  switch (error_screen) {
    case ScreenType::kDownloadError:
    case ScreenType::kNetworkError:
    case ScreenType::kPasswordError:
    case ScreenType::kConnectionError:
    case ScreenType::kGeneralError:
      previous_screen_ = std::move(current_screen_);
      current_screen_ = CreateScreen(error_screen);
      break;
    default:
      LOG(WARNING)
          << "Not a valid error screen. Defaulting to general error case.";
      previous_screen_ = std::move(current_screen_);
      current_screen_ = CreateScreen(ScreenType::kGeneralError);
      break;
  }
  current_screen_->Show();
}

ScreenType ScreenController::GetCurrentScreen() {
  return current_screen_->GetType();
}

void ScreenController::SwitchLocale(ScreenInterface* screen) {
  previous_screen_ = std::move(current_screen_);
  current_screen_ = CreateScreen(ScreenType::kLanguageDropDownScreen);
  current_screen_->Show();
}

void ScreenController::UpdateLocale(ScreenInterface* screen,
                                    int selected_locale_index) {
  // Change locale and update constants.
  CHECK(draw_utils_) << "Screen drawing utility not available.";
  if (screen->GetType() != ScreenType::kLanguageDropDownScreen) {
    LOG(WARNING) << "Only the language dropdown screen can change the locale.";
    return;
  }
  draw_utils_->LocaleChange(selected_locale_index);
  current_screen_ = std::move(previous_screen_);
  current_screen_->Reset();
  current_screen_->Show();
}

void ScreenController::OnKeyPress(int key) {
  CHECK(current_screen_) << "Could not send key event to screen.";

  if (key < 0 || key >= kKeyMax) {
    LOG(ERROR) << "Key code out of range. Key=" << key;
    return;
  }

  current_screen_->OnKeyPress(key);
}

void ScreenController::GetState(State* state_out) {
  CHECK(current_screen_) << "Could not get State for current screen.";
  state_out->CopyFrom(current_screen_->GetState());
}

bool ScreenController::MoveBackward(brillo::ErrorPtr* error) {
  CHECK(current_screen_) << "Could not move to previous screen.";
  LOG(INFO) << "MoveBackward from screen: " << current_screen_->GetName();
  return current_screen_->MoveBackward(error);
}

bool ScreenController::MoveForward(brillo::ErrorPtr* error) {
  CHECK(current_screen_) << "Could not move to next screen.";
  LOG(INFO) << "MoveForward from screen: " << current_screen_->GetName();
  return current_screen_->MoveForward(error);
}

void ScreenController::PressKey(int key_changed) {
  CHECK(current_screen_) << "Could not send key event to screen.";
  // TODO(hbarnor): Does not support GetPassword screen. Need to look into
  // using `KeyReader` for this.
  current_screen_->OnKeyPress(key_changed);
}

bool ScreenController::Reset(brillo::ErrorPtr* error) {
  if (ResetScreen(error)) {
    seeded_ssid_.clear();
    seeded_passphrase_.clear();
    dbus_recovery_state_.reset();
    return true;
  }
  return false;
}

bool ScreenController::ResetScreen(brillo::ErrorPtr* error) {
  // Don't allow reset in the middle of recovering.
  if (current_screen_->GetType() == ScreenType::kStartDownload) {
    Error::AddTo(error, FROM_HERE, error::kCannotReset,
                 "Cannot reset whiles recovery in progress.");

    return false;
  }
  previous_screen_ = nullptr;
  current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
  current_screen_->Show();
  return true;
}

void ScreenController::SeedNetworkCredentials(const std::string& ssid,
                                              const std::string& passphrase) {
  seeded_ssid_ = ssid;
  seeded_passphrase_ = passphrase;
  if (current_screen_->GetType() == ScreenType::kNetworkDropDownScreen) {
    ScreenNetwork* screen = dynamic_cast<ScreenNetwork*>(current_screen_.get());
    if (screen) {
      screen->SeedCredentials(seeded_ssid_, seeded_passphrase_);
    }
  }
}

void ScreenController::StartRecovery(const std::string& ssid,
                                     const std::string& passphrase) {
  if (dbus_recovery_state_.has_value()) {
    LOG(ERROR) << "Recovery already in progress.;";
    return;
  }
  dbus_recovery_state_ = State::IDLE;
  brillo::ErrorPtr error;
  LOG(INFO) << "Starting Dbus triggered recovery flow.;";
  // Always start from welcome screen.
  bool result = ResetScreen(&error);
  if (result) {
    SeedNetworkCredentials(ssid, passphrase);
    // Move forward from welcome screen.
    brillo::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::BindOnce(base::IgnoreResult(&ScreenController::MoveForward),
                       base::Unretained(this), nullptr));
  } else {
    LOG(ERROR) << "StartRecovery failed. Reason: " << error->GetMessage();

    if (dbus_recovery_state_.value() != State::ERROR) {
      OnError(ScreenType::kGeneralError);
    }
  }
}

void ScreenController::SetStateReporter(
    StateReporterInterface* state_reporter) {
  state_reporter_ = state_reporter;
}

void ScreenController::OnStateChanged(State state) {
  if (state_reporter_) {
    // StateChanged observers cannot/should not modify state.
    state_reporter_->StateChanged(state);
  }
  HandleStateChanged(state.state());
}

void ScreenController::HandleStateChanged(State::States state_state) {
  if (!dbus_recovery_state_.has_value() ||
      (dbus_recovery_state_.value() == state_state)) {
    return;
  }

  LOG(INFO) << "Recovery flow transitioning state: "
            << State_States_Name(dbus_recovery_state_.value()) << " -> "
            << State_States_Name(state_state);
  dbus_recovery_state_ = state_state;
  switch (state_state) {
    case State::CONNECTED:
    case State::NETWORK_CREDENTIALS:
    case State::NETWORK_SELECTION: {
      brillo::MessageLoop::current()->PostTask(
          FROM_HERE,
          base::BindOnce(base::IgnoreResult(&ScreenController::MoveForward),
                         base::Unretained(this), nullptr));

      break;
    }
    case State::ERROR:
    case State::COMPLETED: {
      seeded_ssid_.clear();
      seeded_passphrase_.clear();
      dbus_recovery_state_.reset();
      break;
    }
    default:
      break;
  }
}

void ScreenController::OnDisplayChange(const base::FilePath& path, bool error) {
  // Disable key reader delegate when switching away from the UI display, enable
  // in all other cases.
  if (error) {
    LOG(ERROR) << "Enabling keyreader, error watching path=" << path;
    key_reader_.SetDelegate(this);
    return;
  }

  base::FilePath resolved_path;
  if (base::ReadSymbolicLink(path, &resolved_path) &&
      resolved_path != kUiDisplayPath) {
    LOG(INFO) << "Disabling key reader due to display change.";
    key_reader_.SetDelegate(nullptr);
    return;
  }
  LOG(INFO) << "Enabling key reader.";
  key_reader_.SetDelegate(this);
}

}  // namespace minios
