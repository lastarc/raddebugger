// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal DW_SectionKind
dw_section_kind_from_macho_section_name(String8 sectname)
{
  // NOTE: Mach-O section names are limited to 16 characters, so the one DWARF
  // section name which is longer is stored truncated.
  DW_SectionKind result = DW_SectionKind_Null;
  if(str8_match(sectname, str8_lit("__debug_str_offs"), 0))
  {
    result = DW_SectionKind_StrOffsets;
  }
  else
  {
    result = dw_section_kind_from_string(sectname);
  }
  return result;
}

internal DW_Raw
dw_raw_from_macho_bin(Arena *arena, MACHO_Bin *bin)
{
  DW_Raw result = {0};
  B32 is_section_present[ArrayCount(result.sec)] = {0};
  for EachIndex(idx, bin->section_count)
  {
    MACHO_SectionInfo *sect = &bin->sections[idx];
    if(!str8_match(sect->segname, str8_lit("__DWARF"), 0)) { continue; }
    DW_SectionKind section_kind = dw_section_kind_from_macho_section_name(sect->sectname);
    if(section_kind == DW_SectionKind_Null) { continue; }
    if(is_section_present[section_kind])    { continue; }
    is_section_present[section_kind] = 1;
    DW_Section *d = &result.sec[section_kind];
    d->name = push_str8_copy(arena, dw_regular_name_from_section_kind_table[section_kind]);
    d->data = str8_substr(bin->data, r1u64(sect->foff, sect->foff + sect->size));
  }
  return result;
}
