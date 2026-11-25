// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Generated Code

#include "generated/mac_window_manager.meta.c"

////////////////////////////////
//~ Helpers

internal MAC_WM_Window *
mac_wm_window_from_handle(WM_Window handle)
{
  MAC_WM_Window *result = (MAC_WM_Window *)handle.u64[0];
  return result;
}

internal WM_Window
mac_wm_handle_from_window(MAC_WM_Window *window)
{
  WM_Window result = {(U64)window};
  return result;
}

internal WM_Event *
mac_wm_push_event(WM_EventKind kind, MAC_WM_Window *window, U64 modifier_flags)
{
  WM_Event *result = wm_event_list_push_new(mac_wm_state->pending_events_arena, &mac_wm_state->pending_events, kind);
  result->window = mac_wm_handle_from_window(window);
  result->modifiers = mac_wm_modifiers_from_flags(modifier_flags);
  result->timestamp_us = now_time_us();
  return result;
}

internal WM_Modifiers
mac_wm_modifiers_from_flags(U64 flags)
{
  // NOTE: Command maps to Ctrl, so the application's Ctrl-based bindings use
  // the platform's conventional shortcut key; Control also maps to Ctrl.
  WM_Modifiers modifiers = 0;
  if(flags & MAC_WM_ModifierFlag_Shift)                                 { modifiers |= WM_Modifier_Shift; }
  if(flags & (MAC_WM_ModifierFlag_Control|MAC_WM_ModifierFlag_Command)) { modifiers |= WM_Modifier_Ctrl; }
  if(flags & MAC_WM_ModifierFlag_Option)                                { modifiers |= WM_Modifier_Alt; }
  return modifiers;
}

