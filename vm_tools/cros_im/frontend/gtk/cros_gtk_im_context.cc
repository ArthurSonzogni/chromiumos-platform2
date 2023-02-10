// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frontend/gtk/cros_gtk_im_context.h"

#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>
#include <gdk/gdkx.h>
// Remove definitions from X11 headers that collide with our code.
#undef FocusIn
#undef FocusOut
#include <cstring>
#include <iostream>
#include <utility>

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
  im_context->set_client_window = Fwd<&CrosGtkIMContext::SetClientWindow>;
  im_context->get_preedit_string = Fwd<&CrosGtkIMContext::GetPreeditString>;
  im_context->filter_keypress = Fwd<&CrosGtkIMContext::FilterKeypress>;
  im_context->focus_in = Fwd<&CrosGtkIMContext::FocusIn>;
  im_context->focus_out = Fwd<&CrosGtkIMContext::FocusOut>;
  im_context->reset = Fwd<&CrosGtkIMContext::Reset>;
  im_context->set_cursor_location = Fwd<&CrosGtkIMContext::SetCursorLocation>;
  im_context->set_surrounding = Fwd<&CrosGtkIMContext::SetSurrounding>;
}

void cros_gtk_im_context_class_finalize(CrosGtkIMContextClass* klass) {}

// end of GObject integration
////////////////////////////////////////////////////////////////////////////////

uint32_t GetZwpHintsFromGtk(GtkInputHints gtk_hints) {
  uint32_t zwp_hints = ZWP_TEXT_INPUT_V1_CONTENT_HINT_NONE;

  // Input hints are not often set so default to turning on auto completion and
  // auto correction unless explicitly disabled.

  // Don't require GTK_INPUT_HINT_SPELLCHECK
  if (!(gtk_hints & GTK_INPUT_HINT_NO_SPELLCHECK))
    zwp_hints |= ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_CORRECTION;

  // Don't require GTK_INPUT_HINT_WORD_COMPLETION
  zwp_hints |= ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_COMPLETION;

  // TODO(timloh): Current TITLECASE and and AUTO_CAPITALIZATION seem to make
  // text entirely uppercase, maybe due to lack of surrounding text support.
  // Test this (e.g. <input autocapitalize="on"> in Firefox) when VK support
  // is improved, and default to auto capitalize once this works.
  if (gtk_hints & GTK_INPUT_HINT_LOWERCASE) {
    zwp_hints |= ZWP_TEXT_INPUT_V1_CONTENT_HINT_LOWERCASE;
  } else if (gtk_hints & GTK_INPUT_HINT_UPPERCASE_CHARS) {
    zwp_hints |= ZWP_TEXT_INPUT_V1_CONTENT_HINT_UPPERCASE;
  } else if (gtk_hints & GTK_INPUT_HINT_UPPERCASE_WORDS) {
    zwp_hints |= ZWP_TEXT_INPUT_V1_CONTENT_HINT_TITLECASE;
  } else if (gtk_hints & GTK_INPUT_HINT_UPPERCASE_SENTENCES) {
    zwp_hints |= ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_CAPITALIZATION;
  }

  // Handled explicitly in FocusIn():
  // - GTK_INPUT_HINT_INHIBIT_OSK
  // Not supported:
  // - GTK_INPUT_HINT_VERTICAL_WRITING
  // - GTK_INPUT_HINT_EMOJI
  // - GTK_INPUT_HINT_NO_EMOJI
  // - ZWP_TEXT_INPUT_V1_CONTENT_HINT_LATIN
  // - ZWP_TEXT_INPUT_V1_CONTENT_HINT_MULTILINE
  // GetZwpHintsPurposeFromGtk special-cases a PASSWORD or PIN purpose, ignoring
  // any explicitly set hints and instead setting:
  // - ZWP_TEXT_INPUT_V1_CONTENT_HINT_HIDDEN_TEXT
  // - ZWP_TEXT_INPUT_V1_CONTENT_HINT_SENSITIVE_DATA
  // - (aka ZWP_TEXT_INPUT_V1_CONTENT_HINT_PASSWORD)
  return zwp_hints;
}

uint32_t GetZwpPurposeFromGtk(GtkInputPurpose gtk_purpose) {
  switch (gtk_purpose) {
    case GTK_INPUT_PURPOSE_FREE_FORM:
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_NORMAL;
    case GTK_INPUT_PURPOSE_ALPHA:
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_ALPHA;
    case GTK_INPUT_PURPOSE_DIGITS:
    case GTK_INPUT_PURPOSE_PIN:
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_DIGITS;
    case GTK_INPUT_PURPOSE_NUMBER:
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_NUMBER;
    case GTK_INPUT_PURPOSE_PHONE:
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_PHONE;
    case GTK_INPUT_PURPOSE_URL:
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_URL;
    case GTK_INPUT_PURPOSE_EMAIL:
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_EMAIL;
    case GTK_INPUT_PURPOSE_NAME:
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_NAME;
    case GTK_INPUT_PURPOSE_PASSWORD:
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_PASSWORD;
    default:
      g_warning("Unknown GtkInputPurpose %d", static_cast<int>(gtk_purpose));
      return ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_NORMAL;
  }

  // Not supported:
  // - ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_DATE
  // - ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_TIME
  // - ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_DATETIME
  // - ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_TERMINAL
}

