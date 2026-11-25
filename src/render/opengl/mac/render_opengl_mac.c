// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ OS Hooks
//
// NOTE: context management lives in the Cocoa bridge (mac/cocoa/mac_cocoa.m),
// which shares a single NSOpenGLContext between all windows.

internal VoidProc *
r_ogl_os_load_procedure(char *name)
{
  VoidProc *result = 0;
  if(r_ogl_mac_state != 0 && r_ogl_mac_state->opengl_library != 0)
  {
    result = (VoidProc *)dlsym(r_ogl_mac_state->opengl_library, name);
  }
  return result;
}

internal void
r_ogl_os_init(CmdLine *cmdln)
{
  Arena *arena = arena_alloc();
  r_ogl_mac_state = push_array(arena, R_OGL_MAC_State, 1);
  r_ogl_mac_state->arena = arena;
  r_ogl_mac_state->opengl_library = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY|RTLD_LOCAL);
  if(r_ogl_mac_state->opengl_library == 0)
  {
    sh_message(1, str8_lit("Fatal Error"), str8_lit("Couldn't load the OpenGL framework."));
    abort_self(1);
  }
  if(!mac_cocoa_gl_init())
  {
    sh_message(1, str8_lit("Fatal Error"), str8_lit("Couldn't create an OpenGL 3.2+ core profile context."));
    abort_self(1);
  }
}

internal R_Handle
r_ogl_os_window_equip(WM_Window window)
{
  R_OGL_MAC_Window *w = r_ogl_mac_state->free_window;
  if(w != 0)
  {
    SLLStackPop(r_ogl_mac_state->free_window);
  }
  else
  {
    w = push_array(r_ogl_mac_state->arena, R_OGL_MAC_Window, 1);
  }
  MemoryZeroStruct(w);
  w->wm_window = mac_wm_window_from_handle(window);
  R_Handle result = {(U64)w};
  return result;
}

internal void
r_ogl_os_window_unequip(WM_Window os, R_Handle r)
{
  R_OGL_MAC_Window *w = (R_OGL_MAC_Window *)r.u64[0];
  if(w != 0)
  {
    if(w->wm_window != 0 && w->wm_window->cocoa != 0)
    {
      mac_cocoa_gl_release_window(w->wm_window->cocoa);
    }
    SLLStackPush(r_ogl_mac_state->free_window, w);
  }
}

internal void
r_ogl_os_select_window(WM_Window os, R_Handle r)
{
  R_OGL_Window *outer = (R_OGL_Window *)r.u64[0];
  R_OGL_MAC_Window *w = (R_OGL_MAC_Window *)outer->os.u64[0];
  mac_cocoa_gl_select_window((w != 0 && w->wm_window != 0) ? w->wm_window->cocoa : 0);
}

internal void
r_ogl_os_window_swap(WM_Window os, R_Handle r)
{
  mac_cocoa_gl_swap();
}