internal WM_Key
mac_wm_key_from_keycode(U16 keycode, B32 *out_right_sided)
{
  // NOTE: keycodes are Carbon's kVK_* hardware virtual keycodes
  WM_Key key = WM_Key_Null;
  B32 right_sided = 0;
  switch(keycode)
  {
    default:{}break;
    case 0x00:{key = WM_Key_A;}break;
    case 0x01:{key = WM_Key_S;}break;
    case 0x02:{key = WM_Key_D;}break;
    case 0x03:{key = WM_Key_F;}break;
    case 0x04:{key = WM_Key_H;}break;
    case 0x05:{key = WM_Key_G;}break;
    case 0x06:{key = WM_Key_Z;}break;
    case 0x07:{key = WM_Key_X;}break;
    case 0x08:{key = WM_Key_C;}break;
    case 0x09:{key = WM_Key_V;}break;
    case 0x0B:{key = WM_Key_B;}break;
    case 0x0C:{key = WM_Key_Q;}break;
    case 0x0D:{key = WM_Key_W;}break;
    case 0x0E:{key = WM_Key_E;}break;
    case 0x0F:{key = WM_Key_R;}break;
    case 0x10:{key = WM_Key_Y;}break;
    case 0x11:{key = WM_Key_T;}break;
    case 0x12:{key = WM_Key_1;}break;
    case 0x13:{key = WM_Key_2;}break;
    case 0x14:{key = WM_Key_3;}break;
    case 0x15:{key = WM_Key_4;}break;
    case 0x16:{key = WM_Key_6;}break;
    case 0x17:{key = WM_Key_5;}break;
    case 0x18:{key = WM_Key_Equal;}break;
    case 0x19:{key = WM_Key_9;}break;
    case 0x1A:{key = WM_Key_7;}break;
    case 0x1B:{key = WM_Key_Minus;}break;
    case 0x1C:{key = WM_Key_8;}break;
    case 0x1D:{key = WM_Key_0;}break;
    case 0x1E:{key = WM_Key_RightBracket;}break;
    case 0x1F:{key = WM_Key_O;}break;
    case 0x20:{key = WM_Key_U;}break;
    case 0x21:{key = WM_Key_LeftBracket;}break;
    case 0x22:{key = WM_Key_I;}break;
    case 0x23:{key = WM_Key_P;}break;
    case 0x24:{key = WM_Key_Return;}break;
    case 0x25:{key = WM_Key_L;}break;
    case 0x26:{key = WM_Key_J;}break;
    case 0x27:{key = WM_Key_Quote;}break;
    case 0x28:{key = WM_Key_K;}break;
    case 0x29:{key = WM_Key_Semicolon;}break;
    case 0x2A:{key = WM_Key_BackSlash;}break;
    case 0x2B:{key = WM_Key_Comma;}break;
    case 0x2C:{key = WM_Key_Slash;}break;
    case 0x2D:{key = WM_Key_N;}break;
    case 0x2E:{key = WM_Key_M;}break;
    case 0x2F:{key = WM_Key_Period;}break;
    case 0x30:{key = WM_Key_Tab;}break;
    case 0x31:{key = WM_Key_Space;}break;
    case 0x32:{key = WM_Key_Tick;}break;
    case 0x33:{key = WM_Key_Backspace;}break;
    case 0x35:{key = WM_Key_Esc;}break;
    case 0x36:{key = WM_Key_Ctrl; right_sided = 1;}break; // right command
    case 0x37:{key = WM_Key_Ctrl;}break;                  // left command
    case 0x38:{key = WM_Key_Shift;}break;
    case 0x39:{key = WM_Key_CapsLock;}break;
    case 0x3A:{key = WM_Key_Alt;}break;
    case 0x3B:{key = WM_Key_Ctrl;}break;
    case 0x3C:{key = WM_Key_Shift; right_sided = 1;}break;
    case 0x3D:{key = WM_Key_Alt; right_sided = 1;}break;
    case 0x3E:{key = WM_Key_Ctrl; right_sided = 1;}break;
    case 0x40:{key = WM_Key_F17;}break;
    case 0x41:{key = WM_Key_NumPeriod;}break;
    case 0x43:{key = WM_Key_NumStar;}break;
    case 0x45:{key = WM_Key_NumPlus;}break;
    case 0x47:{key = WM_Key_NumLock;}break;   // keypad "clear" sits where num lock does
    case 0x4B:{key = WM_Key_NumSlash;}break;
    case 0x4C:{key = WM_Key_Return;}break;    // keypad enter
    case 0x4E:{key = WM_Key_NumMinus;}break;
    case 0x4F:{key = WM_Key_F18;}break;
    case 0x50:{key = WM_Key_F19;}break;
    case 0x51:{key = WM_Key_Equal;}break;     // keypad equals
    case 0x52:{key = WM_Key_Num0;}break;
    case 0x53:{key = WM_Key_Num1;}break;
    case 0x54:{key = WM_Key_Num2;}break;
    case 0x55:{key = WM_Key_Num3;}break;
    case 0x56:{key = WM_Key_Num4;}break;
    case 0x57:{key = WM_Key_Num5;}break;
    case 0x58:{key = WM_Key_Num6;}break;
    case 0x59:{key = WM_Key_Num7;}break;
    case 0x5A:{key = WM_Key_F20;}break;
    case 0x5B:{key = WM_Key_Num8;}break;
    case 0x5C:{key = WM_Key_Num9;}break;
    case 0x60:{key = WM_Key_F5;}break;
    case 0x61:{key = WM_Key_F6;}break;
    case 0x62:{key = WM_Key_F7;}break;
    case 0x63:{key = WM_Key_F3;}break;
    case 0x64:{key = WM_Key_F8;}break;
    case 0x65:{key = WM_Key_F9;}break;
    case 0x67:{key = WM_Key_F11;}break;
    case 0x69:{key = WM_Key_F13;}break;
    case 0x6A:{key = WM_Key_F16;}break;
    case 0x6B:{key = WM_Key_F14;}break;
    case 0x6D:{key = WM_Key_F10;}break;
    case 0x6F:{key = WM_Key_F12;}break;
    case 0x71:{key = WM_Key_F15;}break;
    case 0x72:{key = WM_Key_Insert;}break;    // "help" sits where insert does
    case 0x73:{key = WM_Key_Home;}break;
    case 0x74:{key = WM_Key_PageUp;}break;
    case 0x75:{key = WM_Key_Delete;}break;
    case 0x76:{key = WM_Key_F4;}break;
    case 0x77:{key = WM_Key_End;}break;
    case 0x78:{key = WM_Key_F2;}break;
    case 0x79:{key = WM_Key_PageDown;}break;
    case 0x7A:{key = WM_Key_F1;}break;
    case 0x7B:{key = WM_Key_Left;}break;
    case 0x7C:{key = WM_Key_Right;}break;
    case 0x7D:{key = WM_Key_Down;}break;
    case 0x7E:{key = WM_Key_Up;}break;
  }
  if(out_right_sided != 0)
  {
    out_right_sided[0] = right_sided;
  }
  return key;
}

