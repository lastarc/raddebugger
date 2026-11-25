// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Parsing Functions

internal B32
macho_data_is_macho(String8 data)
{
  U32 magic = 0;
  str8_deserial_read_struct(data, 0, &magic);
  B32 result = (magic == MACHO_MAGIC_64 ||
                magic == MACHO_MAGIC_32 ||
                from_be_u32(magic) == MACHO_FAT_MAGIC ||
                from_be_u32(magic) == MACHO_FAT_MAGIC_64);
  return result;
}

internal String8
macho_thin_data_from_data(String8 data, U32 preferred_cputype)
{
  String8 result = data;
  MACHO_FatHeader fat = {0};
  str8_deserial_read_struct(data, 0, &fat);
  U32 fat_magic = from_be_u32(fat.magic);
  if(fat_magic == MACHO_FAT_MAGIC || fat_magic == MACHO_FAT_MAGIC_64)
  {
    // NOTE: pick the preferred architecture's slice, falling back to the
    // first 64-bit one, then the first one.
    U32 nfat_arch = from_be_u32(fat.nfat_arch);
    nfat_arch = Min(nfat_arch, 64);
    Rng1U64 preferred = {0};
    Rng1U64 first_64bit = {0};
    Rng1U64 first = {0};
    U64 off = sizeof(fat);
    for(U32 idx = 0; idx < nfat_arch; idx += 1)
    {
      U32 cputype = 0;
      Rng1U64 range = {0};
      if(fat_magic == MACHO_FAT_MAGIC)
      {
        MACHO_FatArch arch = {0};
        if(str8_deserial_read_struct(data, off, &arch) != sizeof(arch)) { break; }
        off += sizeof(arch);
        cputype = from_be_u32(arch.cputype);
        range = r1u64(from_be_u32(arch.offset), (U64)from_be_u32(arch.offset) + from_be_u32(arch.size));
      }
      else
      {
        MACHO_FatArch64 arch = {0};
        if(str8_deserial_read_struct(data, off, &arch) != sizeof(arch)) { break; }
        off += sizeof(arch);
        cputype = from_be_u32(arch.cputype);
        U64 arch_off = from_be_u64(arch.offset);
        U64 arch_size = from_be_u64(arch.size);
        range = r1u64(arch_off, arch_off + arch_size);
      }
      if(range.max < range.min || range.max > data.size) { continue; }
      if(first.max == 0)                                      { first = range; }
      if(first_64bit.max == 0 && cputype & MACHO_CPU_ARCH_ABI64) { first_64bit = range; }
      if(preferred.max == 0 && cputype == preferred_cputype)  { preferred = range; }
    }
    Rng1U64 pick = preferred.max ? preferred : first_64bit.max ? first_64bit : first;
    result = str8_substr(data, pick);
  }
  return result;
}

internal MACHO_Bin
macho_bin_from_data(Arena *arena, String8 data)
{
  MACHO_Bin bin = {0};

  //- select thin image
  String8 thin = macho_thin_data_from_data(data, MACHO_CPU_TYPE_X86_64);

  //- read header (only 64-bit little-endian images are supported)
  MACHO_Header64 hdr = {0};
  if(str8_deserial_read_struct(thin, 0, &hdr) != sizeof(hdr) || hdr.magic != MACHO_MAGIC_64)
  {
    return bin;
  }
  bin.is_valid = 1;
  bin.data     = thin;
  bin.cputype  = hdr.cputype;
  bin.filetype = hdr.filetype;

  //- gather load commands
  U64 cmds_opl = Min(thin.size, sizeof(hdr) + (U64)hdr.sizeofcmds);
  U64 segment_count = 0;
  U64 section_count = 0;
  for(U64 pass = 0; pass < 2; pass += 1)
  {
    if(pass == 1)
    {
      bin.segments = push_array(arena, MACHO_SegmentInfo, segment_count);
      bin.sections = push_array(arena, MACHO_SectionInfo, section_count);
    }
    U64 off = sizeof(hdr);
    for(U32 cmd_idx = 0; cmd_idx < hdr.ncmds && off + sizeof(MACHO_LoadCommand) <= cmds_opl; cmd_idx += 1)
    {
      MACHO_LoadCommand lc = {0};
      str8_deserial_read_struct(thin, off, &lc);
      if(lc.cmdsize < sizeof(lc) || off + lc.cmdsize > cmds_opl)
      {
        break;
      }
      String8 lc_data = str8_substr(thin, r1u64(off, off + lc.cmdsize));
      switch(lc.cmd)
      {
        default:{}break;
        case MACHO_LoadCommandKind_Segment64:
        {
          MACHO_SegmentCommand64 seg = {0};
          if(str8_deserial_read_struct(lc_data, 0, &seg) != sizeof(seg)) { break; }
          U64 max_nsects = (lc_data.size - sizeof(seg)) / sizeof(MACHO_Section64);
          U64 nsects = Min((U64)seg.nsects, max_nsects);
          if(pass == 0)
          {
            segment_count += 1;
            section_count += nsects;
          }
          else
          {
            MACHO_SegmentInfo *dst = &bin.segments[bin.segment_count];
            bin.segment_count += 1;
            dst->name     = push_str8_copy(arena, str8_cstring_capped(seg.segname, seg.segname + sizeof(seg.segname)));
            dst->vaddr    = seg.vmaddr;
            dst->vsize    = seg.vmsize;
            dst->foff     = seg.fileoff;
            dst->fsize    = seg.filesize;
            dst->initprot = seg.initprot;
            for(U64 sect_idx = 0; sect_idx < nsects; sect_idx += 1)
            {
              MACHO_Section64 sect = {0};
              str8_deserial_read_struct(lc_data, sizeof(seg) + sect_idx*sizeof(MACHO_Section64), &sect);
              MACHO_SectionInfo *s = &bin.sections[bin.section_count];
              bin.section_count += 1;
              s->segname      = push_str8_copy(arena, str8_cstring_capped(sect.segname, sect.segname + sizeof(sect.segname)));
              s->sectname     = push_str8_copy(arena, str8_cstring_capped(sect.sectname, sect.sectname + sizeof(sect.sectname)));
              s->vaddr        = sect.addr;
              s->size         = sect.size;
              s->foff         = sect.offset;
              s->flags        = sect.flags;
              s->seg_initprot = seg.initprot;
            }
          }
        }break;
        case MACHO_LoadCommandKind_Symtab:
        if(pass == 1)
        {
          MACHO_SymtabCommand symtab = {0};
          if(str8_deserial_read_struct(lc_data, 0, &symtab) == sizeof(symtab))
          {
            bin.has_symtab = 1;
            bin.symoff  = symtab.symoff;
            bin.nsyms   = symtab.nsyms;
            bin.stroff  = symtab.stroff;
            bin.strsize = symtab.strsize;
          }
        }break;
        case MACHO_LoadCommandKind_UUID:
        if(pass == 1)
        {
          MACHO_UUIDCommand uuid = {0};
          if(str8_deserial_read_struct(lc_data, 0, &uuid) == sizeof(uuid))
          {
            bin.has_uuid = 1;
            MemoryCopy(bin.uuid, uuid.uuid, sizeof(bin.uuid));
          }
        }break;
        case MACHO_LoadCommandKind_Main:
        if(pass == 1)
        {
          MACHO_EntryPointCommand entry = {0};
          if(str8_deserial_read_struct(lc_data, 0, &entry) == sizeof(entry))
          {
            bin.has_entry = 1;
            bin.entry_off = entry.entryoff;
          }
        }break;
      }
      off += lc.cmdsize;
    }
  }
  return bin;
}

