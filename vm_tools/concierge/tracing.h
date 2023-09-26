// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_TRACING_H_
#define VM_TOOLS_CONCIERGE_TRACING_H_

#include "perfetto/perfetto.h"

namespace vm_tools::concierge {
constexpr char kCategory[] = "concierge";
}  // namespace vm_tools::concierge

PERFETTO_DEFINE_CATEGORIES_IN_NAMESPACE(
    vm_tools::concierge,
    perfetto::Category(vm_tools::concierge::kCategory)
        .SetDescription("Events from concierge"));

#define VMT_TRACE(...) TRACE_EVENT(__VA_ARGS__)
#define VMT_TRACE_BEGIN(...) TRACE_EVENT_BEGIN(__VA_ARGS__)
#define VMT_TRACE_END(category) TRACE_EVENT_END(category)

namespace vm_tools::concierge {
void InitTracing();
}

#endif  // VM_TOOLS_CONCIERGE_TRACING_H_