internal WM_Key
mac_wm_key_from_cocoa_event(MAC_CocoaEvent *event, B32 *out_right_sided)
{
  WM_Key key = mac_wm_key_from_keycode(event->keycode, out_right_sided);

  // NOTE: prefer the layout's letter for letter keys, so shortcuts follow the
  // user's keyboard layout (as with Windows virtual keys & X11 keysyms).
  if('A' <= event->layout_letter && event->layout_letter <= 'Z')
  {
    local_persist WM_Key letter_keys[26] =
    {
      WM_Key_A, WM_Key_B, WM_Key_C, WM_Key_D, WM_Key_E, WM_Key_F, WM_Key_G, WM_Key_H, WM_Key_I,
      WM_Key_J, WM_Key_K, WM_Key_L, WM_Key_M, WM_Key_N, WM_Key_O, WM_Key_P, WM_Key_Q, WM_Key_R,
      WM_Key_S, WM_Key_T, WM_Key_U, WM_Key_V, WM_Key_W, WM_Key_X, WM_Key_Y, WM_Key_Z,
    };
    key = letter_keys[event->layout_letter - 'A'];
  }
  return key;
}

internal void
mac_wm_set_key_down(WM_Key key, B32 down)
{
  if(key <= WM_Key_Null || key >= WM_Key_COUNT) { return; }
  if(down)
  {
    mac_wm_state->key_down_state[key/64] |= (1ull<<(key%64));
  }
  else
  {
    mac_wm_state->key_down_state[key/64] &= ~(1ull<<(key%64));
  }
}

internal int
mac_wm_hit_from_pos(MAC_WM_Window *window, Vec2F32 pos)
{
  int hit = MAC_CocoaHit_Client;
  if(window->custom_border)
  {
    Rng2F32 rect = wm_client_rect_from_window(mac_wm_handle_from_window(window));
    Rng2F32 inside = pad_2f32(rect, -window->custom_edge_thickness);
    Rng2F32 inside_and_title_bar = inside;
    inside_and_title_bar.y0 += window->custom_title_bar_thickness;
    B32 in_non_client_area = 0;
    if(!contains_2f32(inside_and_title_bar, pos))
    {
      in_non_client_area = 1;
      for(MAC_WM_WindowClientArea *n = window->first_client_area; n != 0; n = n->next)
      {
        if(contains_2f32(n->rect, pos))
        {
          in_non_client_area = 0;
          break;
        }
      }
    }
    if(in_non_client_area)
    {
      Rng2F32 title_bar = r2f32p(inside.x0, inside.y0, inside.x1, inside.y0 + window->custom_title_bar_thickness);
      if(contains_2f32(title_bar, pos))
      {
        hit = MAC_CocoaHit_Caption;
      }
      else if(contains_2f32(rect, pos) && !contains_2f32(inside, pos))
      {
        if(pos.x <= inside.x0) { hit |= MAC_CocoaHit_EdgeLeft; }
        if(pos.x >= inside.x1) { hit |= MAC_CocoaHit_EdgeRight; }
        if(pos.y <= inside.y0) { hit |= MAC_CocoaHit_EdgeTop; }
        if(pos.y >= inside.y1) { hit |= MAC_CocoaHit_EdgeBottom; }
      }
    }
  }
  return hit;
}

internal MAC_CocoaCursor
mac_wm_cocoa_cursor_from_cursor(WM_Cursor cursor)
{
  MAC_CocoaCursor result = MAC_CocoaCursor_Arrow;
  switch(cursor)
  {
    default:{}break;
    case WM_Cursor_Pointer:         {result = MAC_CocoaCursor_Arrow;}break;
    case WM_Cursor_IBar:            {result = MAC_CocoaCursor_IBeam;}break;
    case WM_Cursor_LeftRight:       {result = MAC_CocoaCursor_ResizeLeftRight;}break;
    case WM_Cursor_UpDown:          {result = MAC_CocoaCursor_ResizeUpDown;}break;
    case WM_Cursor_DownRight:       {result = MAC_CocoaCursor_ResizeDiagonalDownRight;}break;
    case WM_Cursor_UpRight:         {result = MAC_CocoaCursor_ResizeDiagonalUpRight;}break;
    case WM_Cursor_UpDownLeftRight: {result = MAC_CocoaCursor_OpenHand;}break;
    case WM_Cursor_HandPoint:       {result = MAC_CocoaCursor_PointingHand;}break;
    case WM_Cursor_Disabled:        {result = MAC_CocoaCursor_NotAllowed;}break;
  }
  return result;
}

