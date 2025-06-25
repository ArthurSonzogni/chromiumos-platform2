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
constexpr char kBaguetteVersion[] = "2025-06-25-000107_889c8f4af868ed8fc55b19d229dad1f9359fd25c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "74bb93675ef121430dc92802ca7c10a9e9e398d688f1111bbd5cc8c4227b8d9b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "cd5c22736504e7836873ec148032961cad4227df669af0fe8483d886857fd9cc";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
