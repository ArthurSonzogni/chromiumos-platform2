// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "service_constants.h"

GQuark chromeos_login_error_quark() {
  return g_quark_from_static_string("chromeos-login-error-quark");
}

namespace cryptohome {
const char *kCryptohomeInterface = "org.chromium.CryptohomeInterface";
const char *kCryptohomeServiceName = "org.chromium.Cryptohome";
const char *kCryptohomeServicePath = "/org/chromium/Cryptohome";
// Methods
const char *kCryptohomeCheckKey = "CheckKey";
const char *kCryptohomeIsMounted = "IsMounted";
const char *kCryptohomeMount = "Mount";
const char *kCryptohomeUnmount = "Unmount";
}  // namespace cryptohome

namespace login_manager {
const char *kSessionManagerInterface = "org.chromium.SessionManagerInterface";
const char *kSessionManagerServiceName = "org.chromium.SessionManager";
const char *kSessionManagerServicePath = "/org/chromium/SessionManager";
// Methods
const char *kSessionManagerEmitLoginPromptReady = "EmitLoginPromptReady";
const char *kSessionManagerStartSession = "StartSession";
const char *kSessionManagerStopSession = "StopSession";
// Signals
const char *kSessionManagerSessionStateChanged = "SessionStateChanged";
}  // namespace login_manager

namespace speech_synthesis {
const char *kSpeechSynthesizerInterface =
    "org.chromium.SpeechSynthesizerInterface";
const char *kSpeechSynthesizerServicePath = "/org/chromium/SpeechSynthesizer";
const char *kSpeechSynthesizerServiceName = "org.chromium.SpeechSynthesizer";
}  // namespace speech_synthesis