////////////////////////////////
//~ Cocoa Bridge Callbacks

internal void
mac_wm_cocoa_push_event(MAC_CocoaEvent *event)
{
  MAC_WM_Window *window = (MAC_WM_Window *)event->user_window;
  switch(event->kind)
  {
    default:{}break;

    //- keys
    case MAC_CocoaEventKind_KeyPress:
    case MAC_CocoaEventKind_KeyRelease:
    {
      B32 is_press = (event->kind == MAC_CocoaEventKind_KeyPress);
      B32 right_sided = 0;
      WM_Key key = mac_wm_key_from_cocoa_event(event, &right_sided);
      WM_Event *e = mac_wm_push_event(is_press ? WM_EventKind_Press : WM_EventKind_Release, window, event->modifier_flags);
      e->key = key;
      e->is_repeat = !!event->is_repeat;
      e->repeat_count = 1;
      e->right_sided = right_sided;
      mac_wm_set_key_down(key, is_press);
    }break;

    //- modifier keys
    case MAC_CocoaEventKind_ModifierChange:
    {
      U64 flags = event->modifier_flags;
      WM_Key key = WM_Key_Null;
      B32 right_sided = 0;
      B32 is_down = 0;
      switch(event->keycode)
      {
        default:{}break;
        case 0x38:{key = WM_Key_Shift;    is_down = !!(flags & MAC_WM_DeviceFlag_LShift);}break;
        case 0x3C:{key = WM_Key_Shift;    is_down = !!(flags & MAC_WM_DeviceFlag_RShift); right_sided = 1;}break;
        case 0x3B:{key = WM_Key_Ctrl;     is_down = !!(flags & MAC_WM_DeviceFlag_LCtl);}break;
        case 0x3E:{key = WM_Key_Ctrl;     is_down = !!(flags & MAC_WM_DeviceFlag_RCtl);   right_sided = 1;}break;
        case 0x37:{key = WM_Key_Ctrl;     is_down = !!(flags & MAC_WM_DeviceFlag_LCmd);}break;
        case 0x36:{key = WM_Key_Ctrl;     is_down = !!(flags & MAC_WM_DeviceFlag_RCmd);   right_sided = 1;}break;
        case 0x3A:{key = WM_Key_Alt;      is_down = !!(flags & MAC_WM_DeviceFlag_LAlt);}break;
        case 0x3D:{key = WM_Key_Alt;      is_down = !!(flags & MAC_WM_DeviceFlag_RAlt);   right_sided = 1;}break;
        case 0x39:{key = WM_Key_CapsLock; is_down = !!(flags & MAC_WM_ModifierFlag_CapsLock);}break;
      }
      if(key != WM_Key_Null)
      {
        WM_Event *e = mac_wm_push_event(is_down ? WM_EventKind_Press : WM_EventKind_Release, window, flags);
        e->key = key;
        e->right_sided = right_sided;
        if(key == WM_Key_Alt   && e->modifiers & WM_Modifier_Alt)   { e->modifiers &= ~WM_Modifier_Alt; }
        if(key == WM_Key_Ctrl  && e->modifiers & WM_Modifier_Ctrl)  { e->modifiers &= ~WM_Modifier_Ctrl; }
        if(key == WM_Key_Shift && e->modifiers & WM_Modifier_Shift) { e->modifiers &= ~WM_Modifier_Shift; }
        mac_wm_set_key_down(key, is_down);
      }
    }break;

    //- mouse buttons
    case MAC_CocoaEventKind_MousePress:
    case MAC_CocoaEventKind_MouseRelease:
    {
      WM_Key key = WM_Key_Null;
      switch(event->mouse_button)
      {
        default:{}break;
        case 0:{key = WM_Key_LeftMouseButton;}break;
        case 1:{key = WM_Key_RightMouseButton;}break;
        case 2:{key = WM_Key_MiddleMouseButton;}break;
      }
      if(key != WM_Key_Null)
      {
        B32 is_press = (event->kind == MAC_CocoaEventKind_MousePress);
        WM_Event *e = mac_wm_push_event(is_press ? WM_EventKind_Press : WM_EventKind_Release, window, event->modifier_flags);
        e->key = key;
        e->pos = v2f32(event->x, event->y);
        mac_wm_set_key_down(key, is_press);
      }
    }break;

    //- mouse movement
    case MAC_CocoaEventKind_MouseMove:
    {
      WM_Event *e = mac_wm_push_event(WM_EventKind_MouseMove, window, event->modifier_flags);
      e->pos = v2f32(event->x, event->y);

      // custom border: show resize cursors over the window edges
      int hit = mac_wm_hit_from_pos(window, e->pos);
      B32 l = !!(hit & MAC_CocoaHit_EdgeLeft), r = !!(hit & MAC_CocoaHit_EdgeRight);
      B32 t = !!(hit & MAC_CocoaHit_EdgeTop),  b = !!(hit & MAC_CocoaHit_EdgeBottom);
      if(l || r || t || b)
      {
        WM_Cursor cursor = WM_Cursor_Pointer;
        if((l && t) || (r && b))      { cursor = WM_Cursor_DownRight; }
        else if((r && t) || (l && b)) { cursor = WM_Cursor_UpRight; }
        else if(l || r)               { cursor = WM_Cursor_LeftRight; }
        else                          { cursor = WM_Cursor_UpDown; }
        wm_set_cursor(cursor);
      }
    }break;

    //- scrolling
    case MAC_CocoaEventKind_Scroll:
    {
      WM_Event *e = mac_wm_push_event(WM_EventKind_Scroll, window, event->modifier_flags);
      e->pos = v2f32(event->x, event->y);
      e->delta = v2f32(event->dx, event->dy);
    }break;

    //- text
    case MAC_CocoaEventKind_Text:
    {
      WM_Event *e = mac_wm_push_event(WM_EventKind_Text, window, event->modifier_flags);
      e->character = event->codepoint;
    }break;

    //- focus
    case MAC_CocoaEventKind_LoseFocus:
    {
      mac_wm_push_event(WM_EventKind_WindowLoseFocus, window, event->modifier_flags);
      MemoryZeroArray(mac_wm_state->key_down_state);
    }break;

    //- closing
    case MAC_CocoaEventKind_Close:
    {
      mac_wm_push_event(WM_EventKind_WindowClose, window, event->modifier_flags);
    }break;
    case MAC_CocoaEventKind_QuitRequest:
    {
      for(MAC_WM_Window *w = mac_wm_state->first_window; w != 0; w = w->next)
      {
        mac_wm_push_event(WM_EventKind_WindowClose, w, event->modifier_flags);
      }
    }break;

    //- file drops
    case MAC_CocoaEventKind_FileDrop:
    {
      WM_Event *e = mac_wm_push_event(WM_EventKind_FileDrop, window, event->modifier_flags);
      e->pos = v2f32(event->x, event->y);
      for EachIndex(idx, event->path_count)
      {
        String8 path = str8_copy(mac_wm_state->pending_events_arena, str8_cstring(event->paths[idx]));
        str8_list_push(mac_wm_state->pending_events_arena, &e->strings, path);
      }
    }break;
  }
}

