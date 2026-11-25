// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DWARF_PARSE_MACHO_H
#define DWARF_PARSE_MACHO_H

internal DW_SectionKind dw_section_kind_from_macho_section_name(String8 sectname);
internal DW_Raw dw_raw_from_macho_bin(Arena *arena, MACHO_Bin *bin);

#endif // DWARF_PARSE_MACHO_H
