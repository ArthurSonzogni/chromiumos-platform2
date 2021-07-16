// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_H_

#include <linux/types.h>
#include <memory>
#include <sys/types.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>

#include "sommelier-ctx.h"     // NOLINT(build/include_directory)
#include "sommelier-global.h"  // NOLINT(build/include_directory)
#include "sommelier-mmap.h"    // NOLINT(build/include_directory)
#include "sommelier-timing.h"  // NOLINT(build/include_directory)
#include "sommelier-util.h"    // NOLINT(build/include_directory)
#include "sommelier-window.h"  // NOLINT(build/include_directory)
#include "virtualization/wayland_channel.h"

#define SOMMELIER_VERSION "0.20"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define CONTROL_MASK (1 << 0)
#define ALT_MASK (1 << 1)
#define SHIFT_MASK (1 << 2)


struct sl_global;
struct sl_compositor;
struct sl_shm;
struct sl_shell;
struct sl_data_device_manager;
struct sl_data_offer;
struct sl_data_source;
struct sl_xdg_shell;
struct sl_subcompositor;
struct sl_aura_shell;
struct sl_viewporter;
struct sl_linux_dmabuf;
struct sl_keyboard_extension;
struct sl_text_input_manager;
struct sl_relative_pointer_manager;
struct sl_pointer_constraints;
struct sl_window;
struct zaura_shell;
struct zcr_keyboard_extension_v1;

#ifdef GAMEPAD_SUPPORT
struct sl_gamepad;
struct sl_gaming_input_manager;
struct zcr_gaming_input_v2;
#endif

class WaylandChannel;

extern const struct wl_registry_listener sl_registry_listener;

struct sl_context {
  char** runprog;
  struct wl_display* display;
  struct wl_display* host_display;
  struct wl_client* client;
  struct sl_compositor* compositor;
  struct sl_subcompositor* subcompositor;
  struct sl_shm* shm;
  struct sl_shell* shell;
  struct sl_data_device_manager* data_device_manager;
  struct sl_xdg_shell* xdg_shell;
  struct sl_aura_shell* aura_shell;
  struct sl_viewporter* viewporter;
  struct sl_linux_dmabuf* linux_dmabuf;
  struct sl_linux_explicit_synchronization* linux_explicit_synchronization;
  struct sl_keyboard_extension* keyboard_extension;
  struct sl_text_input_manager* text_input_manager;
#ifdef GAMEPAD_SUPPORT
  struct sl_gaming_input_manager* gaming_input_manager;
#endif
  struct sl_relative_pointer_manager* relative_pointer_manager;
  struct sl_pointer_constraints* pointer_constraints;
  struct wl_list outputs;
  struct wl_list seats;
  std::unique_ptr<struct wl_event_source> display_event_source;
  std::unique_ptr<struct wl_event_source> display_ready_event_source;
  std::unique_ptr<struct wl_event_source> sigchld_event_source;
  std::unique_ptr<struct wl_event_source> sigusr1_event_source;
  std::unique_ptr<struct wl_event_source> clipboard_event_source;
  struct wl_array dpi;
  int wm_fd;
  int wayland_channel_fd;
  int virtwl_socket_fd;
  int virtwl_display_fd;
  std::unique_ptr<struct wl_event_source> wayland_channel_event_source;
  std::unique_ptr<struct wl_event_source> virtwl_socket_event_source;
  const char* vm_id;
  const char* drm_device;
  struct gbm_device* gbm;
  int xwayland;
  pid_t xwayland_pid;
  pid_t child_pid;
  pid_t peer_pid;
  struct xkb_context* xkb_context;
  struct wl_list accelerators;
  struct wl_list registries;
  struct wl_list globals;
  struct wl_list host_outputs;
  int next_global_id;
  xcb_connection_t* connection;
  std::unique_ptr<struct wl_event_source> connection_event_source;
  const xcb_query_extension_reply_t* xfixes_extension;
  xcb_screen_t* screen;
  xcb_window_t window;
  struct wl_list windows, unpaired_windows;
  struct sl_window* host_focus_window;
  int needs_set_input_focus;
#ifdef GAMEPAD_SUPPORT
  struct wl_list gamepads;
#endif
  double desired_scale;
  double scale;
  const char* application_id;
  int exit_with_child;
  const char* sd_notify;
  int clipboard_manager;
  uint32_t frame_color;
  uint32_t dark_frame_color;
  bool support_damage_buffer;
  int fullscreen_mode;
  struct sl_host_seat* default_seat;
  xcb_window_t selection_window;
  xcb_window_t selection_owner;
  int selection_incremental_transfer;
  xcb_selection_request_event_t selection_request;
  xcb_timestamp_t selection_timestamp;
  struct wl_data_device* selection_data_device;
  struct sl_data_offer* selection_data_offer;
  struct sl_data_source* selection_data_source;
  int selection_data_source_send_fd;
  struct wl_list selection_data_source_send_pending;
  std::unique_ptr<struct wl_event_source> selection_send_event_source;
  xcb_get_property_reply_t* selection_property_reply;
  int selection_property_offset;
  std::unique_ptr<struct wl_event_source> selection_event_source;
  xcb_atom_t selection_data_type;
  struct wl_array selection_data;
  int selection_data_offer_receive_fd;
  int selection_data_ack_pending;
  union {
    const char* name;
    xcb_intern_atom_cookie_t cookie;
    xcb_atom_t value;
  } atoms[ATOM_LAST + 1];
  xcb_visualid_t visual_ids[256];
  xcb_colormap_t colormaps[256];
  Timing* timing;
  const char* trace_filename;
  bool trace_system;
  bool use_explicit_fence;
  bool use_virtgpu_channel;
  // Never freed after allocation due the fact sommelier doesn't have a
  // shutdown function yet.
  WaylandChannel* channel;
};