internal int
mac_wm_cocoa_hit_test(void *user_window, float x, float y)
{
  MAC_WM_Window *window = (MAC_WM_Window *)user_window;
  int result = mac_wm_hit_from_pos(window, v2f32(x, y));
  return result;
}

internal void
mac_wm_cocoa_modal_update(void)
{
  update();
}

////////////////////////////////
//~ @per_os_impl Main Initialization API

internal void
wm_init(void)
{
  Arena *arena = arena_alloc();
  mac_wm_state = push_array(arena, MAC_WM_State, 1);
  mac_wm_state->arena = arena;
  mac_wm_state->pending_events_arena = arena_alloc();

  //- init cocoa
  MAC_CocoaCallbacks callbacks = {0};
  callbacks.push_event   = mac_wm_cocoa_push_event;
  callbacks.hit_test     = mac_wm_cocoa_hit_test;
  callbacks.modal_update = mac_wm_cocoa_modal_update;
  mac_cocoa_init(&callbacks, mac_wm_icon_file_bytes.str, mac_wm_icon_file_bytes.size);

  //- fill out gfx info
  mac_wm_state->gfx_info.double_click_time = (F32)mac_cocoa_double_click_interval();
  mac_wm_state->gfx_info.caret_blink_time = 0.5f;
  mac_wm_state->gfx_info.default_refresh_rate = (F32)mac_cocoa_main_display_refresh_rate();
  if(mac_wm_state->gfx_info.double_click_time <= 0)
  {
    mac_wm_state->gfx_info.double_click_time = 0.5f;
  }
  if(mac_wm_state->gfx_info.default_refresh_rate <= 0)
  {
    mac_wm_state->gfx_info.default_refresh_rate = 60.f;
  }
}

