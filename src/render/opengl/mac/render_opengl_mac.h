// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef RENDER_OPENGL_MAC_H
#define RENDER_OPENGL_MAC_H

////////////////////////////////
//~ OpenGL Includes
//
// NOTE: the backend loads the procedures in R_OGL_ProcedureXList into global
// function pointers of the same names; Darwin's gl3.h declares those names as
// functions, so rename its declarations out of the way while including it.

#define GL_SILENCE_DEPRECATION 1
#define glGenBuffers glGenBuffers__mac_static
#define glBindBuffer glBindBuffer__mac_static
#define glDeleteBuffers glDeleteBuffers__mac_static
#define glGenVertexArrays glGenVertexArrays__mac_static
#define glBindVertexArray glBindVertexArray__mac_static
#define glGenFramebuffers glGenFramebuffers__mac_static
#define glDeleteFramebuffers glDeleteFramebuffers__mac_static
#define glCreateProgram glCreateProgram__mac_static
#define glCreateShader glCreateShader__mac_static
#define glShaderSource glShaderSource__mac_static
#define glCompileShader glCompileShader__mac_static
#define glGetShaderiv glGetShaderiv__mac_static
#define glGetShaderInfoLog glGetShaderInfoLog__mac_static
#define glGetProgramiv glGetProgramiv__mac_static
#define glGetProgramInfoLog glGetProgramInfoLog__mac_static
#define glAttachShader glAttachShader__mac_static
#define glLinkProgram glLinkProgram__mac_static
#define glValidateProgram glValidateProgram__mac_static
#define glDeleteShader glDeleteShader__mac_static
#define glUseProgram glUseProgram__mac_static
#define glGetUniformLocation glGetUniformLocation__mac_static
#define glGetAttribLocation glGetAttribLocation__mac_static
#define glEnableVertexAttribArray glEnableVertexAttribArray__mac_static
#define glVertexAttribPointer glVertexAttribPointer__mac_static
#define glBufferData glBufferData__mac_static
#define glBufferSubData glBufferSubData__mac_static
#define glBlendFuncSeparate glBlendFuncSeparate__mac_static
#define glUniform1f glUniform1f__mac_static
#define glUniform2f glUniform2f__mac_static
#define glUniform3f glUniform3f__mac_static
#define glUniform4f glUniform4f__mac_static
#define glUniform4fv glUniform4fv__mac_static
#define glUniformMatrix3fv glUniformMatrix3fv__mac_static
#define glUniformMatrix4fv glUniformMatrix4fv__mac_static
#define glUniform1i glUniform1i__mac_static
#define glTexImage3D glTexImage3D__mac_static
#define glTexSubImage3D glTexSubImage3D__mac_static
#define glGenerateMipmap glGenerateMipmap__mac_static
#define glBindAttribLocation glBindAttribLocation__mac_static
#define glBindFragDataLocation glBindFragDataLocation__mac_static
#define glBindFramebuffer glBindFramebuffer__mac_static
#define glActiveTexture glActiveTexture__mac_static
#define glVertexAttribDivisor glVertexAttribDivisor__mac_static
#define glDrawArraysInstanced glDrawArraysInstanced__mac_static
#define glDebugMessageCallback glDebugMessageCallback__mac_static
#define glFramebufferTexture2D glFramebufferTexture2D__mac_static
#define glBlitFramebuffer glBlitFramebuffer__mac_static
#pragma push_macro("internal")
#pragma push_macro("global")
#undef internal
#undef global
#include <OpenGL/gl3.h>
#include <dlfcn.h>
#pragma pop_macro("global")
#pragma pop_macro("internal")
#undef glGenBuffers
#undef glBindBuffer
#undef glDeleteBuffers
#undef glGenVertexArrays
#undef glBindVertexArray
#undef glGenFramebuffers
#undef glDeleteFramebuffers
#undef glCreateProgram
#undef glCreateShader
#undef glShaderSource
#undef glCompileShader
#undef glGetShaderiv
#undef glGetShaderInfoLog
#undef glGetProgramiv
#undef glGetProgramInfoLog
#undef glAttachShader
#undef glLinkProgram
#undef glValidateProgram
#undef glDeleteShader
#undef glUseProgram
#undef glGetUniformLocation
#undef glGetAttribLocation
#undef glEnableVertexAttribArray
#undef glVertexAttribPointer
#undef glBufferData
#undef glBufferSubData
#undef glBlendFuncSeparate
#undef glUniform1f
#undef glUniform2f
#undef glUniform3f
#undef glUniform4f
#undef glUniform4fv
#undef glUniformMatrix3fv
#undef glUniformMatrix4fv
#undef glUniform1i
#undef glTexImage3D
#undef glTexSubImage3D
#undef glGenerateMipmap
#undef glBindAttribLocation
#undef glBindFragDataLocation
#undef glBindFramebuffer
#undef glActiveTexture
#undef glVertexAttribDivisor
#undef glDrawArraysInstanced
#undef glDebugMessageCallback
#undef glFramebufferTexture2D
#undef glBlitFramebuffer

////////////////////////////////
//~ State

typedef struct R_OGL_MAC_Window R_OGL_MAC_Window;
struct R_OGL_MAC_Window
{
  R_OGL_MAC_Window *next;
  MAC_WM_Window *wm_window;
};

typedef struct R_OGL_MAC_State R_OGL_MAC_State;
struct R_OGL_MAC_State
{
  Arena *arena;
  void *opengl_library;
  R_OGL_MAC_Window *free_window;
};

global R_OGL_MAC_State *r_ogl_mac_state = 0;

#endif // RENDER_OPENGL_MAC_H
