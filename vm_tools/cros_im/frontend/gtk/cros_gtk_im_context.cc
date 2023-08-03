// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frontend/gtk/cros_gtk_im_context.h"

#include <gdk/gdk.h>

#ifdef GTK4
#include <gdk/wayland/gdkwayland.h>
#include <gdk/x11/gdkx.h>
#include <gtk/gtkwidget.h>
#else
#include <gdk/gdkwayland.h>
#include <gdk/gdkx.h>
#endif

// Remove definitions from X11 headers that collide with our code.
#undef FocusIn
#undef FocusOut
#include <cstring>
#include <iostream>
#include <utility>

#include "backend/wayland_manager.h"
#include "frontend/gtk/x11.h"
#include "util/logging.h"

namespace cros_im {
namespace gtk {

namespace {

////////////////////////////////////////////////////////////////////////////////
// GObject integration

struct CrosGtkIMContextClass {
  GtkIMContextClass parent_class;
};

G_DEFINE_DYNAMIC_TYPE(CrosGtkIMContext,
                      cros_gtk_im_context,
                      GTK_TYPE_IM_CONTEXT)

template <typename T>
CrosGtkIMContext* ToCrosGtkIMContext(T* obj) {
  return G_TYPE_CHECK_INSTANCE_CAST((obj), cros_gtk_im_context_get_type(),
                                    CrosGtkIMContext);
}

void cros_gtk_im_context_init(CrosGtkIMContext* context) {
  new (context) CrosGtkIMContext();
}

void DisposeCrosGtkIMContext(GObject* gobject) {
  ToCrosGtkIMContext(gobject)->~CrosGtkIMContext();
  G_OBJECT_CLASS(cros_gtk_im_context_parent_class)->dispose(gobject);
}

template <auto F, typename... Args>
auto Fwd(GtkIMContext* context, Args... args) {
  return (ToCrosGtkIMContext(context)->*F)(args...);
}

void cros_gtk_im_context_class_init(CrosGtkIMContextClass* klass) {
  GtkIMContextClass* im_context = GTK_IM_CONTEXT_CLASS(klass);
  GObjectClass* gobject = G_OBJECT_CLASS(klass);

  gobject->dispose = DisposeCrosGtkIMContext;
#ifdef GTK4
  im_context->set_client_widget = Fwd<&CrosGtkIMContext::SetClientWidget>;
#else
  im_context->set_client_window = Fwd<&CrosGtkIMContext::SetClientWindow>;
#endif
  im_context->get_preedit_string = Fwd<&CrosGtkIMContext::GetPreeditString>;
  im_context->filter_keypress = Fwd<&CrosGtkIMContext::FilterKeypress>;
  im_context->focus_in = Fwd<&CrosGtkIMContext::FocusIn>;
  im_context->focus_out = Fwd<&CrosGtkIMContext::FocusOut>;
  im_context->reset = Fwd<&CrosGtkIMContext::Reset>;
  im_context->set_cursor_location = Fwd<&CrosGtkIMContext::SetCursorLocation>;
  im_context->set_surrounding = Fwd<&CrosGtkIMContext::SetSurrounding>;
  im_context->set_use_preedit = Fwd<&CrosGtkIMContext::SetUsePreedit>;
}

void cros_gtk_im_context_class_finalize(CrosGtkIMContextClass* klass) {}

// end of GObject integration
////////////////////////////////////////////////////////////////////////////////

IMContextBackend::ContentType ConvertContentType(GtkInputHints gtk_hints,
                                                 GtkInputPurpose gtk_purpose,
                                                 bool supports_preedit) {
  zcr_extended_text_input_v1_input_type input_type =
      ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_TYPE_TEXT;
  zcr_extended_text_input_v1_input_mode input_mode =
      ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_MODE_DEFAULT;
  uint32_t input_flags = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_FLAGS_NONE;
  zcr_extended_text_input_v1_learning_mode learning_mode =
      ZCR_EXTENDED_TEXT_INPUT_V1_LEARNING_MODE_ENABLED;
  // TODO(b/232048153): Listen to set_use_preedit and pass it through here.
  zcr_extended_text_input_v1_inline_composition_support
      inline_composition_support =
          ZCR_EXTENDED_TEXT_INPUT_V1_INLINE_COMPOSITION_SUPPORT_SUPPORTED;

  switch (gtk_purpose) {
    case GTK_INPUT_PURPOSE_FREE_FORM:
    case GTK_INPUT_PURPOSE_ALPHA:
    case GTK_INPUT_PURPOSE_NAME:
    case GTK_INPUT_PURPOSE_TERMINAL:
      // default case
      break;
    case GTK_INPUT_PURPOSE_PIN:
      learning_mode = ZCR_EXTENDED_TEXT_INPUT_V1_LEARNING_MODE_DISABLED;
      [[fallthrough]];
    case GTK_INPUT_PURPOSE_DIGITS:
    case GTK_INPUT_PURPOSE_NUMBER:
      input_type = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_TYPE_NUMBER;
      input_mode = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_MODE_NUMERIC;
      break;
    case GTK_INPUT_PURPOSE_PHONE:
      input_type = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_TYPE_TELEPHONE;
      input_mode = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_MODE_TEL;
      break;
    case GTK_INPUT_PURPOSE_URL:
      input_type = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_TYPE_URL;
      input_mode = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_MODE_URL;
      break;
    case GTK_INPUT_PURPOSE_EMAIL:
      input_type = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_TYPE_EMAIL;
      input_mode = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_MODE_EMAIL;
      break;
    case GTK_INPUT_PURPOSE_PASSWORD:
      input_type = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_TYPE_PASSWORD;
      learning_mode = ZCR_EXTENDED_TEXT_INPUT_V1_LEARNING_MODE_DISABLED;
      break;
    default:
      LOG(WARNING) << "Unknown GtkInputPurpose: " << gtk_purpose;
      break;
  }

  if (gtk_hints & GTK_INPUT_HINT_SPELLCHECK) {
    input_flags |= ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_FLAGS_SPELLCHECK_ON;
  } else if (gtk_hints & GTK_INPUT_HINT_NO_SPELLCHECK) {
    input_flags |= ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_FLAGS_SPELLCHECK_OFF;
  }

  if (gtk_hints & GTK_INPUT_HINT_WORD_COMPLETION) {
    input_flags |= ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_FLAGS_AUTOCOMPLETE_ON;
  }

  if (gtk_hints & GTK_INPUT_HINT_LOWERCASE) {
    input_flags |= ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_FLAGS_AUTOCAPITALIZE_NONE;
  } else if (gtk_hints & GTK_INPUT_HINT_UPPERCASE_CHARS) {
    input_flags |=
        ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_FLAGS_AUTOCAPITALIZE_CHARACTERS;
  } else if (gtk_hints & GTK_INPUT_HINT_UPPERCASE_WORDS) {
    input_flags |= ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_FLAGS_AUTOCAPITALIZE_WORDS;
  } else if (gtk_hints & GTK_INPUT_HINT_UPPERCASE_SENTENCES) {
    input_flags |=
        ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_FLAGS_AUTOCAPITALIZE_SENTENCES;
  }

  if (gtk_hints & GTK_INPUT_HINT_INHIBIT_OSK) {
    input_mode = ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_MODE_NONE;
  }

  // GTK_INPUT_HINT_EMOJI and GTK_INPUT_HINT_NO_EMOJI are currently ignored.

  if (!supports_preedit) {
    inline_composition_support =
        ZCR_EXTENDED_TEXT_INPUT_V1_INLINE_COMPOSITION_SUPPORT_UNSUPPORTED;
  }

  return {.input_type = input_type,
          .input_mode = input_mode,
          .input_flags = input_flags,
          .learning_mode = learning_mode,
          .inline_composition_support = inline_composition_support};
}

PangoAttribute* ToPangoAttribute(const PreeditStyle& style) {
  PangoAttribute* attr = nullptr;
  // TODO(timloh): Work out how to best style pre-edit text. This code tries to
  // match Chrome, but some applications fail to distinguish the different types
  // of underline. Adjusting fg/bg colours may be more robust.
  switch (style.style) {
    // Chrome does not currently send DEFAULT, NONE, ACTIVE, INACTIVE.
    default:
    case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_HIGHLIGHT:
    case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_SELECTION:
      attr = pango_attr_underline_new(PANGO_UNDERLINE_DOUBLE);
      break;
    case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_UNDERLINE:
      attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
      break;
    case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_INCORRECT:
      attr = pango_attr_underline_new(PANGO_UNDERLINE_ERROR);
      break;
  }
  attr->start_index = style.index;
  attr->end_index = style.index + style.length;
  return attr;
}

}  // namespace

bool CrosGtkIMContext::InitializeWaylandManager() {
  GdkDisplay* gdk_display = gdk_display_get_default();
  if (!gdk_display) {
    LOG(WARNING) << "GdkDisplay wasn't found";
    return false;
  }
  if (GDK_IS_X11_DISPLAY(gdk_display)) {
    if (!SetUpWaylandForX11())
      return false;
  } else if (GDK_IS_WAYLAND_DISPLAY(gdk_display)) {
    WaylandManager::CreateInstance(
        gdk_wayland_display_get_wl_display(gdk_display));
  } else {
    LOG(WARNING) << "Unknown GdkDisplay type";
    return false;
  }
  return true;
}

void CrosGtkIMContext::RegisterType(GTypeModule* module) {
  cros_gtk_im_context_register_type(module);
}

CrosGtkIMContext* CrosGtkIMContext::Create() {
  return ToCrosGtkIMContext(
      g_object_new(cros_gtk_im_context_get_type(), nullptr));
}

GType CrosGtkIMContext::GetType() {
  return cros_gtk_im_context_get_type();
}

CrosGtkIMContext::CrosGtkIMContext()
    : backend_observer_(this),
      backend_(std::make_unique<IMContextBackend>(&backend_observer_)) {
  is_x11_ = GDK_IS_X11_DISPLAY(gdk_display_get_default());
}

CrosGtkIMContext::~CrosGtkIMContext() = default;

#ifdef GTK4
void CrosGtkIMContext::SetClientWidget(GtkWidget* widget) {
  if (widget) {
    g_set_object(&client_widget_, widget);

    GdkSurface* surface =
        gtk_native_get_surface(GTK_NATIVE(gtk_widget_get_root(widget)));
    g_set_object(&root_surface_, surface);
    if (!root_surface_)
      LOG(WARNING) << "Root GdkSurface was null";
    if (pending_activation_)
      Activate();
  } else {
    g_set_object(&client_widget_, nullptr);
    g_set_object(&root_surface_, nullptr);
  }
}
#else
void CrosGtkIMContext::SetClientWindow(GdkWindow* window) {
  if (window) {
    GdkWindow* toplevel = gdk_window_get_effective_toplevel(window);
    g_set_object(&gdk_window_, window);
    g_set_object(&top_level_gdk_window_, toplevel);
    if (!top_level_gdk_window_)
      LOG(WARNING) << "Top-level GdkWindow was null";
    if (pending_activation_)
      Activate();
  } else {
    g_set_object(&gdk_window_, nullptr);
    g_set_object(&top_level_gdk_window_, nullptr);
  }
}
#endif

void CrosGtkIMContext::GetPreeditString(char** preedit,
                                        PangoAttrList** styles,
                                        int* cursor_pos) {
  if (preedit)
    *preedit = g_strdup(preedit_.c_str());
  if (cursor_pos)
    *cursor_pos = g_utf8_strlen(preedit_.c_str(), preedit_cursor_pos_);
  if (styles) {
    *styles = pango_attr_list_new();
    for (const auto& style : preedit_styles_)
      pango_attr_list_insert(*styles, ToPangoAttribute(style));
  }
}

#ifdef GTK4
gboolean CrosGtkIMContext::FilterKeypress(GdkEvent* event) {
#else
gboolean CrosGtkIMContext::FilterKeypress(GdkEventKey* event) {
#endif
  // The original purpose of this interface was to provide IMEs a chance to
  // consume key events and emit signals like preedit-changed or commit in
  // response.  In our implementation (the Wayland model), when a text field
  // has focus the compositor will not send regular keyboard events at all and
  // rather directly send us processed events like text_input_v1::commit_string

  // For key events that are not consumed by the IME, we receive
  // text_input_v1::keysym and generate a fake key event in response, which
  // triggers this function. Keys like backspace, enter and tab (control
  // characters) will be handled specifically by GTK widgets, while non-control
  // characters (e.g. 'a') need to be converted here into commit signals.

  // TODO(b/232048508): Chrome sometimes sends wl_keyboard::key instead, which
  // could lead to race conditions under X11.
#ifdef GTK4
  if (gdk_event_get_event_type(event) != GDK_KEY_PRESS) {
#else
  if (event->type != GDK_KEY_PRESS) {
#endif
    return false;
  }

  // Don't consume events with modifiers like <Ctrl>.
  uint32_t unicode_char;
  guint no_text_input_mask;
  guint event_keyval;
  guint modifier_state;
#ifdef GTK4
  no_text_input_mask = GDK_CONTROL_MASK | GDK_ALT_MASK;
  modifier_state = gdk_event_get_modifier_state(event);
  event_keyval = gdk_key_event_get_keyval(event);
#else
  GdkDisplay* gdk_display = gdk_window_get_display(gdk_window_);
  no_text_input_mask =
      gdk_keymap_get_modifier_mask(gdk_keymap_get_for_display(gdk_display),
                                   GDK_MODIFIER_INTENT_NO_TEXT_INPUT);
  modifier_state = event->state;
  event_keyval = event->keyval;
#endif

  if (modifier_state & no_text_input_mask) {
    return false;
  }

  unicode_char = gdk_keyval_to_unicode(event_keyval);
  if (!unicode_char || g_unichar_iscntrl(unicode_char)) {
    return false;
  }

  // g_unichar_to_utf8() supposedly requires 6 bytes of space, despite UTF-8
  // only needing 4 bytes.
  char utf8[6];
  size_t len = g_unichar_to_utf8(unicode_char, utf8);
  backend_observer_.Commit({utf8, len});
  return true;
}

void CrosGtkIMContext::FocusIn() {
#ifdef GTK4
  if (root_surface_) {
#else
  if (top_level_gdk_window_) {
#endif
    Activate();
  } else {
    // TODO(timloh): Add an automated test for this case. This code path can be
    // manually tested by opening gedit, clicking "Save", then clicking the find
    // (magnifying glass) icon.
    pending_activation_ = true;
  }
}

void CrosGtkIMContext::FocusOut() {
  if (pending_activation_) {
    pending_activation_ = false;
  } else if (backend_->IsActive()) {
    backend_->Deactivate();
  }
}

void CrosGtkIMContext::Reset() {
  backend_->Reset();
}

void CrosGtkIMContext::SetCursorLocation(GdkRectangle* area) {
  int x = 0, y = 0;
#ifdef GTK4
  // TODO(b/291845382): In GTK4, when the window is not maximized the position
  // of the candidates box is incorrect.
  if (!client_widget_)
    return;
  auto* native = gtk_widget_get_native(client_widget_);
  double top_level_x, top_level_y;
  // Get coordinates against the current window.
  gtk_widget_translate_coordinates(client_widget_, GTK_WIDGET(native), area->x,
                                   area->y, &top_level_x, &top_level_y);
  x = top_level_x;
  y = top_level_y;
#else
  if (!gdk_window_)
    return;

  int offset_x = 0, offset_y = 0;
  gdk_window_get_origin(gdk_window_, &offset_x, &offset_y);

  // When running directly under Wayland, these are usually (always?) zero,
  // but typically non-zero when running under X11.
  int top_level_x = 0, top_level_y = 0;
  gdk_window_get_origin(top_level_gdk_window_, &top_level_x, &top_level_y);

  x = offset_x - top_level_x + area->x;
  y = offset_y - top_level_y + area->y;
#endif
  backend_->SetCursorLocation(x, y, area->width, area->height);
}

void CrosGtkIMContext::SetSurrounding(const char* text,
                                      int len,
                                      int cursor_index) {}

void CrosGtkIMContext::SetUsePreedit(gboolean use_preedit) {
  // GTK doesn't specify when exactly this should be called, but apps we've
  // found using this (Sublime, Inkscape) call it prior to activation. If we
  // find apps which behave differently, we might need to explicitly call
  // SetContentType() here.

  // This is not covered by automated tests yet. GtkTextView and GtkEntry both
  // do not expose the IM context they use, so we'd have to manually create one
  // ourselves.
  supports_preedit_ = use_preedit;
}

CrosGtkIMContext::BackendObserver::BackendObserver(CrosGtkIMContext* context)
    : context_(context) {}

void CrosGtkIMContext::BackendObserver::SetPreedit(
    const std::string& preedit,
    int cursor,
    const std::vector<PreeditStyle>& styles) {
  bool was_empty = context_->preedit_.empty();
  context_->preedit_ = preedit;
  context_->preedit_cursor_pos_ = cursor;
  context_->preedit_styles_ = styles;
  if (was_empty && !preedit.empty())
    g_signal_emit_by_name(context_, "preedit-start");
  g_signal_emit_by_name(context_, "preedit-changed");
  if (!was_empty && preedit.empty())
    g_signal_emit_by_name(context_, "preedit-end");
}

void CrosGtkIMContext::BackendObserver::Commit(const std::string& text) {
  if (!context_->preedit_.empty()) {
    context_->preedit_.clear();
    context_->preedit_cursor_pos_ = 0;
    context_->preedit_styles_.clear();
    g_signal_emit_by_name(context_, "preedit-changed");
    g_signal_emit_by_name(context_, "preedit-end");
  }
  g_signal_emit_by_name(context_, "commit", text.c_str());
}

void CrosGtkIMContext::BackendObserver::KeySym(uint32_t keysym,
                                               KeyState state,
                                               uint32_t modifiers) {
  // See comment in FilterKeypress for general context.

  // Some apps do not behave correctly if we immediately convert these into
  // commit events, so do that in FilterKeypress instead (b/255273154).

#ifdef GTK4
  LOG(WARNING) << "KeySym is currently unimplemented for GTK4. Dropped keysym: "
               << keysym;
  // TODO(b/283915925): In GTK4, gdkevent struct is readonly and we cannot
  // construct new events. Consider moving KeySym to sommelier side.
#else
  if (!context_->gdk_window_)
    return;

  // TODO(timloh): Chrome appears to only send press events currently.
  GdkEvent* raw_event = gdk_event_new(
      state == KeyState::kPressed ? GDK_KEY_PRESS : GDK_KEY_RELEASE);

  GdkEventKey* event = reinterpret_cast<GdkEventKey*>(raw_event);
  // Ref is dropped in gdk_event_free.
  g_set_object(&event->window, context_->gdk_window_);
  event->send_event = true;
  event->time = GDK_CURRENT_TIME;
  event->keyval = keysym;
  event->is_modifier = false;
  // TODO(timloh): Use text_input::modifiers_map to properly translate these.
  // It seems like Chrome's bit masks for shift, caps lock, ctrl and alt all
  // match GDK, but rarer modifier keys don't quite match.
  event->state = modifiers;

  // These are "deprecated and should never be used" so we leave them empty.
  // We may have to revisit if we find apps relying on these.
  event->length = 0;
  event->string = nullptr;

  GdkDisplay* gdk_display = gdk_window_get_display(context_->gdk_window_);
  GdkKeymapKey* keys;
  int n_keys;
  bool success = gdk_keymap_get_entries_for_keyval(
      gdk_keymap_get_for_display(gdk_display), keysym, &keys, &n_keys);
  if (success && keys) {
    event->hardware_keycode = keys[0].keycode;
    event->group = keys[0].group;
    g_free(keys);
  } else {
    // TODO(b/264834882): Currently our tests don't make fake keymaps so they
    // end up reaching here for non-ascii symbols, even though in practice we
    // would always (IIUC) be reaching the if branch.
    LOG(WARNING) << "Failed to find keycode for keysym: " << keysym;
    event->hardware_keycode = 0;
    event->group = 0;
  }

  gdk_event_set_device(
      raw_event,
      gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display)));
  gdk_display_put_event(gdk_display, raw_event);
  gdk_event_free(raw_event);
#endif
}

void CrosGtkIMContext::Activate() {
#ifdef GTK4
  // GTK4 may trigger multiple calls to Activate() (b/294469470).
  if (backend_->IsActive())
    return;

  if (!root_surface_) {
    LOG(WARNING) << "Tried to activate without an active window.";
    return;
  }

  if (is_x11_) {
    backend_->ActivateX11(gdk_x11_surface_get_xid(root_surface_));
  } else {
    wl_surface* surface = gdk_wayland_surface_get_wl_surface(root_surface_);
    if (!surface) {
      LOG(WARNING) << "GdkSurface doesn't have an associated wl_surface.";
      return;
    }
    backend_->Activate(surface);
  }
#else
  if (!top_level_gdk_window_) {
    LOG(WARNING) << "Tried to activate without an active window.";
    return;
  }

  if (is_x11_) {
    backend_->ActivateX11(gdk_x11_window_get_xid(top_level_gdk_window_));
  } else {
    wl_surface* surface =
        gdk_wayland_window_get_wl_surface(top_level_gdk_window_);
    if (!surface) {
      LOG(WARNING) << "GdkWindow doesn't have an associated wl_surface.";
      return;
    }
    backend_->Activate(surface);
  }
#endif

  pending_activation_ = false;

  // This request takes effect when we call SetContentType.
  // TODO(b/232048095): Support surrounding text.
  backend_->SetSupportsSurrounding(false);

  GtkInputHints gtk_hints = GTK_INPUT_HINT_NONE;
  GtkInputPurpose gtk_purpose = GTK_INPUT_PURPOSE_FREE_FORM;
  g_object_get(this, "input-hints", &gtk_hints, "input-purpose", &gtk_purpose,
               NULL);
  backend_->SetContentType(
      ConvertContentType(gtk_hints, gtk_purpose, supports_preedit_));

  if (!(gtk_hints & GTK_INPUT_HINT_INHIBIT_OSK))
    backend_->ShowInputPanel();
}

}  // namespace gtk
}  // namespace cros_im
