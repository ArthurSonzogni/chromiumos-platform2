// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_TRACING_H_
#define VM_TOOLS_CONCIERGE_TRACING_H_

#if USE_TRACING

#include "perfetto/perfetto.h"

namespace vm_tools::concierge {
constexpr char kCategory[] = "concierge";
}  // namespace vm_tools::concierge

PERFETTO_DEFINE_CATEGORIES(perfetto::Category(vm_tools::concierge::kCategory)
                               .SetDescription("Events from concierge"));

#define VMT_TRACE(...) TRACE_EVENT(__VA_ARGS__)
#define VMT_TRACE_BEGIN(...) TRACE_EVENT_BEGIN(__VA_ARGS__)
#define VMT_TRACE_END(category) TRACE_EVENT_END(category)

namespace vm_tools::concierge {
void InitTracing();
}

#else  // (i.e. !USE_TRACING)

// If tracing is off, all of the macros/implementations are no-op.

#define VMT_TRACE(category, name, ...)
#define VMT_TRACE_BEGIN(category, name, ...)
#define VMT_TRACE_END(category)

namespace vm_tools::concierge {
inline void InitTracing() {}
}  // namespace vm_tools::concierge

#endif  // USE_TRACING

#endif  // VM_TOOLS_CONCIERGE_TRACING_H_