IMContextBackend::ContentType GetZwpHintsPurposeFromGtk(
    GtkInputHints gtk_hints, GtkInputPurpose gtk_purpose) {
  uint32_t zwp_purpose = GetZwpPurposeFromGtk(gtk_purpose);

  uint32_t zwp_hints;
  if (gtk_purpose == GTK_INPUT_PURPOSE_PASSWORD ||
      gtk_purpose == GTK_INPUT_PURPOSE_PIN) {
    zwp_hints = ZWP_TEXT_INPUT_V1_CONTENT_HINT_PASSWORD;
  } else {
    zwp_hints = GetZwpHintsFromGtk(gtk_hints);
  }

  return {.hints = zwp_hints, .purpose = zwp_purpose};
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

void CrosGtkIMContext::RegisterType(GTypeModule* module) {
  cros_gtk_im_context_register_type(module);
}

CrosGtkIMContext* CrosGtkIMContext::Create() {
  return ToCrosGtkIMContext(
      g_object_new(cros_gtk_im_context_get_type(), nullptr));
}

CrosGtkIMContext::CrosGtkIMContext()
    : backend_observer_(this),
      backend_(std::make_unique<IMContextBackend>(&backend_observer_)) {
  is_x11_ = GDK_IS_X11_DISPLAY(gdk_display_get_default());
}

CrosGtkIMContext::~CrosGtkIMContext() = default;

void CrosGtkIMContext::SetClientWindow(GdkWindow* window) {
  if (window) {
    GdkWindow* toplevel = gdk_window_get_effective_toplevel(window);
    g_set_object(&gdk_window_, window);
    g_set_object(&top_level_gdk_window_, toplevel);
    if (!top_level_gdk_window_)
      g_warning("Top-level GdkWindow was null");
    if (pending_activation_)
      Activate();
  } else {
    g_set_object(&gdk_window_, nullptr);
    g_set_object(&top_level_gdk_window_, nullptr);
  }
}

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

gboolean CrosGtkIMContext::FilterKeypress(GdkEventKey* event) {
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

  if (event->type != GDK_KEY_PRESS) {
    return false;
  }

  // Don't consume events with modifiers like <Ctrl>.
  GdkDisplay* gdk_display = gdk_window_get_display(gdk_window_);
  GdkModifierType no_text_input_mask =
      gdk_keymap_get_modifier_mask(gdk_keymap_get_for_display(gdk_display),
                                   GDK_MODIFIER_INTENT_NO_TEXT_INPUT);
  if (event->state & no_text_input_mask) {
    return false;
  }

  uint32_t c = gdk_keyval_to_unicode(event->keyval);
  if (!c || g_unichar_iscntrl(c)) {
    return false;
  }

  // g_unichar_to_utf8() supposedly requires 6 bytes of space, despite UTF-8
  // only needing 4 bytes.
  char utf8[6];
  size_t len = g_unichar_to_utf8(c, utf8);
  backend_observer_.Commit({utf8, len});
  return true;
}

void CrosGtkIMContext::FocusIn() {
  if (top_level_gdk_window_) {
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
  } else {
    backend_->Deactivate();
  }
}

void CrosGtkIMContext::Reset() {
  backend_->Reset();
}

void CrosGtkIMContext::SetCursorLocation(GdkRectangle* area) {
  if (!gdk_window_)
    return;

  int offset_x = 0, offset_y = 0;
  gdk_window_get_origin(gdk_window_, &offset_x, &offset_y);

  // When running directly under Wayland, these are usually (always?) zero,
  // but typically non-zero when running under X11.
  int top_level_x = 0, top_level_y = 0;
  gdk_window_get_origin(top_level_gdk_window_, &top_level_x, &top_level_y);

  backend_->SetCursorLocation(offset_x - top_level_x + area->x,
                              offset_y - top_level_y + area->y, area->width,
                              area->height);

  UpdateSurrounding();
}

void CrosGtkIMContext::SetSurrounding(const char* text,
                                      int len,
                                      int cursor_index) {
  if (len == -1) {
    // Null-terminated
    surrounding_ = text;
  } else {
    // Not necessarily null-terminated
    surrounding_ = std::string(text, len);
  }
  surrounding_cursor_pos_ = cursor_index;
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

void CrosGtkIMContext::BackendObserver::SetPreeditRegion(
    int start_offset, int length, const std::vector<PreeditStyle>& styles) {
  std::optional<std::string> text =
      DeleteSurroundingTextImpl(start_offset, length);
  if (!text.has_value())
    return;

  context_->preedit_ = std::move(text.value());
  context_->preedit_cursor_pos_ = length;
  context_->preedit_styles_ = styles;

  g_signal_emit_by_name(context_, "preedit-start");
  g_signal_emit_by_name(context_, "preedit-changed");
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

void CrosGtkIMContext::BackendObserver::DeleteSurroundingText(int start_offset,
                                                              int length) {
  DeleteSurroundingTextImpl(start_offset, length);
}

void CrosGtkIMContext::BackendObserver::KeySym(uint32_t keysym,
                                               KeyState state,
                                               uint32_t modifiers) {
  // See comment in FilterKeypress for general context.

  // Some apps do not behave correctly if we immediately convert these into
  // commit events, so do that in FilterKeypress insead (b/255273154).

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
    g_warning("Failed to find keycode for keysym %u", keysym);
    event->hardware_keycode = 0;
    event->group = 0;
  }

  gdk_event_set_device(
      raw_event,
      gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display)));
  gdk_display_put_event(gdk_display, raw_event);
  gdk_event_free(raw_event);
}

