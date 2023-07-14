// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frontend/gtk/cros_gtk_im_context.h"
#include "util/logging.h"

// This file defines the functions required to wire up a GTK4 IM module.
namespace cros_im {
namespace gtk {

namespace {
constexpr gulong kInvalidSignalHandlerId = 0;
gulong signal_handler_id;

// Disconnect the signal handler if there is one in use.
void MaybeDisconnectSignalHandler() {
  if (signal_handler_id != kInvalidSignalHandlerId)
    g_signal_handler_disconnect(gdk_display_manager_get(), signal_handler_id);

  signal_handler_id = kInvalidSignalHandlerId;
}

// The following function is used as a callback to complete
// g_io_im_cros_gtk4_load().
void on_display_notify_signal(GdkDisplayManager* self,
                              GParamSpec* pspec,
                              gpointer module) {
  if (!CrosGtkIMContext::InitializeWaylandManager()) {
    LOG(ERROR) << "Failed to initialize Wayland manager for GTK4 IM module.";
    return;
  }

  MaybeDisconnectSignalHandler();

  CrosGtkIMContext::RegisterType(G_TYPE_MODULE(module));
  g_io_extension_point_implement(GTK_IM_MODULE_EXTENSION_POINT_NAME,
                                 CrosGtkIMContext::GetType(), "cros",
                                 /* priority= */ 0);
}

}  // namespace

extern "C" {

void g_io_im_cros_gtk4_load(GIOModule* module) {
  g_type_module_use(G_TYPE_MODULE(module));

  // Unlike GTK3, GTK4 doesn't provide an initialization hook where the
  // GDKDisplay is available. So wait for the display to be initialized before
  // finishing the module load.
  signal_handler_id =
      g_signal_connect(gdk_display_manager_get(), "notify::default-display",
                       G_CALLBACK(on_display_notify_signal), module);
}

void g_io_im_cros_gtk4_unload(GIOModule* module) {
  g_type_module_unuse(G_TYPE_MODULE(module));
  MaybeDisconnectSignalHandler();
}

}  // extern "C"

}  // namespace gtk
}  // namespace cros_im
