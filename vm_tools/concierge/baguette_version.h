// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2025-05-26-000134_ed6250fa435d03db2ec65c9b2704f886d18f1b02";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3ef26163e3196ecf1ad7c12ed5155b068e7ae576d89d0992b28b9220094b2308";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4ad91695069e3cc15c46e361bf89371f7e20c9b0679d8299ece5183462198bf6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
