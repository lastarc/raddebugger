// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Helpers

typedef struct MACHO_UWND_FunctionInfo MACHO_UWND_FunctionInfo;
struct MACHO_UWND_FunctionInfo
{
  B32 found;
  U64 func_vaddr;
  U32 encoding;
};

// NOTE: reads from the memory map; on failure, fills `result` with the
// missed range (so the caller can fetch it & retry), & returns 0.
internal B32
macho_uwnd_read(MemoryMap *memory_map, U64 vaddr, U64 size, void *out, UWND_StepResult *result)
{
  Rng1U64 range = r1u64(vaddr, vaddr+size);
  B32 good = (memory_map_read(memory_map, range, out) == size);
  if(!good)
  {
    result->status = UWND_StepStatus_FailedMemoryRead;
    result->missed_read_vaddr_range = range;
  }
  return good;
}

internal B32
macho_uwnd_function_info_from_pc(MemoryMap *memory_map, U64 image_base, Rng1U64 unwind_info_range, U64 pc, MACHO_UWND_FunctionInfo *info_out, UWND_StepResult *result)
{
  MemoryZeroStruct(info_out);
  U64 base = unwind_info_range.min;
  U64 size = dim_1u64(unwind_info_range);
  if(size < sizeof(MACHO_UnwindInfoHeader) || pc < image_base)
  {
    return 1;
  }
  U64 func_off = pc - image_base;

  //- read header
  MACHO_UnwindInfoHeader hdr = {0};
  if(!macho_uwnd_read(memory_map, base, sizeof(hdr), &hdr, result)) { return 0; }
  if(hdr.version != MACHO_UNWIND_SECTION_VERSION || hdr.index_count < 2 ||
     (U64)hdr.index_section_offset + (U64)hdr.index_count*sizeof(MACHO_UnwindInfoIndexEntry) > size)
  {
    return 1;
  }

  //- binary search first-level index (last entry is a sentinel w/ the end offset)
  MACHO_UnwindInfoIndexEntry idx_entry = {0};
  MACHO_UnwindInfoIndexEntry next_idx_entry = {0};
  {
    U64 entries_vaddr = base + hdr.index_section_offset;
    MACHO_UnwindInfoIndexEntry last = {0};
    if(!macho_uwnd_read(memory_map, entries_vaddr + (hdr.index_count-1)*sizeof(last), sizeof(last), &last, result)) { return 0; }
    if(func_off >= last.function_offset)
    {
      return 1;
    }
    U64 lo = 0;
    U64 hi = hdr.index_count-1; // exclusive (sentinel)
    for(;hi - lo > 1;)
    {
      U64 mid = lo + (hi - lo)/2;
      MACHO_UnwindInfoIndexEntry e = {0};
      if(!macho_uwnd_read(memory_map, entries_vaddr + mid*sizeof(e), sizeof(e), &e, result)) { return 0; }
      if(e.function_offset <= func_off) { lo = mid; }
      else                              { hi = mid; }
    }
    if(!macho_uwnd_read(memory_map, entries_vaddr + lo*sizeof(idx_entry), sizeof(idx_entry), &idx_entry, result)) { return 0; }
    if(!macho_uwnd_read(memory_map, entries_vaddr + (lo+1)*sizeof(next_idx_entry), sizeof(next_idx_entry), &next_idx_entry, result)) { return 0; }
    if(func_off < idx_entry.function_offset || idx_entry.second_level_pages_section_offset == 0)
    {
      return 1;
    }
  }

  //- read second-level page
  U64 page_vaddr = base + idx_entry.second_level_pages_section_offset;
  U32 kind = 0;
  if(!macho_uwnd_read(memory_map, page_vaddr, sizeof(kind), &kind, result)) { return 0; }
  switch(kind)
  {
    default:{}break;
    case MACHO_UNWIND_SECOND_LEVEL_REGULAR:
    {
      MACHO_UnwindInfoRegularPageHeader page = {0};
      if(!macho_uwnd_read(memory_map, page_vaddr, sizeof(page), &page, result)) { return 0; }
      if(page.entry_count == 0) { break; }
      U64 entries_vaddr = page_vaddr + page.entry_page_offset;
      U64 lo = 0;
      U64 hi = page.entry_count;
      for(;hi - lo > 1;)
      {
        U64 mid = lo + (hi - lo)/2;
        MACHO_UnwindInfoRegularEntry e = {0};
        if(!macho_uwnd_read(memory_map, entries_vaddr + mid*sizeof(e), sizeof(e), &e, result)) { return 0; }
        if(e.function_offset <= func_off) { lo = mid; }
        else                              { hi = mid; }
      }
      MACHO_UnwindInfoRegularEntry e = {0};
      if(!macho_uwnd_read(memory_map, entries_vaddr + lo*sizeof(e), sizeof(e), &e, result)) { return 0; }
      if(e.function_offset <= func_off)
      {
        info_out->found      = 1;
        info_out->func_vaddr = image_base + e.function_offset;
        info_out->encoding   = e.encoding;
      }
    }break;
    case MACHO_UNWIND_SECOND_LEVEL_COMPRESSED:
    {
      MACHO_UnwindInfoCompressedPageHeader page = {0};
      if(!macho_uwnd_read(memory_map, page_vaddr, sizeof(page), &page, result)) { return 0; }
      if(page.entry_count == 0) { break; }
      U64 entries_vaddr = page_vaddr + page.entry_page_offset;
      U64 lo = 0;
      U64 hi = page.entry_count;
      for(;hi - lo > 1;)
      {
        U64 mid = lo + (hi - lo)/2;
        U32 e = 0;
        if(!macho_uwnd_read(memory_map, entries_vaddr + mid*sizeof(e), sizeof(e), &e, result)) { return 0; }
        U64 e_func_off = (U64)idx_entry.function_offset + (e & 0x00FFFFFFu);
        if(e_func_off <= func_off) { lo = mid; }
        else                       { hi = mid; }
      }
      U32 e = 0;
      if(!macho_uwnd_read(memory_map, entries_vaddr + lo*sizeof(e), sizeof(e), &e, result)) { return 0; }
      U64 e_func_off = (U64)idx_entry.function_offset + (e & 0x00FFFFFFu);
      U32 encoding_idx = (e >> 24) & 0xFF;
      U32 encoding = 0;
      if(encoding_idx < hdr.common_encodings_array_count)
      {
        U64 vaddr = base + hdr.common_encodings_array_section_offset + encoding_idx*sizeof(U32);
        if(!macho_uwnd_read(memory_map, vaddr, sizeof(encoding), &encoding, result)) { return 0; }
      }
      else
      {
        U32 page_encoding_idx = encoding_idx - hdr.common_encodings_array_count;
        if(page_encoding_idx >= page.encodings_count) { break; }
        U64 vaddr = page_vaddr + page.encodings_page_offset + page_encoding_idx*sizeof(U32);
        if(!macho_uwnd_read(memory_map, vaddr, sizeof(encoding), &encoding, result)) { return 0; }
      }
      if(e_func_off <= func_off)
      {
        info_out->found      = 1;
        info_out->func_vaddr = image_base + e_func_off;
        info_out->encoding   = encoding;
      }
    }break;
  }
  return 1;
}

