// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREEN_CONTROLLER_H_
#define MINIOS_SCREEN_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path_watcher.h>
#include <brillo/errors/error.h>
#include <minios/proto_bindings/minios.pb.h>

#include "minios/draw_utils.h"
#include "minios/key_reader.h"
#include "minios/network_manager_interface.h"
#include "minios/process_manager.h"
#include "minios/screen_controller_interface.h"
#include "minios/screen_interface.h"
#include "minios/state_reporter_interface.h"
#include "minios/update_engine_proxy.h"

namespace minios {

// Minimal set of keys supported by MiniOS. Keys 200 and above are dropped.
constexpr int kKeyMax = 200;

class ScreenController : public ScreenControllerInterface,
                         public KeyReader::Delegate {
 public:
  ScreenController(std::shared_ptr<DrawInterface> draw_utils,
                   std::shared_ptr<UpdateEngineProxy> update_engine_proxy,
                   std::shared_ptr<NetworkManagerInterface> network_manager,
                   ProcessManagerInterface* process_manager);

  virtual ~ScreenController() = default;

  ScreenController(const ScreenController&) = delete;
  ScreenController& operator=(const ScreenController&) = delete;

  bool Init();

  // APIs used to expose MiniOs control over DBus.
  // Get the current state of MiniOs.
  void GetState(State* state_out);

  // Advance the current screen to the previous screen if possible.
  bool MoveBackward(brillo::ErrorPtr* error);

  // Advance the current screen to the next screen if possible.
  bool MoveForward(brillo::ErrorPtr* error);

  // Insert a key press into the MiniOs keyboard event loop.
  void PressKey(int key_changed);

  // Reset MiniOs to its initial screen and state.
  bool Reset(brillo::ErrorPtr* error);

  // Set credentials for use in advancing/controlling the network screens.
  void SeedNetworkCredentials(const std::string& ssid,
                              const std::string& passphrase);

  // Perform the network based recovery.
  // Does this by resetting to the initial screen and state. It then steps
  // steps through the network selection screen and starts the recovery.
  void StartRecovery(const std::string& ssid, const std::string& passphrase);

  void SetStateReporter(StateReporterInterface* state_reporter);

  // ScreenControllerInterface overrides.
  // Called by screens when the user presses the next or continue button.
  void OnForward(ScreenInterface* screen) override;

  // Called by screens when the user presses the back or cancel button.
  void OnBackward(ScreenInterface* screen) override;

  // Changes the screen to the specified screen.
  void GoToScreen(ScreenType type, bool save_previous = false) override;

  // Called by screens to show an error screen.
  void OnError(ScreenType error_screen) override;

  // Returns the `current_screen_` type.
  ScreenType GetCurrentScreen() override;

  // Changes to the `kLanguageDropDownScreen` class.
  void SwitchLocale(ScreenInterface* screen) override;

  // Returns to the original screen and updates UI elements based on locale
  // change.
  void UpdateLocale(ScreenInterface* screen,
                    int selected_locale_index) override;

  // Report state changes via `state_reporter_`.
  void OnStateChanged(State state) override;

  void SetCurrentScreenForTest(ScreenType current_screen) {
    current_screen_ = CreateScreen(current_screen);
  }

  void SetCurrentScreenForTest(
      std::unique_ptr<ScreenInterface> current_screen) {
    current_screen_ = std::move(current_screen);
  }

 private:
  FRIEND_TEST(GoToScreenTest, ChangeScreensSavePrevious);

  // Creates each class ptr as needed.
  std::unique_ptr<ScreenInterface> CreateScreen(ScreenType screen_type);

  // Take next action if Dbus recovery flow is in progress.
  void HandleStateChanged(State::States state_state);

  // This function overloads `KeyReader::Delegate`.
  void OnKeyPress(int key) override;

  // Reset MiniOs to its initial screen.
  bool ResetScreen(brillo::ErrorPtr* error);

  // Update keyreader on display changing from UI (to terminal in dev mode).
  void OnDisplayChange(const base::FilePath& path, bool error);

  std::shared_ptr<DrawInterface> draw_utils_;

  std::shared_ptr<UpdateEngineProxy> update_engine_proxy_;

  std::shared_ptr<NetworkManagerInterface> network_manager_;

  std::optional<State::States> dbus_recovery_state_;
  // Pre-seeded network credentials.
  std::string seeded_ssid_;
  std::string seeded_passphrase_;

  StateReporterInterface* state_reporter_ = nullptr;

  ProcessManagerInterface* process_manager_;

  KeyReader key_reader_;

  // Currently displayed screen. This class receives all the key events.
  std::unique_ptr<ScreenInterface> current_screen_;
  // Previous screen only used when changing the language so you know what
  // screen to return to after selection.
  std::unique_ptr<ScreenInterface> previous_screen_;

  std::unique_ptr<base::FilePathWatcher> frecon_screen_watcher_;
};

}  // namespace minios

#endif  // MINIOS_SCREEN_CONTROLLER_H_
