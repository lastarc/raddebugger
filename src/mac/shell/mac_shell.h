// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_SHELL_H
#define MAC_SHELL_H

#pragma push_macro("internal")
#pragma push_macro("global")
#undef internal
#undef global
#include <CoreFoundation/CoreFoundation.h>
#pragma pop_macro("global")
#pragma pop_macro("internal")
#undef nil // NOTE: MacTypes.h defines `nil`, which this codebase uses as an identifier

////////////////////////////////
//~ Helpers

// runs a program directly (no shell interpretation of arguments), optionally
// capturing its stdout; returns 1 if the program ran & exited with status 0.
internal B32 mac_sh_run(Arena *arena, char **argv, String8 *stdout_out);
internal CFStringRef mac_sh_cfstring_from_str8(String8 string);

#endif // MAC_SHELL_H
