// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_CTX_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_CTX_H_

const char* sl_context_atom_name(int atom_enum);
void sl_context_init_default(struct sl_context* ctx);

bool sl_context_init_virtwl(struct sl_context* ctx,
                            struct wl_event_loop* event_loop,
                            bool display);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_CTX_H_