// We require a host compositor supporting at least this wl_compositor version.
constexpr uint32_t kMinHostWlCompositorVersion =
    WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION;

struct sl_compositor {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_global;
  struct wl_compositor* internal;
};

struct sl_shm {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_global;
  struct wl_shm* internal;
};

struct sl_seat {
  struct sl_context* ctx;
  uint32_t id;
  uint32_t version;
  struct sl_global* host_global;
  uint32_t last_serial;
  struct wl_list link;
};

struct sl_host_pointer {
  struct sl_seat* seat;
  struct wl_resource* resource;
  struct wl_pointer* proxy;
  struct wl_resource* focus_resource;
  struct wl_listener focus_resource_listener;
  uint32_t focus_serial;
  uint32_t time;
  wl_fixed_t axis_delta[2];
  int32_t axis_discrete[2];
};

struct sl_relative_pointer_manager {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_global;
  struct zwp_relative_pointer_manager_v1* internal;
};

struct sl_viewport {
  struct wl_list link;
  wl_fixed_t src_x;
  wl_fixed_t src_y;
  wl_fixed_t src_width;
  wl_fixed_t src_height;
  int32_t dst_width;
  int32_t dst_height;
};

struct sl_host_callback {
  struct wl_resource* resource;
  struct wl_callback* proxy;
};

struct sl_host_surface {
  struct sl_context* ctx;
  struct wl_resource* resource;
  struct wl_surface* proxy;
  struct wp_viewport* viewport;
  uint32_t contents_width;
  uint32_t contents_height;
  int32_t contents_scale;
  struct wl_list contents_viewport;
  struct sl_mmap* contents_shm_mmap;
  int has_role;
  int has_output;
  uint32_t last_event_serial;
  struct sl_output_buffer* current_buffer;
  struct zwp_linux_surface_synchronization_v1* surface_sync;
  struct wl_list released_buffers;
  struct wl_list busy_buffers;
};

struct sl_host_region {
  struct sl_context* ctx;
  struct wl_resource* resource;
  struct wl_region* proxy;
};

struct sl_host_buffer {
  struct sl_context* ctx;
  struct wl_resource* resource;
  struct wl_buffer* proxy;
  uint32_t width;
  uint32_t height;
  struct sl_mmap* shm_mmap;
  uint32_t shm_format;
  struct sl_sync_point* sync_point;
};

struct sl_data_source_send_request {
  int fd;
  xcb_intern_atom_cookie_t cookie;
  struct sl_data_source* data_source;
  struct wl_list link;
};

struct sl_subcompositor {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_global;
};

struct sl_shell {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_global;
};

struct sl_output {
  struct sl_context* ctx;
  uint32_t id;
  uint32_t version;
  struct sl_global* host_global;
  struct wl_list link;
};

struct sl_host_output {
  struct sl_context* ctx;
  struct wl_resource* resource;
  struct wl_output* proxy;
  struct zaura_output* aura_output;
  int internal;
  int x;
  int y;
  int physical_width;
  int physical_height;
  int subpixel;
  char* make;
  char* model;
  int transform;
  uint32_t flags;
  int width;
  int height;
  int refresh;
  int scale_factor;
  int current_scale;
  int preferred_scale;
  int device_scale_factor;
  int expecting_scale;
  struct wl_list link;
};

struct sl_host_seat {
  struct sl_seat* seat;
  struct wl_resource* resource;
  struct wl_seat* proxy;
};

struct sl_accelerator {
  struct wl_list link;
  uint32_t modifiers;
  xkb_keysym_t symbol;
};

struct sl_keyboard_extension {
  struct sl_context* ctx;
  uint32_t id;
  struct zcr_keyboard_extension_v1* internal;
};

struct sl_data_device_manager {
  struct sl_context* ctx;
  uint32_t id;
  uint32_t version;
  struct sl_global* host_global;
  struct wl_data_device_manager* internal;
};

