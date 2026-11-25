// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_WINDOW_MANAGER_H
#define MAC_WINDOW_MANAGER_H

////////////////////////////////
//~ Includes

#include "mac/cocoa/mac_cocoa.h"

////////////////////////////////
//~ NSEventModifierFlags (mirrored, as this translation unit is plain C)

#define MAC_WM_ModifierFlag_CapsLock    (1ull << 16)
#define MAC_WM_ModifierFlag_Shift       (1ull << 17)
#define MAC_WM_ModifierFlag_Control     (1ull << 18)
#define MAC_WM_ModifierFlag_Option      (1ull << 19)
#define MAC_WM_ModifierFlag_Command     (1ull << 20)

// device-dependent bits (IOKit's NX_DEVICE*KEYMASK), to tell left/right apart
#define MAC_WM_DeviceFlag_LCtl          0x00000001ull
#define MAC_WM_DeviceFlag_LShift        0x00000002ull
#define MAC_WM_DeviceFlag_RShift        0x00000004ull
#define MAC_WM_DeviceFlag_LCmd          0x00000008ull
#define MAC_WM_DeviceFlag_RCmd          0x00000010ull
#define MAC_WM_DeviceFlag_LAlt          0x00000020ull
#define MAC_WM_DeviceFlag_RAlt          0x00000040ull
#define MAC_WM_DeviceFlag_RCtl          0x00002000ull

////////////////////////////////
//~ Window State

typedef struct MAC_WM_WindowClientArea MAC_WM_WindowClientArea;
struct MAC_WM_WindowClientArea
{
  MAC_WM_WindowClientArea *next;
  Rng2F32 rect;
};

typedef struct MAC_WM_Window MAC_WM_Window;
struct MAC_WM_Window
{
  MAC_WM_Window *next;
  MAC_WM_Window *prev;
  MAC_CocoaWindow *cocoa;
  B32 custom_border;
  F32 custom_title_bar_thickness;
  F32 custom_edge_thickness;
  MAC_WM_WindowClientArea *first_client_area;
  MAC_WM_WindowClientArea *last_client_area;
};

////////////////////////////////
//~ State Bundle

typedef struct MAC_WM_State MAC_WM_State;
struct MAC_WM_State
{
  Arena *arena;
  MAC_WM_Window *first_window;
  MAC_WM_Window *last_window;
  MAC_WM_Window *free_window;
  MAC_WM_WindowClientArea *free_client_area;
  WM_SystemInfo gfx_info;
  WM_Cursor last_set_cursor;

  // NOTE: Cocoa callbacks may run anywhere inside event dispatch, including
  // nested modal loops (live resize); events accumulate here, & are handed
  // out (deep-copied) by wm_get_events.
  Arena *pending_events_arena;
  WM_EventList pending_events;

  U64 key_down_state[(WM_Key_COUNT+63)/64];
};

////////////////////////////////
//~ Generated Code

#include "generated/mac_window_manager.meta.h"

////////////////////////////////
//~ Globals

global MAC_WM_State *mac_wm_state = 0;

////////////////////////////////
//~ Helpers

internal MAC_WM_Window *mac_wm_window_from_handle(WM_Window handle);
internal WM_Window mac_wm_handle_from_window(MAC_WM_Window *window);
internal WM_Event *mac_wm_push_event(WM_EventKind kind, MAC_WM_Window *window, U64 modifier_flags);
internal WM_Modifiers mac_wm_modifiers_from_flags(U64 flags);
internal WM_Key mac_wm_key_from_keycode(U16 keycode, B32 *out_right_sided);
internal void mac_wm_set_key_down(WM_Key key, B32 down);
internal int mac_wm_hit_from_pos(MAC_WM_Window *window, Vec2F32 pos);

////////////////////////////////
//~ Cocoa Bridge Callbacks

internal void mac_wm_cocoa_push_event(MAC_CocoaEvent *event);
internal int  mac_wm_cocoa_hit_test(void *user_window, float x, float y);
internal void mac_wm_cocoa_modal_update(void);

#endif // MAC_WINDOW_MANAGER_H
