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
constexpr char kBaguetteVersion[] = "2026-10-09-000207_3d27b8a79e3bdfa7bb76fd148e0ab53c58c62b0e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9194728764a938bb8f265ce8a8aa9a35c1ea930a3e09064b51127b96d41956f8";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "2b47103300ed614895ad1660c366334f308348fc3b1a281b411b184d1174c686";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
