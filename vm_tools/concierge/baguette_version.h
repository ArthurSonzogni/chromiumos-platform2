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
constexpr char kBaguetteVersion[] = "2025-09-30-000101_6ae103f28616dc3ec4bdc2ebdcc3559bdcf77d05";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c2a4f578fc9ba5cf608f49f4f12206673b13168a020354f2f4c35b6a60cdc378";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c7ff370ec7c286ad13e35ef933275bbd2b3fb733dcb38dcde9dcce5927a93b7e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
