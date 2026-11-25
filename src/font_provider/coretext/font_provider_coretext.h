// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef FONT_PROVIDER_CORETEXT_H
#define FONT_PROVIDER_CORETEXT_H

////////////////////////////////
//~ CoreText Includes

#pragma push_macro("internal")
#pragma push_macro("global")
#undef internal
#undef global
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#pragma pop_macro("global")
#pragma pop_macro("internal")
#undef nil // NOTE: MacTypes.h defines `nil`, which this codebase uses as an identifier

////////////////////////////////
//~ State Types

typedef struct FP_CT_State FP_CT_State;
struct FP_CT_State
{
  Arena *arena;
};

////////////////////////////////
//~ Globals

global FP_CT_State *fp_ct_state = 0;

////////////////////////////////
//~ Helpers

internal CGFontRef fp_ct_cgfont_from_handle(FP_Handle handle);
internal FP_Handle fp_ct_handle_from_cgfont(CGFontRef font);

#endif // FONT_PROVIDER_CORETEXT_H