internal MACHO_SegmentInfo *
macho_segment_from_name(MACHO_Bin *bin, String8 name)
{
  MACHO_SegmentInfo *result = 0;
  for EachIndex(idx, bin->segment_count)
  {
    if(str8_match(bin->segments[idx].name, name, 0))
    {
      result = &bin->segments[idx];
      break;
    }
  }
  return result;
}

internal MACHO_SectionInfo *
macho_section_from_name(MACHO_Bin *bin, String8 segname, String8 sectname)
{
  MACHO_SectionInfo *result = 0;
  for EachIndex(idx, bin->section_count)
  {
    if(str8_match(bin->sections[idx].segname, segname, 0) &&
       str8_match(bin->sections[idx].sectname, sectname, 0))
    {
      result = &bin->sections[idx];
      break;
    }
  }
  return result;
}

internal U64
macho_text_vaddr_from_bin(MACHO_Bin *bin)
{
  // NOTE: an image's base address is that of its __TEXT segment (which maps
  // the Mach-O header itself, at file offset 0)
  U64 result = 0;
  MACHO_SegmentInfo *text = macho_segment_from_name(bin, str8_lit("__TEXT"));
  if(text != 0)
  {
    result = text->vaddr;
  }
  return result;
}

internal U64
macho_image_vsize_from_bin(MACHO_Bin *bin)
{
  U64 base = macho_text_vaddr_from_bin(bin);
  U64 opl = base;
  for EachIndex(idx, bin->segment_count)
  {
    MACHO_SegmentInfo *seg = &bin->segments[idx];
    if(str8_match(seg->name, str8_lit("__PAGEZERO"), 0)) { continue; }
    if(seg->vaddr + seg->vsize > opl)
    {
      opl = seg->vaddr + seg->vsize;
    }
  }
  return opl - base;
}

internal B32
macho_has_dwarf(MACHO_Bin *bin)
{
  B32 result = 0;
  for EachIndex(idx, bin->section_count)
  {
    MACHO_SectionInfo *sect = &bin->sections[idx];
    if(str8_match(sect->segname, str8_lit("__DWARF"), 0) &&
       str8_match(sect->sectname, str8_lit("__debug_info"), 0) &&
       sect->size != 0)
    {
      result = 1;
      break;
    }
  }
  return result;
}

internal B32
macho_has_debug_map(MACHO_Bin *bin)
{
  // NOTE: the "debug map" is a set of STABS symbols (N_OSO) referencing the
  // object files which hold the DWARF, which is left there by the linker
  // unless `dsymutil` is run to gather it into a .dSYM bundle.
  B32 result = 0;
  if(bin->has_symtab)
  {
    U64 syms_opl = Min(bin->data.size, bin->symoff + bin->nsyms*sizeof(MACHO_Nlist64));
    for(U64 off = bin->symoff; off + sizeof(MACHO_Nlist64) <= syms_opl; off += sizeof(MACHO_Nlist64))
    {
      MACHO_Nlist64 nlist = {0};
      str8_deserial_read_struct(bin->data, off, &nlist);
      if(nlist.n_type == MACHO_N_OSO)
      {
        result = 1;
        break;
      }
    }
  }
  return result;
}

internal Guid
macho_guid_from_bin(MACHO_Bin *bin)
{
  Guid result = {0};
  if(bin->has_uuid)
  {
    MemoryCopy(result.v, bin->uuid, sizeof(result.v));
  }
  return result;
}