//- function starts (LC_FUNCTION_STARTS: ULEB128 deltas, the first relative to __TEXT)

internal U64 *
macho_uwnd_function_starts_from_data(Arena *arena, String8 data, U64 base_vaddr, U64 *count_out)
{
  // NOTE: count first, then fill (each entry is at least one byte)
  U64 count = 0;
  for(U64 off = 0, pass = 0; pass < 2; pass += 1, off = 0)
  {
    U64 *starts = (pass == 1) ? push_array_no_zero(arena, U64, count) : 0;
    U64 idx = 0;
    U64 addr = base_vaddr;
    for(;off < data.size;)
    {
      U64 delta = 0;
      U64 shift = 0;
      B32 done = 0;
      for(;off < data.size && shift < 64;)
      {
        U8 byte = data.str[off];
        off += 1;
        delta |= (U64)(byte & 0x7F) << shift;
        shift += 7;
        if(!(byte & 0x80)) { done = 1; break; }
      }
      if(!done || delta == 0) { break; } // zero terminates the list
      addr += delta;
      if(pass == 1 && idx < count) { starts[idx] = addr; }
      idx += 1;
    }
    if(pass == 0)
    {
      count = idx;
    }
    else
    {
      *count_out = Min(idx, count);
      return starts;
    }
  }
  *count_out = 0;
  return 0;
}

