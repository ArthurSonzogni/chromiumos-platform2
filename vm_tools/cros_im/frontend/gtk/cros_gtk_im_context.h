// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_FRONTEND_GTK_CROS_GTK_IM_CONTEXT_H_
#define VM_TOOLS_CROS_IM_FRONTEND_GTK_CROS_GTK_IM_CONTEXT_H_

#include <gtk/gtk.h>
#include <gtk/gtkimmodule.h>
#include <memory>
#include <optional>
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
  static bool InitializeWaylandManager();
  static void RegisterType(GTypeModule* module);

  static CrosGtkIMContext* Create();
  static GType GetType();

  CrosGtkIMContext();
  ~CrosGtkIMContext();

  // GtkIMContext implementation:
#ifdef GTK4
  void SetClientWidget(GtkWidget* widget);
  gboolean FilterKeypress(GdkEvent* event);
#else
  void SetClientWindow(GdkWindow* window);
  gboolean FilterKeypress(GdkEventKey* key);
#endif
  void GetPreeditString(char** str, PangoAttrList** attrs, int* cursor_pos);
  void FocusIn();
  void FocusOut();
  void Reset();
  void SetCursorLocation(GdkRectangle* area);
  void SetSurrounding(const char* text, int len, int cursor_index);
  void SetUsePreedit(gboolean use_preedit);

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
    void SetPreeditRegion(int start_offset,
                          int length,
                          const std::vector<PreeditStyle>& styles) override;
    void Commit(const std::string& commit) override;

    void DeleteSurroundingText(int start_offset, int length) override;

    void KeySym(uint32_t keysym, KeyState state, uint32_t modifiers) override;

   private:
    // Returns the deleted text on success, an empty string on failure.
    std::optional<std::string> DeleteSurroundingTextImpl(int byte_offset,
                                                         int byte_length);

    CrosGtkIMContext* context_;
  };

  void Activate();

  // Retrieves the current surrounding text. On success, returns true and
  // populates surrounding_ and surrounding_cursor_pos_.
  bool RetrieveSurrounding();
  // Retrieves and then sends the surrounding text to the backend. The text may
  // be trimmed if it is too long.
  void UpdateSurrounding();

  bool is_x11_;

  // Ref counted
#ifdef GTK4
  GtkWidget* client_widget_ = nullptr;
  GdkSurface* root_surface_ = nullptr;
#else
  GdkWindow* gdk_window_ = nullptr;
  GdkWindow* top_level_gdk_window_ = nullptr;
#endif
  // Set if FocusIn() is called prior to SetClientWindow()/SetClientWidget().
  bool pending_activation_ = false;

  bool supports_preedit_ = true;

  // Updated by calling RetrieveSurrounding()
  std::string surrounding_;
  int surrounding_cursor_pos_ = 0;

  std::string preedit_;
  int32_t preedit_cursor_pos_ = 0;
  std::vector<PreeditStyle> preedit_styles_;

  BackendObserver backend_observer_;
  std::unique_ptr<cros_im::IMContextBackend> backend_;
};

}  // namespace gtk
}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_FRONTEND_GTK_CROS_GTK_IM_CONTEXT_H_
