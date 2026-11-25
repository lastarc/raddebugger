// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_COCOA_H
#define MAC_COCOA_H

////////////////////////////////
//~ macOS Cocoa Bridge
//
// All Objective-C/Cocoa code lives in mac_cocoa.m, which is compiled as its
// own translation unit (see build.sh). Cocoa's headers define many names
// (FileInfo, nil, check, verify, Handle, ...) which collide with this
// codebase, so the unity build only ever sees this plain-C interface.
//
// The bridge has no link-time dependencies on the rest of the program; it
// calls back into the window manager layer via the registered callbacks.
// All functions must be called on the main thread, except for
// mac_cocoa_post_wakeup, which may be called from any thread.

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct MAC_CocoaWindow MAC_CocoaWindow;

typedef enum MAC_CocoaEventKind
{
  MAC_CocoaEventKind_Null,
  MAC_CocoaEventKind_KeyPress,
  MAC_CocoaEventKind_KeyRelease,
  MAC_CocoaEventKind_ModifierChange,
  MAC_CocoaEventKind_MousePress,
  MAC_CocoaEventKind_MouseRelease,
  MAC_CocoaEventKind_MouseMove,
  MAC_CocoaEventKind_Scroll,
  MAC_CocoaEventKind_Text,
  MAC_CocoaEventKind_LoseFocus,
  MAC_CocoaEventKind_Close,
  MAC_CocoaEventKind_FileDrop,
  MAC_CocoaEventKind_QuitRequest,
}
MAC_CocoaEventKind;

typedef enum MAC_CocoaHit
{
  MAC_CocoaHit_Client      = 0,
  MAC_CocoaHit_Caption     = (1<<0),
  MAC_CocoaHit_EdgeLeft    = (1<<1),
  MAC_CocoaHit_EdgeRight   = (1<<2),
  MAC_CocoaHit_EdgeTop     = (1<<3),
  MAC_CocoaHit_EdgeBottom  = (1<<4),
}
MAC_CocoaHit;

typedef enum MAC_CocoaCursor
{
  MAC_CocoaCursor_Arrow,
  MAC_CocoaCursor_IBeam,
  MAC_CocoaCursor_ResizeLeftRight,
  MAC_CocoaCursor_ResizeUpDown,
  MAC_CocoaCursor_ResizeDiagonalDownRight,
  MAC_CocoaCursor_ResizeDiagonalUpRight,
  MAC_CocoaCursor_OpenHand,
  MAC_CocoaCursor_PointingHand,
  MAC_CocoaCursor_NotAllowed,
  MAC_CocoaCursor_COUNT
}
MAC_CocoaCursor;

typedef struct MAC_CocoaEvent MAC_CocoaEvent;
struct MAC_CocoaEvent
{
  MAC_CocoaEventKind kind;
  void *user_window;            // the user pointer passed to mac_cocoa_window_open
  uint64_t modifier_flags;      // NSEventModifierFlags (incl. device-dependent bits)
  uint16_t keycode;             // hardware virtual keycode (kVK_*)
  uint32_t layout_letter;       // 'A'..'Z' if this key types a letter in the current layout, else 0
  int32_t is_repeat;
  int32_t mouse_button;         // 0: left, 1: right, 2: middle, else other
  int32_t click_count;
  uint32_t codepoint;           // for text events
  float x, y;                   // in pixels, relative to the view's top-left
  float dx, dy;                 // scroll deltas, in Windows wheel units (120/notch, +y = down)
  char **paths;                 // for file drops (valid only during the callback)
  uint64_t path_count;
};

typedef struct MAC_CocoaCallbacks MAC_CocoaCallbacks;
struct MAC_CocoaCallbacks
{
  void (*push_event)(MAC_CocoaEvent *event);
  int  (*hit_test)(void *user_window, float x, float y); // returns MAC_CocoaHit flags
  void (*modal_update)(void);                            // redraw from within modal loops (live resize)
};

//- init
void     mac_cocoa_init(MAC_CocoaCallbacks *callbacks, const void *icon_png_data, uint64_t icon_png_size);
double   mac_cocoa_double_click_interval(void);
double   mac_cocoa_main_display_refresh_rate(void);

//- events
int32_t  mac_cocoa_pump_events(int32_t wait); // returns 1 if a wakeup was received (or pumping is suspended)
void     mac_cocoa_post_wakeup(void);
uint64_t mac_cocoa_current_modifier_flags(void);
uint64_t mac_cocoa_pressed_mouse_buttons(void);
void     mac_cocoa_set_cursor(MAC_CocoaCursor cursor);

//- clipboard
void     mac_cocoa_set_clipboard_text(const void *utf8, uint64_t size);
char    *mac_cocoa_get_clipboard_text_malloc(uint64_t *size_out); // caller frees with free()

//- windows
MAC_CocoaWindow *mac_cocoa_window_open(void *user_window, double x, double y_top, double width, double height, int32_t custom_border, int32_t center, const void *title_utf8, uint64_t title_size);
void     mac_cocoa_window_close(MAC_CocoaWindow *window);
void     mac_cocoa_window_set_title(MAC_CocoaWindow *window, const void *title_utf8, uint64_t title_size);
void     mac_cocoa_window_show(MAC_CocoaWindow *window);
void     mac_cocoa_window_focus(MAC_CocoaWindow *window);
int32_t  mac_cocoa_window_is_focused(MAC_CocoaWindow *window);
int32_t  mac_cocoa_window_is_fullscreen(MAC_CocoaWindow *window);
void     mac_cocoa_window_set_fullscreen(MAC_CocoaWindow *window, int32_t fullscreen);
int32_t  mac_cocoa_window_is_maximized(MAC_CocoaWindow *window);
void     mac_cocoa_window_set_maximized(MAC_CocoaWindow *window, int32_t maximized);
int32_t  mac_cocoa_window_is_minimized(MAC_CocoaWindow *window);
void     mac_cocoa_window_set_minimized(MAC_CocoaWindow *window, int32_t minimized);
void     mac_cocoa_window_bring_to_front(MAC_CocoaWindow *window);
void     mac_cocoa_window_move_to_display(MAC_CocoaWindow *window, uint32_t display_id);
void     mac_cocoa_window_frame(MAC_CocoaWindow *window, double *x_out, double *y_top_out, double *width_out, double *height_out);
void     mac_cocoa_window_client_size_pixels(MAC_CocoaWindow *window, double *width_out, double *height_out);
double   mac_cocoa_window_scale(MAC_CocoaWindow *window);
uint32_t mac_cocoa_window_display_id(MAC_CocoaWindow *window);
void     mac_cocoa_window_mouse_pixels(MAC_CocoaWindow *window, float *x_out, float *y_out);

//- displays
uint64_t mac_cocoa_display_count(void);
uint32_t mac_cocoa_display_id_from_index(uint64_t index);
int32_t  mac_cocoa_display_name(uint32_t display_id, char *buffer, uint64_t buffer_size);
int32_t  mac_cocoa_display_size(uint32_t display_id, double *width_out, double *height_out);
double   mac_cocoa_display_scale(uint32_t display_id);

//- other applications
int32_t  mac_cocoa_frontmost_application_pid(void);
void     mac_cocoa_activate_application_pid(int32_t pid);

//- OpenGL
int32_t  mac_cocoa_gl_init(void);
void     mac_cocoa_gl_select_window(MAC_CocoaWindow *window);
void     mac_cocoa_gl_release_window(MAC_CocoaWindow *window);
void     mac_cocoa_gl_swap(void);

#if defined(__cplusplus)
}
#endif

#endif // MAC_COCOA_H
