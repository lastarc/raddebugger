// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MACHO_PARSE_H
#define MACHO_PARSE_H

////////////////////////////////
//~ Parsed Mach-O Types

typedef struct MACHO_SegmentInfo MACHO_SegmentInfo;
struct MACHO_SegmentInfo
{
  String8 name;
  U64 vaddr;
  U64 vsize;
  U64 foff;
  U64 fsize;
  U32 initprot;
};

typedef struct MACHO_SectionInfo MACHO_SectionInfo;
struct MACHO_SectionInfo
{
  String8 segname;
  String8 sectname;
  U64 vaddr;
  U64 size;
  U64 foff;
  U32 flags;
  U32 seg_initprot;
};

typedef struct MACHO_Bin MACHO_Bin;
struct MACHO_Bin
{
  B32 is_valid;
  String8 data; // the selected (thin) image, from fat files
  U32 cputype;
  U32 filetype;
  MACHO_SegmentInfo *segments;
  U64 segment_count;
  MACHO_SectionInfo *sections;
  U64 section_count;
  B32 has_uuid;
  U8 uuid[16];
  B32 has_entry;
  U64 entry_off;
  B32 has_symtab;
  U64 symoff;
  U64 nsyms;
  U64 stroff;
  U64 strsize;
};

////////////////////////////////
//~ Parsing Functions

internal B32 macho_data_is_macho(String8 data);
internal String8 macho_thin_data_from_data(String8 data, U32 preferred_cputype);
internal MACHO_Bin macho_bin_from_data(Arena *arena, String8 data);
internal MACHO_SegmentInfo *macho_segment_from_name(MACHO_Bin *bin, String8 name);
internal MACHO_SectionInfo *macho_section_from_name(MACHO_Bin *bin, String8 segname, String8 sectname);
internal U64 macho_text_vaddr_from_bin(MACHO_Bin *bin);
internal U64 macho_image_vsize_from_bin(MACHO_Bin *bin);
internal B32 macho_has_dwarf(MACHO_Bin *bin);
internal B32 macho_has_debug_map(MACHO_Bin *bin);
internal Guid macho_guid_from_bin(MACHO_Bin *bin);

#endif // MACHO_PARSE_H
