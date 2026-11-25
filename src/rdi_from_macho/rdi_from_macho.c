// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal RDIM_BinarySectionList
m2r_rdi_binary_sections_from_macho_bin(Arena *arena, MACHO_Bin *bin)
{
  // NOTE: virtual offsets are relative to the image base, which for Mach-O
  // images is the __TEXT segment's address (where the header is mapped).
  RDIM_BinarySectionList result = {0};
  U64 base_vaddr = macho_text_vaddr_from_bin(bin);
  for EachIndex(idx, bin->section_count)
  {
    MACHO_SectionInfo *src = &bin->sections[idx];
    if(src->vaddr < base_vaddr) { continue; }
    if(str8_match(src->segname, str8_lit("__DWARF"), 0)) { continue; }
    RDI_BinarySectionFlags flags = 0;
    if(src->seg_initprot & MACHO_VM_PROT_READ)    { flags |= RDI_BinarySectionFlag_Read; }
    if(src->seg_initprot & MACHO_VM_PROT_WRITE)   { flags |= RDI_BinarySectionFlag_Write; }
    if(src->seg_initprot & MACHO_VM_PROT_EXECUTE) { flags |= RDI_BinarySectionFlag_Execute; }
    RDIM_BinarySection *dst = rdim_binary_section_list_push(arena, &result);
    dst->name       = src->sectname;
    dst->flags      = flags;
    dst->voff_first = src->vaddr - base_vaddr;
    dst->voff_opl   = src->vaddr - base_vaddr + src->size;
    dst->foff_first = src->foff;
    dst->foff_opl   = src->foff ? src->foff + src->size : 0;
  }
  return result;
}