////////////////////////////////
//~ @per_os_impl Graphics System Info

internal WM_SystemInfo *
wm_get_system_info(void)
{
  return &mac_wm_state->gfx_info;
}

////////////////////////////////
//~ @per_os_impl Clipboards

internal void
wm_set_clipboard_text(String8 string)
{
  mac_cocoa_set_clipboard_text(string.str, string.size);
}

internal String8
wm_get_clipboard_text(Arena *arena)
{
  U64 size = 0;
  char *text = mac_cocoa_get_clipboard_text_malloc(&size);
  String8 result = {0};
  if(text != 0)
  {
    result = str8_copy(arena, str8((U8 *)text, size));
    free(text);
  }
  return result;
}

////////////////////////////////
//~ @per_os_impl Windows

internal WM_Window
wm_window_open(Rng2F32 rect, WM_WindowFlags flags, String8 title)
{
  MAC_WM_Window *w = mac_wm_state->free_window;
  if(w != 0)
  {
    SLLStackPop(mac_wm_state->free_window);
  }
  else
  {
    w = push_array_no_zero(mac_wm_state->arena, MAC_WM_Window, 1);
  }
  MemoryZeroStruct(w);
  DLLPushBack(mac_wm_state->first_window, mac_wm_state->last_window, w);

  // NOTE: window rects are in points, with a top-left origin relative to the
  // primary display; client rects & mouse positions are in pixels.
  Vec2F32 size = dim_2f32(rect);
  w->custom_border = !!(flags & WM_WindowFlag_CustomBorder);
  w->cocoa = mac_cocoa_window_open(w, rect.x0, rect.y0, size.x, size.y,
                                   w->custom_border,
                                   !!(flags & WM_WindowFlag_UseDefaultPosition),
                                   title.str, title.size);
  WM_Window handle = mac_wm_handle_from_window(w);
  return handle;
}

internal void
wm_window_close(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  mac_cocoa_window_close(w->cocoa);
  w->cocoa = 0;
  wm_window_clear_custom_border_data(handle);
  DLLRemove(mac_wm_state->first_window, mac_wm_state->last_window, w);

  // NOTE: drop any pending events which refer to this window
  for(WM_Event *e = mac_wm_state->pending_events.first, *next = 0; e != 0; e = next)
  {
    next = e->next;
    if(e->window.u64[0] == (U64)w)
    {
      DLLRemove(mac_wm_state->pending_events.first, mac_wm_state->pending_events.last, e);
      mac_wm_state->pending_events.count -= 1;
    }
  }
  SLLStackPush(mac_wm_state->free_window, w);
}

internal void
wm_window_set_title(WM_Window handle, String8 title)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  mac_cocoa_window_set_title(w->cocoa, title.str, title.size);
}

internal void
wm_window_first_paint(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  mac_cocoa_window_show(w->cocoa);
}

internal void
wm_window_focus(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  mac_cocoa_window_focus(w->cocoa);
}

internal B32
wm_window_is_focused(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return 0; }
  return !!mac_cocoa_window_is_focused(w->cocoa);
}

internal B32
wm_window_is_fullscreen(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return 0; }
  return !!mac_cocoa_window_is_fullscreen(w->cocoa);
}