internal U64
macho_uwnd_function_start_from_vaddr(MACHO_UWND_ModuleUnwindInfo *unwind_info, U64 vaddr)
{
  U64 result = 0;
  U64 *v = unwind_info->function_starts;
  U64 count = unwind_info->function_starts_count;
  if(count != 0 && v[0] <= vaddr)
  {
    U64 lo = 0;
    U64 hi = count;
    for(;hi - lo > 1;)
    {
      U64 mid = lo + (hi - lo)/2;
      if(v[mid] <= vaddr) { lo = mid; }
      else                { hi = mid; }
    }
    result = v[lo];
  }
  return result;
}

////////////////////////////////
//~ Unwinding Abstraction Implementation

internal UWND_StepResult
macho_uwnd_step(Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *regs, U64 *cfa_out)
{
  UWND_StepResult result = {UWND_StepStatus_Error};
  MACHO_UWND_ModuleUnwindInfo *unwind_info = (MACHO_UWND_ModuleUnwindInfo *)module_info->unwind_info;
  if(arch != Arch_x64 || unwind_info == 0)
  {
    return uwnd_step_moduleless(arch, memory_map, regs);
  }
  X64_RegBlock *r = (X64_RegBlock *)regs;
  U64 pc = r->rip;

  //- look up this pc's function & compact unwind encoding
  MACHO_UWND_FunctionInfo info = {0};
  if(!macho_uwnd_function_info_from_pc(memory_map, module_info->base_vaddr, unwind_info->unwind_info_vaddr_range, pc, &info, &result))
  {
    return result;
  }
  
  //- disambiguate `pc == function start`
  //
  // NOTE: a caller frame whose function ends in a call to a noreturn function
  // has a return address equal to the *next* function's start. per the SysV
  // ABI, rsp is 16-byte aligned at call sites (& so in caller frames, after the
  // return address is popped), & is 8 mod 16 at function entry. so, an aligned
  // rsp at a function start means this pc is a return address, which belongs
  // to the function containing pc-1.
  //
  // NOTE: the function's true start: the closest of the compact unwind entry
  // (which may cover several adjacent functions) & the LC_FUNCTION_STARTS one.
  //
  B32 rsp_is_post_call = ((r->rsp & 15) == 8);
  U64 lookup_pc = pc;
  U64 func_start = Max(macho_uwnd_function_start_from_vaddr(unwind_info, pc), info.found ? info.func_vaddr : 0);
  if(func_start != 0 && pc == func_start && !rsp_is_post_call)
  {
    lookup_pc = pc-1;
    if(!macho_uwnd_function_info_from_pc(memory_map, module_info->base_vaddr, unwind_info->unwind_info_vaddr_range, lookup_pc, &info, &result))
    {
      return result;
    }
    func_start = Max(macho_uwnd_function_start_from_vaddr(unwind_info, lookup_pc), info.found ? info.func_vaddr : 0);
  }
  U32 mode = info.found ? (info.encoding & MACHO_UNWIND_X86_64_MODE_MASK) : 0;

  //- DWARF mode -> use the referenced __eh_frame FDE
  if(mode == MACHO_UNWIND_X86_64_MODE_DWARF && dim_1u64(unwind_info->eh_frame_vaddr_range) != 0)
  {
    U64 fde_off = (info.encoding & MACHO_UNWIND_X86_64_DWARF_SECTION_OFFSET);
    U64 fde_vaddr = unwind_info->eh_frame_vaddr_range.min + fde_off;
    EH_PtrCtx ptr_ctx = {0};
    ptr_ctx.pc_vaddr   = unwind_info->eh_frame_vaddr_range.min;
    ptr_ctx.text_vaddr = module_info->base_vaddr;
    ptr_ctx.data_vaddr = unwind_info->eh_frame_vaddr_range.min;
    ptr_ctx.func_vaddr = info.func_vaddr;
    r->rip = lookup_pc; // (CFI row lookup; the new rip comes from the stack)
    result = eh_uwnd_step_from_fde_vaddr(arch, memory_map, &ptr_ctx, EH_PtrEnc_PcRel|EH_PtrEnc_SData4, fde_vaddr, tls_vaddr, regs, cfa_out);
    return result;
  }

  //- read instruction bytes needed for prologue/epilogue detection
  //
  // NOTE: compact unwind encodings describe the state at call sites; the top
  // frame may be stopped at a function's entry (e.g. a function breakpoint),
  // mid-prologue, or at its `ret`, where the frame isn't (or is no longer)
  // set up. those states are detected & unwound as such.
  //
  // NOTE: pc & the function's entry are inside this module's mapped image,
  // so these reads are expected to succeed (once the memory is fetched).
  U8 pc_byte = 0;
  U8 entry_byte = 0;
  if(rsp_is_post_call)
  {
    if(!macho_uwnd_read(memory_map, pc, 1, &pc_byte, &result)) { return result; }
  }
  else if(func_start != 0 && pc == func_start+1)
  {
    if(!macho_uwnd_read(memory_map, func_start, 1, &entry_byte, &result)) { return result; }
  }
  B32 at_entry = (func_start != 0 && pc == func_start && rsp_is_post_call);
  B32 at_ret   = (rsp_is_post_call && pc_byte == 0xC3);
  B32 after_push_rbp = (func_start != 0 && pc == func_start+1 && entry_byte == 0x55 && !rsp_is_post_call);

  //- compute new register values
  U64 new_rip = 0;
  U64 new_rsp = 0;
  U64 new_rbp = r->rbp;
  U64 saved_regs[7] = {0}; // indexed by MACHO_UnwindX64Reg
  B32 saved_regs_valid[7] = {0};
  if(at_entry || at_ret)
  {
    // return address is at the top of the stack
    if(!macho_uwnd_read(memory_map, r->rsp, 8, &new_rip, &result)) { return result; }
    new_rsp = r->rsp + 8;
  }
  else if(after_push_rbp)
  {
    // `push rbp` executed, `mov rbp, rsp` not yet
    if(!macho_uwnd_read(memory_map, r->rsp, 8, &new_rbp, &result)) { return result; }
    if(!macho_uwnd_read(memory_map, r->rsp+8, 8, &new_rip, &result)) { return result; }
    new_rsp = r->rsp + 16;
  }
  else if(mode == MACHO_UNWIND_X86_64_MODE_STACK_IMMD || mode == MACHO_UNWIND_X86_64_MODE_STACK_IND)
  {
    // frameless functions: fixed stack size, w/ saved registers right below
    // the return address
    U32 stack_size = (info.encoding & MACHO_UNWIND_X86_64_FRAMELESS_STACK_SIZE) >> 16;
    U32 stack_adjust = (info.encoding & MACHO_UNWIND_X86_64_FRAMELESS_STACK_ADJUST) >> 13;
    U32 reg_count = (info.encoding & MACHO_UNWIND_X86_64_FRAMELESS_STACK_REG_COUNT) >> 10;
    U32 permutation = (info.encoding & MACHO_UNWIND_X86_64_FRAMELESS_STACK_REG_PERMUTATION);
    U64 stack_size_bytes = 0;
    if(mode == MACHO_UNWIND_X86_64_MODE_STACK_IND)
    {
      // stack size is encoded in the function's `sub rsp, imm32` instruction
      U32 sub_imm = 0;
      if(!macho_uwnd_read(memory_map, info.func_vaddr + stack_size, sizeof(sub_imm), &sub_imm, &result)) { return result; }
      stack_size_bytes = (U64)sub_imm + 8*(U64)stack_adjust;
    }
    else
    {
      stack_size_bytes = 8*(U64)stack_size;
    }
    if(reg_count > 6)
    {
      return result;
    }

    // decode register permutation (see libunwind's CompactUnwinder)
    U32 permunreg[6] = {0};
    switch(reg_count)
    {
      case 6:
      case 5:
      {
        permunreg[0] = permutation/120; permutation -= permunreg[0]*120;
        permunreg[1] = permutation/24;  permutation -= permunreg[1]*24;
        permunreg[2] = permutation/6;   permutation -= permunreg[2]*6;
        permunreg[3] = permutation/2;   permutation -= permunreg[3]*2;
        permunreg[4] = permutation;
        permunreg[5] = 0;
      }break;
      case 4:
      {
        permunreg[0] = permutation/60; permutation -= permunreg[0]*60;
        permunreg[1] = permutation/12; permutation -= permunreg[1]*12;
        permunreg[2] = permutation/3;  permutation -= permunreg[2]*3;
        permunreg[3] = permutation;
      }break;
      case 3:
      {
        permunreg[0] = permutation/20; permutation -= permunreg[0]*20;
        permunreg[1] = permutation/4;  permutation -= permunreg[1]*4;
        permunreg[2] = permutation;
      }break;
      case 2:
      {
        permunreg[0] = permutation/5; permutation -= permunreg[0]*5;
        permunreg[1] = permutation;
      }break;
      case 1:
      {
        permunreg[0] = permutation;
      }break;
      default:{}break;
    }
    U32 registers_saved[6] = {0};
    B32 used[7] = {0};
    for(U32 i = 0; i < reg_count; i += 1)
    {
      U32 renum = 0;
      for(U32 u = 1; u < 7; u += 1)
      {
        if(!used[u])
        {
          if(renum == permunreg[i])
          {
            registers_saved[i] = u;
            used[u] = 1;
            break;
          }
          renum += 1;
        }
      }
    }
    if(stack_size_bytes < 8 + 8*(U64)reg_count)
    {
      return result;
    }
    U64 saved_registers_vaddr = r->rsp + stack_size_bytes - 8 - 8*(U64)reg_count;
    for(U32 i = 0; i < reg_count; i += 1)
    {
      U32 reg = registers_saved[i];
      if(reg != MACHO_UnwindX64Reg_None && reg < ArrayCount(saved_regs))
      {
        if(!macho_uwnd_read(memory_map, saved_registers_vaddr, 8, &saved_regs[reg], &result)) { return result; }
        saved_regs_valid[reg] = 1;
      }
      saved_registers_vaddr += 8;
    }
    if(!macho_uwnd_read(memory_map, saved_registers_vaddr, 8, &new_rip, &result)) { return result; }
    new_rsp = saved_registers_vaddr + 8;
    if(saved_regs_valid[MACHO_UnwindX64Reg_RBP])
    {
      new_rbp = saved_regs[MACHO_UnwindX64Reg_RBP];
    }
  }
  else
  {
    // RBP-based frame: either per the encoding, or as the fallback for code
    // without unwind info (frame pointers are the platform default)
    U64 rbp = r->rbp;
    if(rbp == 0 || rbp < r->rsp)
    {
      return result;
    }
    if(mode == MACHO_UNWIND_X86_64_MODE_RBP_FRAME)
    {
      U32 saved_regs_offset = (info.encoding & MACHO_UNWIND_X86_64_RBP_FRAME_OFFSET) >> 16;
      U32 saved_regs_locations = (info.encoding & MACHO_UNWIND_X86_64_RBP_FRAME_REGISTERS);
      U64 saved_registers_vaddr = rbp - 8*(U64)saved_regs_offset;
      for(U32 i = 0; i < 5; i += 1)
      {
        U32 reg = (saved_regs_locations & 0x7);
        if(reg != MACHO_UnwindX64Reg_None)
        {
          if(reg >= MACHO_UnwindX64Reg_RBP)
          {
            return result;
          }
          if(!macho_uwnd_read(memory_map, saved_registers_vaddr, 8, &saved_regs[reg], &result)) { return result; }
          saved_regs_valid[reg] = 1;
        }
        saved_registers_vaddr += 8;
        saved_regs_locations >>= 3;
      }
    }
    if(!macho_uwnd_read(memory_map, rbp, 8, &new_rbp, &result)) { return result; }
    if(!macho_uwnd_read(memory_map, rbp+8, 8, &new_rip, &result)) { return result; }
    new_rsp = rbp + 16;
  }

  //- commit
  if(saved_regs_valid[MACHO_UnwindX64Reg_RBX]) { r->rbx = saved_regs[MACHO_UnwindX64Reg_RBX]; }
  if(saved_regs_valid[MACHO_UnwindX64Reg_R12]) { r->r12 = saved_regs[MACHO_UnwindX64Reg_R12]; }
  if(saved_regs_valid[MACHO_UnwindX64Reg_R13]) { r->r13 = saved_regs[MACHO_UnwindX64Reg_R13]; }
  if(saved_regs_valid[MACHO_UnwindX64Reg_R14]) { r->r14 = saved_regs[MACHO_UnwindX64Reg_R14]; }
  if(saved_regs_valid[MACHO_UnwindX64Reg_R15]) { r->r15 = saved_regs[MACHO_UnwindX64Reg_R15]; }
  r->rbp = new_rbp;
  r->rip = new_rip;
  r->rsp = new_rsp;
  if(cfa_out != 0)
  {
    cfa_out[0] = new_rsp;
  }
  result.status = UWND_StepStatus_Good;
  return result;
}
