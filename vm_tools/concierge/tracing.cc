// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/tracing.h"

#include <utility>

#include "base/logging.h"
#include "perfetto/perfetto.h"

PERFETTO_TRACK_EVENT_STATIC_STORAGE_IN_NAMESPACE(vm_tools::concierge);

namespace vm_tools::concierge {

namespace {

// We have to guess what the TraceConfig given to traced will eventually be.
// Empirically it is sufficient to just listen for all non-debug track events.
perfetto::TraceConfig GetConfig() {
  perfetto::TraceConfig trace_cfg;
  // This buffer should be large enough to hold whatever traces get generated
  // before perfetto is done connecting.
  trace_cfg.add_buffers()->set_size_kb(1024);
  perfetto::DataSourceConfig* source_cfg =
      trace_cfg.add_data_sources()->mutable_config();
  // Include events for non-debug tracks.
  source_cfg->set_name("track_event");
  perfetto::protos::gen::TrackEventConfig te_cfg;
  te_cfg.add_enabled_categories(kCategory);
  // For some reason track event configs must be added as raw string protos...
  source_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());
  return trace_cfg;
}

}  // namespace

void InitTracing() {
  perfetto::TracingInitArgs args;

  // Concierge is a CrOS daemon, we make use of the system traced instance which
  // runs on test images. This facilitates comparing concierge events to other
  // system-generated events.
  args.backends |= perfetto::kSystemBackend;

  perfetto::Tracing::Initialize(args);
  if (!::vm_tools::concierge::TrackEvent::Register()) {
    LOG(ERROR) << "Failed to set up tracing";
    return;
  }

  // Tracing with a system daemon doesn't *actually* begin until an asynchronous
  // handshake between the provider and traced occurs. The intended workaround
  // for this is to create a "startup trace" client which we emit to initially.
  perfetto::Tracing::SetupStartupTracingOpts startup_tracing_opts{
      .backend = perfetto::kSystemBackend,
      .on_setup =
          [](perfetto::Tracing::OnStartupTracingSetupCallbackArgs args) {
            LOG(INFO) << "Finished setting up traces with "
                      << args.num_data_sources_started << " sources.";
          },
      .on_aborted = []() { LOG(WARNING) << "Startup traces aborted"; },
      .on_adopted = []() { LOG(INFO) << "Startup traces adopted"; },
  };
  // TODO(b/296025701): We need to block because we want to trace startup, which
  // is also blocking. If startup goes to async we can do this async too (as
  // long as we do it first).
  perfetto::Tracing::SetupStartupTracingBlocking(
      GetConfig(), std::move(startup_tracing_opts));

  LOG_IF(WARNING, !TRACE_EVENT_CATEGORY_ENABLED(kCategory))
      << "Setting up startup traces failed for " << kCategory
      << ", initialization trace data will be lost";
}

}  // namespace vm_tools::concierge