std::optional<std::string>
CrosGtkIMContext::BackendObserver::DeleteSurroundingTextImpl(
    int byte_start_offset, int byte_length) {
  g_assert(byte_start_offset <= 0 && byte_start_offset + byte_length >= 0);

  if (!context_->preedit_.empty()) {
    // TODO(timloh): Work out the correct behaviour here. Should we commit the
    // existing pre-edit text first?
    g_warning(
        "DeleteSurroundingText() called when pre-edit was already present");
    return std::nullopt;
  }

  if (!context_->RetrieveSurrounding()) {
    g_warning(
        "Failed to retrieve surrounding text for DeleteSurroundingText().");
    return std::nullopt;
  }

  const char* surrounding_start = context_->surrounding_.c_str();
  const char* surrounding_end =
      surrounding_start + context_->surrounding_.size();
  const char* cursor = surrounding_start + context_->surrounding_cursor_pos_;
  const char* region_start = cursor + byte_start_offset;
  const char* region_end = region_start + byte_length;

  if (region_start < surrounding_start || region_end > surrounding_end) {
    g_warning(
        "Not enough surrounding text to handle DeleteSurroundingText(%d, %d). "
        "Surrounding text is %zu bytes with cursor at %d.",
        byte_start_offset, byte_length, context_->surrounding_.size(),
        context_->surrounding_cursor_pos_);
    return std::nullopt;
  }

  if (!g_utf8_validate(region_start, byte_length, nullptr)) {
    g_warning("DeleteSurroundingText() cannot delete invalid UTF-8 regions.");
    return std::nullopt;
  }

  int char_offset = -g_utf8_strlen(region_start, -byte_start_offset);
  int char_length = g_utf8_strlen(region_start, byte_length);

  gboolean result = false;
  g_signal_emit_by_name(context_, "delete-surrounding", char_offset,
                        char_length, &result);
  if (!result) {
    g_warning("Failed to delete surrounding text for DeleteSurroundingText().");
    return std::nullopt;
  }

  return std::string(region_start, region_end);
}

void CrosGtkIMContext::Activate() {
  if (!top_level_gdk_window_) {
    g_warning("Tried to activate without an active window.");
    return;
  }

  if (is_x11_) {
    backend_->ActivateX11(gdk_x11_window_get_xid(top_level_gdk_window_));
  } else {
    wl_surface* surface =
        gdk_wayland_window_get_wl_surface(top_level_gdk_window_);
    if (!surface) {
      g_warning("GdkWindow doesn't have an associated wl_surface.");
      return;
    }
    backend_->Activate(surface);
  }

  pending_activation_ = false;

  GtkInputHints gtk_hints = GTK_INPUT_HINT_NONE;
  GtkInputPurpose gtk_purpose = GTK_INPUT_PURPOSE_FREE_FORM;
  g_object_get(this, "input-hints", &gtk_hints, "input-purpose", &gtk_purpose,
               NULL);
  backend_->SetContentType(GetZwpHintsPurposeFromGtk(gtk_hints, gtk_purpose));

  if (!(gtk_hints & GTK_INPUT_HINT_INHIBIT_OSK))
    backend_->ShowInputPanel();

  // Apps should be calling set_cursor_location on focus, which would result in
  // us updating surrounding text, but to support apps that don't do that we
  // also explicitly update surrounding text here.
  UpdateSurrounding();
}

bool CrosGtkIMContext::RetrieveSurrounding() {
  gboolean result = false;
  // SetSurrounding() gets called when this succeeds.
  g_signal_emit_by_name(this, "retrieve-surrounding", &result);
  if (!result)
    g_warning("Failed to retrieve surrounding text.");
  return result;
}

void CrosGtkIMContext::UpdateSurrounding() {
  if (!RetrieveSurrounding())
    return;

  size_t length = surrounding_.length();

  // There is a maximum length to Wayland messages and sending a message that
  // is too long will result in a crash. The actual limit appears to be around
  // 4075 bytes, but we give a bit of leeway here and match the limit Lacros
  // uses.
  constexpr size_t kMaxSurroundingTextByteLength = 4000;

  if (length <= kMaxSurroundingTextByteLength) {
    backend_->SetSurrounding(surrounding_.c_str(), surrounding_cursor_pos_);
    return;
  }

  // TODO(b/232048905): Send a substring of the surrounding text instead of
  // doing nothing.
}

}  // namespace gtk
}  // namespace cros_im
