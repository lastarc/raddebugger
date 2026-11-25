// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MACHO_UNWIND_H
#define MACHO_UNWIND_H

////////////////////////////////
//~ Module Unwind Info Type
//
// Mach-O images carry "compact unwind" info in __TEXT,__unwind_info, which
// covers most functions directly, & otherwise points into __TEXT,__eh_frame
// for DWARF CFI. Both ranges are in the target's address space.

typedef struct MACHO_UWND_ModuleUnwindInfo MACHO_UWND_ModuleUnwindInfo;
struct MACHO_UWND_ModuleUnwindInfo
{
  Rng1U64 unwind_info_vaddr_range;
  Rng1U64 eh_frame_vaddr_range;
  
  // NOTE: sorted function start addresses (from LC_FUNCTION_STARTS). compact
  // unwind entries are merged across adjacent functions w/ identical
  // encodings, so they don't reliably identify function entry points.
  U64 *function_starts;
  U64 function_starts_count;
};

////////////////////////////////
//~ Unwinding Abstraction Implementation

internal U64 *macho_uwnd_function_starts_from_data(Arena *arena, String8 data, U64 base_vaddr, U64 *count_out);
internal UWND_StepResult macho_uwnd_step(Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *regs, U64 *cfa_out);

#endif // MACHO_UNWIND_H