internal void
wm_window_set_fullscreen(WM_Window handle, B32 fullscreen)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  mac_cocoa_window_set_fullscreen(w->cocoa, !!fullscreen);
}

internal B32
wm_window_is_maximized(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return 0; }
  return !!mac_cocoa_window_is_maximized(w->cocoa);
}

internal void
wm_window_set_maximized(WM_Window handle, B32 maximized)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  mac_cocoa_window_set_maximized(w->cocoa, !!maximized);
}

internal B32
wm_window_is_minimized(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return 0; }
  return !!mac_cocoa_window_is_minimized(w->cocoa);
}

internal void
wm_window_set_minimized(WM_Window handle, B32 minimized)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  mac_cocoa_window_set_minimized(w->cocoa, !!minimized);
}

internal void
wm_window_bring_to_front(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  mac_cocoa_window_bring_to_front(w->cocoa);
}

internal void
wm_window_set_monitor(WM_Window handle, WM_Monitor monitor)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0 || monitor.u64[0] == 0) { return; }
  mac_cocoa_window_move_to_display(w->cocoa, (U32)monitor.u64[0]);
}

internal void
wm_window_clear_custom_border_data(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  for(MAC_WM_WindowClientArea *n = w->first_client_area, *next = 0; n != 0; n = next)
  {
    next = n->next;
    SLLStackPush(mac_wm_state->free_client_area, n);
  }
  w->first_client_area = w->last_client_area = 0;
}

internal void
wm_window_push_custom_title_bar(WM_Window handle, F32 thickness)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  w->custom_title_bar_thickness = thickness;
}

internal void
wm_window_push_custom_edges(WM_Window handle, F32 thickness)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  w->custom_edge_thickness = thickness;
}

internal void
wm_window_push_custom_title_bar_client_area(WM_Window handle, Rng2F32 rect)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  if(w == 0) { return; }
  MAC_WM_WindowClientArea *n = mac_wm_state->free_client_area;
  if(n != 0)
  {
    SLLStackPop(mac_wm_state->free_client_area);
  }
  else
  {
    n = push_array(mac_wm_state->arena, MAC_WM_WindowClientArea, 1);
  }
  n->next = 0;
  n->rect = rect;
  SLLQueuePush(w->first_client_area, w->last_client_area, n);
}

internal Rng2F32
wm_rect_from_window(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  Rng2F32 result = {0};
  if(w != 0)
  {
    F64 x = 0, y = 0, width = 0, height = 0;
    mac_cocoa_window_frame(w->cocoa, &x, &y, &width, &height);
    result = r2f32p((F32)x, (F32)y, (F32)(x + width), (F32)(y + height));
  }
  return result;
}

internal Rng2F32
wm_client_rect_from_window(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  Rng2F32 result = {0};
  if(w != 0)
  {
    F64 width = 0, height = 0;
    mac_cocoa_window_client_size_pixels(w->cocoa, &width, &height);
    result = r2f32p(0, 0, (F32)width, (F32)height);
  }
  return result;
}

internal F32
wm_dpi_from_window(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  F32 result = 96.f;
  if(w != 0)
  {
    result = 96.f * (F32)mac_cocoa_window_scale(w->cocoa);
  }
  return result;
}

////////////////////////////////
//~ @per_os_impl External Windows

internal WM_ExtWindow
wm_focused_external_window(void)
{
  WM_ExtWindow result = {0};
  S32 pid = mac_cocoa_frontmost_application_pid();
  if(pid > 0 && (U32)pid != get_process_info()->pid)
  {
    result.u64[0] = (U64)pid;
  }
  return result;
}

internal void
wm_focus_external_window(WM_ExtWindow handle)
{
  if(handle.u64[0] != 0)
  {
    mac_cocoa_activate_application_pid((S32)handle.u64[0]);
  }
}

////////////////////////////////
//~ @per_os_impl Monitors

internal WM_MonitorArray
wm_push_monitors_array(Arena *arena)
{
  WM_MonitorArray result = {0};
  result.count = mac_cocoa_display_count();
  result.v = push_array(arena, WM_Monitor, result.count);
  for EachIndex(idx, result.count)
  {
    result.v[idx].u64[0] = (U64)mac_cocoa_display_id_from_index(idx);
  }
  return result;
}

