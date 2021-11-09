// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_FRONTEND_GTK_CROS_GTK_IM_CONTEXT_H_
#define VM_TOOLS_CROS_IM_FRONTEND_GTK_CROS_GTK_IM_CONTEXT_H_

#include <gtk/gtk.h>
#include <gtk/gtkimmodule.h>
#include <memory>
#include <string>
#include <vector>

#include "backend/im_context_backend.h"

namespace cros_im {

class IMContextBackend;

namespace gtk {

// CrosGtkIMContext implements the GtkIMContext GObject interface via
// IMContextBackend, which supports the Wayland text-input-v1 protocol. Instead
// of handling key events via FilterKeypress, the compositor will directly send
// those to our backend.
class CrosGtkIMContext : public GtkIMContext {
 public:
  // Must be called prior to creating objects.
  static void RegisterType(GTypeModule* module);

  static CrosGtkIMContext* Create();

  CrosGtkIMContext();
  ~CrosGtkIMContext();

  // GtkIMContext implementation:
  void SetClientWindow(GdkWindow* window);
  void GetPreeditString(char** str, PangoAttrList** attrs, int* cursor_pos);
  gboolean FilterKeypress(GdkEventKey* Key);
  void FocusIn();
  void FocusOut();
  void Reset();
  void SetCursorLocation(GdkRectangle* area);
  void SetSurrounding(const char* text, int len, int cursor_index);

 private:
  // CrosGtkIMContext can't implement this directly as it is a GObject and
  // virtual methods break initialization -- GtkIMContext's members must be at
  // the start of the object layout and virtual methods cause this to shift this
  // by a vtable pointer.
  class BackendObserver : public IMContextBackend::Observer {
   public:
    explicit BackendObserver(CrosGtkIMContext* context);
    virtual ~BackendObserver() {}

    void SetPreedit(const std::string& preedit,
                    int cursor,
                    const std::vector<PreeditStyle>& styles) override;
    void Commit(const std::string& commit) override;

    void KeySym(uint32_t keysym, KeyState state) override;

   private:
    CrosGtkIMContext* context_;
  };

  // Ref counted
  GdkWindow* window_ = nullptr;
  GtkWindow* gtk_window_ = nullptr;

  std::string preedit_;
  int32_t preedit_cursor_pos_ = 0;
  std::vector<PreeditStyle> preedit_styles_;

  BackendObserver backend_observer_;
  std::unique_ptr<cros_im::IMContextBackend> backend_;
};

}  // namespace gtk
}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_FRONTEND_GTK_CROS_GTK_IM_CONTEXT_H_