struct sl_data_offer {
  struct sl_context* ctx;
  struct wl_data_offer* internal;
  struct wl_array atoms;    // Contains xcb_atom_t
  struct wl_array cookies;  // Contains xcb_intern_atom_cookie_t
};

struct sl_text_input_manager {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_global;
  struct zwp_text_input_manager_v1* internal;
};

#ifdef GAMEPAD_SUPPORT
struct sl_gaming_input_manager {
  struct sl_context* ctx;
  uint32_t id;
  struct zcr_gaming_input_v2* internal;
};
#endif

struct sl_pointer_constraints {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_global;
  struct zwp_pointer_constraints_v1* internal;
};

struct sl_viewporter {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_viewporter_global;
  struct wp_viewporter* internal;
};

struct sl_xdg_shell {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_global;
  struct zxdg_shell_v6* internal;
};

struct sl_aura_shell {
  struct sl_context* ctx;
  uint32_t id;
  uint32_t version;
  struct sl_global* host_gtk_shell_global;
  struct zaura_shell* internal;
};

struct sl_linux_dmabuf {
  struct sl_context* ctx;
  uint32_t id;
  uint32_t version;
  struct sl_global* host_drm_global;
  struct zwp_linux_dmabuf_v1* internal;
};

struct sl_linux_explicit_synchronization {
  struct sl_context* ctx;
  uint32_t id;
  struct zwp_linux_explicit_synchronization_v1* internal;
};

struct sl_global {
  struct sl_context* ctx;
  const struct wl_interface* interface;
  uint32_t name;
  uint32_t version;
  void* data;
  wl_global_bind_func_t bind;
  struct wl_list link;
};

struct sl_host_registry {
  struct sl_context* ctx;
  struct wl_resource* resource;
  struct wl_list link;
};


typedef void (*sl_sync_func_t)(struct sl_context* ctx,
                               struct sl_sync_point* sync_point);

struct sl_sync_point {
  int fd;
  sl_sync_func_t sync;
};

#ifdef GAMEPAD_SUPPORT
struct sl_host_gamepad {
  struct sl_context* ctx;
  int state;
  struct libevdev* ev_dev;
  struct libevdev_uinput* uinput_dev;
  bool stadia;
  struct wl_list link;
};
#endif

struct sl_host_buffer* sl_create_host_buffer(struct sl_context* ctx,
                                             struct wl_client* client,
                                             uint32_t id,
                                             struct wl_buffer* proxy,
                                             int32_t width,
                                             int32_t height);


struct sl_global* sl_compositor_global_create(struct sl_context* ctx);
void sl_compositor_init_context(struct sl_context* ctx,
                                struct wl_registry* registry,
                                uint32_t id,
                                uint32_t version);

size_t sl_shm_bpp_for_shm_format(uint32_t format);

size_t sl_shm_num_planes_for_shm_format(uint32_t format);

struct sl_global* sl_shm_global_create(struct sl_context* ctx);

struct sl_global* sl_subcompositor_global_create(struct sl_context* ctx);

struct sl_global* sl_shell_global_create(struct sl_context* ctx);

double sl_output_aura_scale_factor_to_double(int scale_factor);

void sl_output_send_host_output_state(struct sl_host_output* host);

struct sl_global* sl_output_global_create(struct sl_output* output);

struct sl_global* sl_seat_global_create(struct sl_seat* seat);

struct sl_global* sl_relative_pointer_manager_global_create(
    struct sl_context* ctx);

struct sl_global* sl_data_device_manager_global_create(struct sl_context* ctx);

struct sl_global* sl_viewporter_global_create(struct sl_context* ctx);

struct sl_global* sl_xdg_shell_global_create(struct sl_context* ctx);

struct sl_global* sl_gtk_shell_global_create(struct sl_context* ctx);

struct sl_global* sl_drm_global_create(struct sl_context* ctx);

struct sl_global* sl_text_input_manager_global_create(struct sl_context* ctx);

struct sl_global* sl_pointer_constraints_global_create(struct sl_context* ctx);

void sl_set_display_implementation(struct sl_context* ctx);


struct sl_sync_point* sl_sync_point_create(int fd);
void sl_sync_point_destroy(struct sl_sync_point* sync_point);

void sl_host_seat_added(struct sl_host_seat* host);
void sl_host_seat_removed(struct sl_host_seat* host);

void sl_restack_windows(struct sl_context* ctx, uint32_t focus_resource_id);

void sl_roundtrip(struct sl_context* ctx);


struct sl_window* sl_lookup_window(struct sl_context* ctx, xcb_window_t id);
int sl_is_our_window(struct sl_context* ctx, xcb_window_t id);

#ifdef GAMEPAD_SUPPORT
void sl_gaming_seat_add_listener(struct sl_context* ctx);
#endif

#define sl_array_for_each(pos, array)                                   \
  for (pos = static_cast<typeof(pos)>((array)->data);                   \
       (const char*)pos < ((const char*)(array)->data + (array)->size); \
       (pos)++)

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_H_