internal WM_Monitor
wm_primary_monitor(void)
{
  WM_Monitor result = {0};
  result.u64[0] = (U64)mac_cocoa_display_id_from_index(0);
  return result;
}

internal WM_Monitor
wm_monitor_from_window(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  WM_Monitor result = {0};
  if(w != 0)
  {
    result.u64[0] = (U64)mac_cocoa_window_display_id(w->cocoa);
  }
  return result;
}

internal String8
wm_name_from_monitor(Arena *arena, WM_Monitor monitor)
{
  String8 result = {0};
  char buffer[512] = {0};
  if(monitor.u64[0] != 0 && mac_cocoa_display_name((U32)monitor.u64[0], buffer, sizeof(buffer)))
  {
    result = str8_copy(arena, str8_cstring(buffer));
  }
  return result;
}

internal Vec2F32
wm_dim_from_monitor(WM_Monitor monitor)
{
  Vec2F32 result = {0};
  F64 width = 0, height = 0;
  if(monitor.u64[0] != 0 && mac_cocoa_display_size((U32)monitor.u64[0], &width, &height))
  {
    result = v2f32((F32)width, (F32)height);
  }
  return result;
}

internal F32
wm_dpi_from_monitor(WM_Monitor monitor)
{
  F32 result = 96.f;
  if(monitor.u64[0] != 0)
  {
    result = 96.f * (F32)mac_cocoa_display_scale((U32)monitor.u64[0]);
  }
  return result;
}

////////////////////////////////
//~ @per_os_impl Events

internal void
wm_send_wakeup_event(void)
{
  mac_cocoa_post_wakeup();
}

internal WM_EventList
wm_get_events(Arena *arena, B32 wait)
{
  for(;;)
  {
    B32 block = (wait && mac_wm_state->pending_events.count == 0);
    B32 woken = mac_cocoa_pump_events(block);
    if(!wait || woken || mac_wm_state->pending_events.count != 0)
    {
      break;
    }
  }
  WM_EventList result = wm_event_list_copy(arena, &mac_wm_state->pending_events);
  MemoryZeroStruct(&mac_wm_state->pending_events);
  arena_clear(mac_wm_state->pending_events_arena);
  return result;
}

internal WM_Modifiers
wm_get_modifiers(void)
{
  WM_Modifiers modifiers = mac_wm_modifiers_from_flags(mac_cocoa_current_modifier_flags());
  return modifiers;
}

internal B32
wm_key_is_down(WM_Key key)
{
  B32 result = 0;
  U64 flags = mac_cocoa_current_modifier_flags();
  U64 buttons = mac_cocoa_pressed_mouse_buttons();
  switch(key)
  {
    default:
    {
      if(WM_Key_Null < key && key < WM_Key_COUNT)
      {
        result = !!(mac_wm_state->key_down_state[key/64] & (1ull<<(key%64)));
      }
    }break;
    case WM_Key_Shift:             {result = !!(flags & MAC_WM_ModifierFlag_Shift);}break;
    case WM_Key_Ctrl:              {result = !!(flags & (MAC_WM_ModifierFlag_Control|MAC_WM_ModifierFlag_Command));}break;
    case WM_Key_Alt:               {result = !!(flags & MAC_WM_ModifierFlag_Option);}break;
    case WM_Key_LeftMouseButton:   {result = !!(buttons & (1<<0));}break;
    case WM_Key_RightMouseButton:  {result = !!(buttons & (1<<1));}break;
    case WM_Key_MiddleMouseButton: {result = !!(buttons & (1<<2));}break;
  }
  return result;
}

internal Vec2F32
wm_mouse_from_window(WM_Window handle)
{
  MAC_WM_Window *w = mac_wm_window_from_handle(handle);
  Vec2F32 result = {0};
  if(w != 0)
  {
    mac_cocoa_window_mouse_pixels(w->cocoa, &result.x, &result.y);
  }
  return result;
}

////////////////////////////////
//~ @per_os_impl Cursors

internal void
wm_set_cursor(WM_Cursor cursor)
{
  // NOTE: Cocoa may reset the cursor (e.g. when crossing window edges), so
  // it's re-applied on every call, rather than only on changes.
  mac_wm_state->last_set_cursor = cursor;
  mac_cocoa_set_cursor(mac_wm_cocoa_cursor_from_cursor(cursor));
}
