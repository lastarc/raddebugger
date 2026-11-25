// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Constants

#define MAC_DMN_X64_TRAP_FLAG ((U64)1 << 8)
#define MAC_DMN_INT3          0xCC

// NOTE: EXC_SOFTWARE codes for Unix-originated exceptions (xnu's sys/ux_exception.h; not in the SDK)
#define MAC_DMN_EXC_UNIX_BAD_SYSCALL 0x10000
#define MAC_DMN_EXC_UNIX_BAD_PIPE    0x10001
#define MAC_DMN_EXC_UNIX_ABORT       0x10002

////////////////////////////////
//~ Memory Helpers

internal U64
mac_dmn_read(task_t task, Rng1U64 range, void *dst)
{
  U64 size = dim_1u64(range);
  U64 result = 0;
  if(size != 0 && task != MACH_PORT_NULL)
  {
    // NOTE: whole-range read first; on failure, fall back to page-granular
    // reads, stopping at the first unreadable page.
    mach_vm_size_t out_size = 0;
    kern_return_t kr = mach_vm_read_overwrite(task, range.min, size, (mach_vm_address_t)dst, &out_size);
    if(kr == KERN_SUCCESS)
    {
      result = out_size;
    }
    else
    {
      U64 page_size = get_system_info()->page_size;
      for(U64 cursor = 0; cursor < size;)
      {
        U64 vaddr = range.min + cursor;
        U64 chunk = Min(size - cursor, AlignPow2(vaddr+1, page_size) - vaddr);
        mach_vm_size_t chunk_out = 0;
        kr = mach_vm_read_overwrite(task, vaddr, chunk, (mach_vm_address_t)((U8 *)dst + cursor), &chunk_out);
        if(kr != KERN_SUCCESS || chunk_out != chunk)
        {
          break;
        }
        cursor += chunk;
        result = cursor;
      }
    }
  }
  return result;
}

internal B32
mac_dmn_write(task_t task, Rng1U64 range, void *src)
{
  U64 size = dim_1u64(range);
  B32 result = (size == 0);
  if(size != 0 && task != MACH_PORT_NULL && size <= max_U32)
  {
    kern_return_t kr = mach_vm_write(task, range.min, (vm_offset_t)src, (mach_msg_type_number_t)size);
    if(kr == KERN_SUCCESS)
    {
      result = 1;
    }
    else
    {
      // NOTE: not writable (e.g. code) -> per-page: make a private writable
      // copy (VM_PROT_COPY), write, then restore the original protection.
      result = 1;
      U64 page_size = get_system_info()->page_size;
      for(U64 cursor = 0; cursor < size;)
      {
        U64 vaddr = range.min + cursor;
        U64 chunk = Min(size - cursor, AlignPow2(vaddr+1, page_size) - vaddr);
        U64 page_vaddr = AlignDownPow2(vaddr, page_size);

        // query current protection
        mach_vm_address_t region_addr = page_vaddr;
        mach_vm_size_t region_size = 0;
        vm_region_basic_info_data_64_t info = {0};
        mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object_name = MACH_PORT_NULL;
        kr = mach_vm_region(task, &region_addr, &region_size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &info_count, &object_name);
        if(object_name != MACH_PORT_NULL)
        {
          mach_port_deallocate(mach_task_self(), object_name);
        }
        if(kr != KERN_SUCCESS || region_addr > page_vaddr)
        {
          result = 0;
          break;
        }

        // write
        B32 needs_protect = !(info.protection & VM_PROT_WRITE);
        if(needs_protect)
        {
          kr = mach_vm_protect(task, page_vaddr, page_size, 0, VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY);
          if(kr != KERN_SUCCESS)
          {
            result = 0;
            break;
          }
        }
        kr = mach_vm_write(task, vaddr, (vm_offset_t)((U8 *)src + cursor), (mach_msg_type_number_t)chunk);
        if(needs_protect)
        {
          mach_vm_protect(task, page_vaddr, page_size, 0, info.protection);
        }
        if(kr != KERN_SUCCESS)
        {
          result = 0;
          break;
        }
        cursor += chunk;
      }
    }
  }
  return result;
}

////////////////////////////////
//~ Entity Functions

internal MAC_DMN_Entity *
mac_dmn_entity_alloc(MAC_DMN_EntityKind kind)
{
  MAC_DMN_Entity *entity = mac_dmn_state->free_entity;
  if(entity != 0)
  {
    mac_dmn_state->free_entity = entity->next_free;
  }
  else
  {
    entity = push_array(mac_dmn_state->entities_arena, MAC_DMN_Entity, 1);
    mac_dmn_state->entities_count += 1;
  }
  U32 gen = entity->gen;
  MemoryZeroStruct(entity);
  entity->gen = gen + 1;
  entity->kind = kind;
  return entity;
}

internal void
mac_dmn_entity_release(MAC_DMN_Entity *entity)
{
  U32 gen = entity->gen + 1;
  MemoryZeroStruct(entity);
  entity->gen = gen;
  entity->next_free = mac_dmn_state->free_entity;
  mac_dmn_state->free_entity = entity;
}

internal DMN_Handle
mac_dmn_handle_from_entity(MAC_DMN_Entity *entity)
{
  DMN_Handle handle = {0};
  if(entity != 0)
  {
    U64 index = (U64)(entity - mac_dmn_state->entities_base);
    if(index <= max_U32)
    {
      handle.u32[0] = (U32)index;
      handle.u32[1] = entity->gen;
    }
  }
  return handle;
}

internal MAC_DMN_Entity *
mac_dmn_entity_from_handle(DMN_Handle handle, MAC_DMN_EntityKind kind)
{
  MAC_DMN_Entity *result = 0;
  U32 index = handle.u32[0];
  U32 gen = handle.u32[1];
  if(index != 0 && index < mac_dmn_state->entities_count)
  {
    MAC_DMN_Entity *entity = &mac_dmn_state->entities_base[index];
    if(entity->gen == gen && entity->kind == kind)
    {
      result = entity;
    }
  }
  return result;
}

//- process / thread / module lifetimes

internal MAC_DMN_Entity *
mac_dmn_process_alloc(pid_t pid, task_t task, MAC_DMN_ProcessState state, B32 is_child)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_alloc(MAC_DMN_EntityKind_Process);
  MAC_DMN_Process *process = &entity->process;
  process->pid = pid;
  process->task = task;
  process->state = state;
  process->is_child = is_child;
  DLLPushBack_NP(mac_dmn_state->first_process, mac_dmn_state->last_process, entity, process.next, process.prev);
  mac_dmn_state->process_count += 1;
  return entity;
}

internal MAC_DMN_Entity *
mac_dmn_thread_alloc(MAC_DMN_Entity *process_entity, mach_port_t port)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_alloc(MAC_DMN_EntityKind_Thread);
  MAC_DMN_Thread *thread = &entity->thread;
  thread->process = process_entity;
  thread->port = port;
  thread->is_live = 1;
  {
    thread_identifier_info_data_t id_info = {0};
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    if(thread_info(port, THREAD_IDENTIFIER_INFO, (thread_info_t)&id_info, &count) == KERN_SUCCESS)
    {
      thread->tid = id_info.thread_id;
    }
  }
  MAC_DMN_Process *process = &process_entity->process;
  DLLPushBack_NP(process->first_thread, process->last_thread, entity, thread.next, thread.prev);
  process->thread_count += 1;
  return entity;
}

internal void
mac_dmn_reply_exception(mach_port_t reply_port, kern_return_t ret_code)
{
  if(reply_port != MACH_PORT_NULL)
  {
    MAC_DMN_ExcReply reply = {0};
    reply.head.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
    reply.head.msgh_size        = sizeof(reply);
    reply.head.msgh_remote_port = reply_port;
    reply.head.msgh_local_port  = MACH_PORT_NULL;
    reply.head.msgh_id          = MAC_DMN_MACH_EXCEPTION_RAISE_MSG_ID + 100;
    reply.ndr                   = NDR_record;
    reply.ret_code              = ret_code;
    kern_return_t kr = mach_msg(&reply.head, MACH_SEND_MSG|MACH_SEND_TIMEOUT, sizeof(reply), 0, MACH_PORT_NULL, 1000, MACH_PORT_NULL);
    if(kr != KERN_SUCCESS)
    {
      // NOTE: reply right not consumed (e.g. the thread is gone) -> drop it
      mach_port_deallocate(mach_task_self(), reply_port);
    }
  }
}

internal void
mac_dmn_thread_release(MAC_DMN_Entity *entity)
{
  MAC_DMN_Thread *thread = &entity->thread;
  MAC_DMN_Process *process = &thread->process->process;
  if(thread->held.active)
  {
    mac_dmn_reply_exception(thread->held.reply_port, KERN_FAILURE);
    thread->held.active = 0;
  }
  if(thread->port != MACH_PORT_NULL)
  {
    mach_port_deallocate(mach_task_self(), thread->port);
  }
  DLLRemove_NP(process->first_thread, process->last_thread, entity, thread.next, thread.prev);
  process->thread_count -= 1;
  mac_dmn_entity_release(entity);
}

internal MAC_DMN_Entity *
mac_dmn_module_alloc(MAC_DMN_Entity *process_entity, U64 base_vaddr, U64 size, String8 path, B32 is_pinned)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_alloc(MAC_DMN_EntityKind_Module);
  MAC_DMN_Module *module = &entity->module;
  module->process = process_entity;
  module->base_vaddr = base_vaddr;
  module->size = size;
  module->is_pinned = is_pinned;
  module->is_live = 1;
  if(path.size != 0)
  {
    U8 *buf = (U8 *)malloc(path.size + 1);
    if(buf != 0)
    {
      MemoryCopy(buf, path.str, path.size);
      buf[path.size] = 0;
      module->path = str8(buf, path.size);
    }
  }
  MAC_DMN_Process *process = &process_entity->process;
  DLLPushBack_NP(process->first_module, process->last_module, entity, module.next, module.prev);
  process->module_count += 1;
  return entity;
}

internal void
mac_dmn_module_release(MAC_DMN_Entity *entity)
{
  MAC_DMN_Module *module = &entity->module;
  MAC_DMN_Process *process = &module->process->process;
  if(module->path.str != 0)
  {
    free(module->path.str);
  }
  DLLRemove_NP(process->first_module, process->last_module, entity, module.next, module.prev);
  process->module_count -= 1;
  mac_dmn_entity_release(entity);
}

internal void
mac_dmn_process_release(MAC_DMN_Entity *entity, B32 is_in_detached_list)
{
  MAC_DMN_Process *process = &entity->process;
  for(MAC_DMN_Entity *t = process->first_thread, *next = 0; t != 0; t = next)
  {
    next = t->thread.next;
    mac_dmn_thread_release(t);
  }
  for(MAC_DMN_Entity *m = process->first_module, *next = 0; m != 0; m = next)
  {
    next = m->module.next;
    mac_dmn_module_release(m);
  }
  for(U32 idx = 0; idx < process->saved_exc_count; idx += 1)
  {
    if(MACH_PORT_VALID(process->saved_exc_ports[idx]))
    {
      mach_port_deallocate(mach_task_self(), process->saved_exc_ports[idx]);
    }
  }
  if(process->suspend_token != MACH_PORT_NULL)
  {
    // NOTE: destroying the token resumes the task (if it still exists)
    mach_port_deallocate(mach_task_self(), process->suspend_token);
    process->suspend_token = MACH_PORT_NULL;
  }
  if(process->task != MACH_PORT_NULL)
  {
    mach_port_deallocate(mach_task_self(), process->task);
  }
  if(is_in_detached_list)
  {
    DLLRemove_NP(mac_dmn_state->first_detached_process, mac_dmn_state->last_detached_process, entity, process.next, process.prev);
  }
  else
  {
    DLLRemove_NP(mac_dmn_state->first_process, mac_dmn_state->last_process, entity, process.next, process.prev);
    mac_dmn_state->process_count -= 1;
  }
  mac_dmn_entity_release(entity);
}

//- lookups

internal MAC_DMN_Entity *
mac_dmn_process_from_task(task_t task)
{
  MAC_DMN_Entity *result = 0;
  for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
  {
    if(p->process.task == task)
    {
      result = p;
      break;
    }
  }
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_thread_from_port(MAC_DMN_Entity *process, mach_port_t port)
{
  MAC_DMN_Entity *result = 0;
  for(MAC_DMN_Entity *t = process->process.first_thread; t != 0; t = t->thread.next)
  {
    if(t->thread.port == port)
    {
      result = t;
      break;
    }
  }
  return result;
}

////////////////////////////////
//~ Register Helpers

internal void
mac_dmn_regs_from_float_state(X64_RegBlock *dst, _STRUCT_X86_AVX512_STATE64 *src, MAC_DMN_FloatFlavor flavor)
{
  MemoryCopy(&dst->fcw, &src->__fpu_fcw, sizeof(U16));
  MemoryCopy(&dst->fsw, &src->__fpu_fsw, sizeof(U16));
  dst->ftw = src->__fpu_ftw;
  dst->fop = src->__fpu_fop;
  dst->fcs = src->__fpu_cs;
  dst->fds = src->__fpu_ds;
  MemoryCopy(&dst->fip, &src->__fpu_ip, sizeof(U64)); // ip, cs, reserved (matches FXSAVE)
  MemoryCopy(&dst->fdp, &src->__fpu_dp, sizeof(U64)); // dp, ds, reserved (matches FXSAVE)
  dst->mxcsr = src->__fpu_mxcsr;
  dst->mxcsr_mask = src->__fpu_mxcsrmask;
  {
    _STRUCT_MMST_REG *st = &src->__fpu_stmm0;
    U80 *st_dst = &dst->st0;
    for(U32 idx = 0; idx < 8; idx += 1)
    {
      MemoryCopy(&st_dst[idx], st[idx].__mmst_reg, 10);
    }
  }
  {
    _STRUCT_XMM_REG *xmm = &src->__fpu_xmm0;
    U512 *zmm = &dst->zmm0;
    for(U32 idx = 0; idx < 16; idx += 1)
    {
      MemoryCopy(&zmm[idx].u8[0], xmm[idx].__xmm_reg, 16);
    }
  }
  if(flavor == MAC_DMN_FloatFlavor_AVX || flavor == MAC_DMN_FloatFlavor_AVX512)
  {
    _STRUCT_XMM_REG *ymmh = &src->__fpu_ymmh0;
    U512 *zmm = &dst->zmm0;
    for(U32 idx = 0; idx < 16; idx += 1)
    {
      MemoryCopy(&zmm[idx].u8[16], ymmh[idx].__xmm_reg, 16);
    }
  }
  if(flavor == MAC_DMN_FloatFlavor_AVX512)
  {
    _STRUCT_OPMASK_REG *k = &src->__fpu_k0;
    U64 *k_dst = &dst->k0;
    for(U32 idx = 0; idx < 8; idx += 1)
    {
      MemoryCopy(&k_dst[idx], k[idx].__opmask_reg, 8);
    }
    _STRUCT_YMM_REG *zmmh = &src->__fpu_zmmh0;
    U512 *zmm = &dst->zmm0;
    for(U32 idx = 0; idx < 16; idx += 1)
    {
      MemoryCopy(&zmm[idx].u8[32], zmmh[idx].__ymm_reg, 32);
    }
    _STRUCT_ZMM_REG *zmm_hi = &src->__fpu_zmm16;
    for(U32 idx = 0; idx < 16; idx += 1)
    {
      MemoryCopy(&zmm[16+idx].u8[0], zmm_hi[idx].__zmm_reg, 64);
    }
  }
}

internal void
mac_dmn_float_state_from_regs(_STRUCT_X86_AVX512_STATE64 *dst, X64_RegBlock *src, MAC_DMN_FloatFlavor flavor)
{
  MemoryCopy(&dst->__fpu_fcw, &src->fcw, sizeof(U16));
  MemoryCopy(&dst->__fpu_fsw, &src->fsw, sizeof(U16));
  dst->__fpu_ftw = src->ftw;
  dst->__fpu_fop = src->fop;
  dst->__fpu_ip = (U32)src->fip;
  dst->__fpu_cs = src->fcs;
  dst->__fpu_dp = (U32)src->fdp;
  dst->__fpu_ds = src->fds;
  dst->__fpu_mxcsr = src->mxcsr;
  {
    _STRUCT_MMST_REG *st = &dst->__fpu_stmm0;
    U80 *st_src = &src->st0;
    for(U32 idx = 0; idx < 8; idx += 1)
    {
      MemoryCopy(st[idx].__mmst_reg, &st_src[idx], 10);
    }
  }
  {
    _STRUCT_XMM_REG *xmm = &dst->__fpu_xmm0;
    U512 *zmm = &src->zmm0;
    for(U32 idx = 0; idx < 16; idx += 1)
    {
      MemoryCopy(xmm[idx].__xmm_reg, &zmm[idx].u8[0], 16);
    }
  }
  if(flavor == MAC_DMN_FloatFlavor_AVX || flavor == MAC_DMN_FloatFlavor_AVX512)
  {
    _STRUCT_XMM_REG *ymmh = &dst->__fpu_ymmh0;
    U512 *zmm = &src->zmm0;
    for(U32 idx = 0; idx < 16; idx += 1)
    {
      MemoryCopy(ymmh[idx].__xmm_reg, &zmm[idx].u8[16], 16);
    }
  }
  if(flavor == MAC_DMN_FloatFlavor_AVX512)
  {
    _STRUCT_OPMASK_REG *k = &dst->__fpu_k0;
    U64 *k_src = &src->k0;
    for(U32 idx = 0; idx < 8; idx += 1)
    {
      MemoryCopy(k[idx].__opmask_reg, &k_src[idx], 8);
    }
    _STRUCT_YMM_REG *zmmh = &dst->__fpu_zmmh0;
    U512 *zmm = &src->zmm0;
    for(U32 idx = 0; idx < 16; idx += 1)
    {
      MemoryCopy(zmmh[idx].__ymm_reg, &zmm[idx].u8[32], 32);
    }
    _STRUCT_ZMM_REG *zmm_hi = &dst->__fpu_zmm16;
    for(U32 idx = 0; idx < 16; idx += 1)
    {
      MemoryCopy(zmm_hi[idx].__zmm_reg, &zmm[16+idx].u8[0], 64);
    }
  }
}

internal B32
mac_dmn_thread_read_regs(MAC_DMN_Thread *thread)
{
  B32 result = 0;
  X64_RegBlock *dst = &thread->regs;

  //- general purpose registers
  x86_thread_state64_t ts = {0};
  mach_msg_type_number_t ts_count = x86_THREAD_STATE64_COUNT;
  if(thread_get_state(thread->port, x86_THREAD_STATE64, (thread_state_t)&ts, &ts_count) == KERN_SUCCESS)
  {
    result = 1;
    MemoryZeroStruct(dst);
    dst->rax    = ts.__rax;
    dst->rbx    = ts.__rbx;
    dst->rcx    = ts.__rcx;
    dst->rdx    = ts.__rdx;
    dst->rdi    = ts.__rdi;
    dst->rsi    = ts.__rsi;
    dst->rbp    = ts.__rbp;
    dst->rsp    = ts.__rsp;
    dst->r8     = ts.__r8;
    dst->r9     = ts.__r9;
    dst->r10    = ts.__r10;
    dst->r11    = ts.__r11;
    dst->r12    = ts.__r12;
    dst->r13    = ts.__r13;
    dst->r14    = ts.__r14;
    dst->r15    = ts.__r15;
    dst->rip    = ts.__rip;
    dst->rflags = ts.__rflags | 0x2;
    dst->cs     = (U16)ts.__cs;
    dst->fs     = (U16)ts.__fs;
    dst->gs     = (U16)ts.__gs;
  }

  //- floating point / vector registers (widest flavor the thread supports)
  if(result)
  {
    MAC_DMN_FloatFlavor flavors[] = {MAC_DMN_FloatFlavor_AVX512, MAC_DMN_FloatFlavor_AVX, MAC_DMN_FloatFlavor_Float};
    B32 got_float = 0;
    for(U32 idx = 0; idx < ArrayCount(flavors) && !got_float; idx += 1)
    {
      MAC_DMN_FloatFlavor flavor = flavors[idx];
      if(thread->float_flavor != MAC_DMN_FloatFlavor_Null && thread->float_flavor != flavor && idx == 0)
      {
        // NOTE: try the last-known-good flavor first
        flavor = thread->float_flavor;
      }
      thread_state_flavor_t mach_flavor = 0;
      mach_msg_type_number_t count = 0;
      switch(flavor)
      {
        default:{}break;
        case MAC_DMN_FloatFlavor_AVX512:{mach_flavor = x86_AVX512_STATE64; count = x86_AVX512_STATE64_COUNT;}break;
        case MAC_DMN_FloatFlavor_AVX:   {mach_flavor = x86_AVX_STATE64;    count = x86_AVX_STATE64_COUNT;}break;
        case MAC_DMN_FloatFlavor_Float: {mach_flavor = x86_FLOAT_STATE64;  count = x86_FLOAT_STATE64_COUNT;}break;
      }
      MemoryZeroStruct(&thread->float_state);
      if(mach_flavor != 0 && thread_get_state(thread->port, mach_flavor, (thread_state_t)&thread->float_state, &count) == KERN_SUCCESS)
      {
        got_float = 1;
        thread->float_flavor = flavor;
        mac_dmn_regs_from_float_state(dst, &thread->float_state, flavor);
      }
    }
    if(!got_float)
    {
      thread->float_flavor = MAC_DMN_FloatFlavor_Null;
    }
  }

  //- debug registers
  if(result)
  {
    x86_debug_state64_t ds = {0};
    mach_msg_type_number_t ds_count = x86_DEBUG_STATE64_COUNT;
    if(thread_get_state(thread->port, x86_DEBUG_STATE64, (thread_state_t)&ds, &ds_count) == KERN_SUCCESS)
    {
      dst->dr0 = ds.__dr0;
      dst->dr1 = ds.__dr1;
      dst->dr2 = ds.__dr2;
      dst->dr3 = ds.__dr3;
      dst->dr6 = ds.__dr6;
      dst->dr7 = ds.__dr7;
    }
  }

  //- gs base (macOS x86-64 TLS base: the thread's pthread TSD area)
  if(result)
  {
    thread_identifier_info_data_t id_info = {0};
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    if(thread_info(thread->port, THREAD_IDENTIFIER_INFO, (thread_info_t)&id_info, &count) == KERN_SUCCESS)
    {
      dst->gsbase = id_info.thread_handle;
    }
  }

  thread->regs_valid = result;
  thread->regs_dirty = 0;
  return result;
}

internal B32
mac_dmn_thread_write_regs(MAC_DMN_Thread *thread)
{
  X64_RegBlock *src = &thread->regs;
  x86_thread_state64_t ts = {0};
  ts.__rax    = src->rax;
  ts.__rbx    = src->rbx;
  ts.__rcx    = src->rcx;
  ts.__rdx    = src->rdx;
  ts.__rdi    = src->rdi;
  ts.__rsi    = src->rsi;
  ts.__rbp    = src->rbp;
  ts.__rsp    = src->rsp;
  ts.__r8     = src->r8;
  ts.__r9     = src->r9;
  ts.__r10    = src->r10;
  ts.__r11    = src->r11;
  ts.__r12    = src->r12;
  ts.__r13    = src->r13;
  ts.__r14    = src->r14;
  ts.__r15    = src->r15;
  ts.__rip    = src->rip;
  ts.__rflags = src->rflags;
  ts.__cs     = src->cs;
  ts.__fs     = src->fs;
  ts.__gs     = src->gs;
  B32 result = (thread_set_state(thread->port, x86_THREAD_STATE64, (thread_state_t)&ts, x86_THREAD_STATE64_COUNT) == KERN_SUCCESS);
  if(result && thread->float_flavor != MAC_DMN_FloatFlavor_Null)
  {
    thread_state_flavor_t mach_flavor = 0;
    mach_msg_type_number_t count = 0;
    switch(thread->float_flavor)
    {
      default:{}break;
      case MAC_DMN_FloatFlavor_AVX512:{mach_flavor = x86_AVX512_STATE64; count = x86_AVX512_STATE64_COUNT;}break;
      case MAC_DMN_FloatFlavor_AVX:   {mach_flavor = x86_AVX_STATE64;    count = x86_AVX_STATE64_COUNT;}break;
      case MAC_DMN_FloatFlavor_Float: {mach_flavor = x86_FLOAT_STATE64;  count = x86_FLOAT_STATE64_COUNT;}break;
    }
    mac_dmn_float_state_from_regs(&thread->float_state, src, thread->float_flavor);
    thread_set_state(thread->port, mach_flavor, (thread_state_t)&thread->float_state, count);
  }
  return result;
}

internal void
mac_dmn_thread_flush_regs(MAC_DMN_Thread *thread)
{
  if(thread->regs_valid && thread->regs_dirty)
  {
    mac_dmn_thread_write_regs(thread);
    thread->regs_dirty = 0;
  }
}

////////////////////////////////
//~ In-Memory Mach-O Image Parsing

typedef struct MAC_DMN_ImageInfo MAC_DMN_ImageInfo;
struct MAC_DMN_ImageInfo
{
  B32 valid;
  Arch arch;
  U32 filetype;
  U32 flags;
  U64 base_vaddr;
  U64 slide;
  U64 vsize;
  U64 entry_point_voff;
  Guid uuid;
  Rng1U64 unwind_info_vaddr_range;
  Rng1U64 eh_frame_vaddr_range;
  Rng1U64 function_starts_vaddr_range;
  U64 linkedit_vmaddr;
  U64 linkedit_fileoff;
  U32 symoff;
  U32 nsyms;
  U32 stroff;
  U32 strsize;
};

internal MAC_DMN_ImageInfo
mac_dmn_image_info_from_vaddr(task_t task, U64 base_vaddr)
{
  MAC_DMN_ImageInfo info = {0};
  Temp scratch = scratch_begin(0, 0);
  struct mach_header_64 header = {0};
  if(mac_dmn_read_struct(task, base_vaddr, &header) &&
     header.magic == MH_MAGIC_64 &&
     header.sizeofcmds <= MB(1))
  {
    U8 *cmds = push_array(scratch.arena, U8, header.sizeofcmds);
    if(mac_dmn_read(task, r1u64(base_vaddr + sizeof(header), base_vaddr + sizeof(header) + header.sizeofcmds), cmds) == header.sizeofcmds)
    {
      info.valid = 1;
      info.arch = (header.cputype == CPU_TYPE_X86_64) ? Arch_x64 : Arch_Null;
      info.filetype = header.filetype;
      info.flags = header.flags;
      info.base_vaddr = base_vaddr;

      //- pass 1: find __TEXT (for the slide)
      B32 got_text = 0;
      U64 text_vmaddr = 0;
      U64 text_vmsize = 0;
      for(U64 off = 0, idx = 0; idx < header.ncmds && off + sizeof(struct load_command) <= header.sizeofcmds; idx += 1)
      {
        struct load_command *lc = (struct load_command *)(cmds + off);
        if(lc->cmdsize < sizeof(struct load_command) || off + lc->cmdsize > header.sizeofcmds) { break; }
        if(lc->cmd == LC_SEGMENT_64 && lc->cmdsize >= sizeof(struct segment_command_64))
        {
          struct segment_command_64 *seg = (struct segment_command_64 *)lc;
          if(MemoryMatch(seg->segname, SEG_TEXT, sizeof(SEG_TEXT)))
          {
            got_text = 1;
            text_vmaddr = seg->vmaddr;
            text_vmsize = seg->vmsize;
          }
        }
        off += lc->cmdsize;
      }
      info.slide = got_text ? (base_vaddr - text_vmaddr) : 0;

      //- pass 2: everything else
      U64 max_vaddr = base_vaddr + text_vmsize;
      U32 function_starts_dataoff = 0;
      U32 function_starts_datasize = 0;
      for(U64 off = 0, idx = 0; idx < header.ncmds && off + sizeof(struct load_command) <= header.sizeofcmds; idx += 1)
      {
        struct load_command *lc = (struct load_command *)(cmds + off);
        if(lc->cmdsize < sizeof(struct load_command) || off + lc->cmdsize > header.sizeofcmds) { break; }
        switch(lc->cmd)
        {
          default:{}break;
          case LC_SEGMENT_64:
          if(lc->cmdsize >= sizeof(struct segment_command_64))
          {
            struct segment_command_64 *seg = (struct segment_command_64 *)lc;
            B32 is_pagezero = (seg->vmaddr == 0 && seg->initprot == 0 && seg->maxprot == 0);
            if(!is_pagezero && seg->vmaddr >= text_vmaddr)
            {
              max_vaddr = Max(max_vaddr, seg->vmaddr + seg->vmsize + info.slide);
            }
            if(MemoryMatch(seg->segname, SEG_LINKEDIT, sizeof(SEG_LINKEDIT)))
            {
              info.linkedit_vmaddr = seg->vmaddr + info.slide;
              info.linkedit_fileoff = seg->fileoff;
            }
            U64 nsects_cap = (lc->cmdsize - sizeof(struct segment_command_64)) / sizeof(struct section_64);
            U64 nsects = Min((U64)seg->nsects, nsects_cap);
            struct section_64 *sects = (struct section_64 *)(seg + 1);
            for(U64 s_idx = 0; s_idx < nsects; s_idx += 1)
            {
              struct section_64 *sect = &sects[s_idx];
              if(MemoryMatch(sect->segname, SEG_TEXT, sizeof(SEG_TEXT)))
              {
                Rng1U64 range = r1u64(sect->addr + info.slide, sect->addr + info.slide + sect->size);
                if(MemoryMatch(sect->sectname, "__unwind_info", sizeof("__unwind_info")))
                {
                  info.unwind_info_vaddr_range = range;
                }
                else if(MemoryMatch(sect->sectname, "__eh_frame", sizeof("__eh_frame")))
                {
                  info.eh_frame_vaddr_range = range;
                }
              }
            }
          }break;
          case LC_UUID:
          if(lc->cmdsize >= sizeof(struct uuid_command))
          {
            struct uuid_command *uc = (struct uuid_command *)lc;
            MemoryCopy(&info.uuid, uc->uuid, Min(sizeof(info.uuid), sizeof(uc->uuid)));
          }break;
          case LC_MAIN:
          if(lc->cmdsize >= sizeof(struct entry_point_command))
          {
            struct entry_point_command *ep = (struct entry_point_command *)lc;
            info.entry_point_voff = ep->entryoff;
          }break;
          case LC_FUNCTION_STARTS:
          if(lc->cmdsize >= sizeof(struct linkedit_data_command))
          {
            struct linkedit_data_command *ld = (struct linkedit_data_command *)lc;
            function_starts_dataoff = ld->dataoff;
            function_starts_datasize = ld->datasize;
          }break;
          case LC_SYMTAB:
          if(lc->cmdsize >= sizeof(struct symtab_command))
          {
            struct symtab_command *st = (struct symtab_command *)lc;
            info.symoff = st->symoff;
            info.nsyms = st->nsyms;
            info.stroff = st->stroff;
            info.strsize = st->strsize;
          }break;
        }
        off += lc->cmdsize;
      }

      if(function_starts_datasize != 0 && info.linkedit_vmaddr != 0 && function_starts_dataoff >= info.linkedit_fileoff)
      {
        U64 vaddr = info.linkedit_vmaddr + (function_starts_dataoff - info.linkedit_fileoff);
        info.function_starts_vaddr_range = r1u64(vaddr, vaddr + function_starts_datasize);
      }
      
      // NOTE: images in the dyld shared cache have their segments scattered
      // across the cache; only __TEXT is contiguous w/ the header.
      B32 is_in_shared_cache = !!(header.flags & 0x80000000u); // MH_DYLIB_IN_CACHE
      info.vsize = is_in_shared_cache ? text_vmsize : (max_vaddr - base_vaddr);
      if(info.vsize == 0)
      {
        info.vsize = get_system_info()->page_size;
      }
    }
  }
  scratch_end(scratch);
  return info;
}

internal DMN_ModuleInfo *
mac_dmn_module_info_from_image_info(Arena *arena, MAC_DMN_ImageInfo *image, String8 path)
{
  DMN_ModuleInfo *info = push_array(arena, DMN_ModuleInfo, 1);
  info->arch                          = image->arch;
  info->vsize                         = image->vsize;
  info->entry_point_voff              = image->entry_point_voff;
  info->module_path                   = push_str8_copy(arena, path);
  info->debug_info_path               = push_str8_copy(arena, path);
  info->debug_info_guid               = image->uuid;
  info->macho_unwind_info_vaddr_range = image->unwind_info_vaddr_range;
  info->macho_eh_frame_vaddr_range    = image->eh_frame_vaddr_range;
  info->macho_function_starts_vaddr_range = image->function_starts_vaddr_range;
  return info;
}

////////////////////////////////
//~ Module Tracking

typedef struct MAC_DMN_ImageCandidate MAC_DMN_ImageCandidate;
struct MAC_DMN_ImageCandidate
{
  MAC_DMN_ImageCandidate *next;
  U64 base_vaddr;
  String8 path;
  MAC_DMN_ImageInfo info;
};

typedef struct MAC_DMN_ImageCandidateList MAC_DMN_ImageCandidateList;
struct MAC_DMN_ImageCandidateList
{
  MAC_DMN_ImageCandidate *first;
  MAC_DMN_ImageCandidate *last;
  U64 count;
};

internal void
mac_dmn_image_candidate_push(Arena *arena, MAC_DMN_ImageCandidateList *list, U64 base_vaddr, String8 path)
{
  MAC_DMN_ImageCandidate *c = push_array(arena, MAC_DMN_ImageCandidate, 1);
  c->base_vaddr = base_vaddr;
  c->path = push_str8_copy(arena, path);
  SLLQueuePush(list->first, list->last, c);
  list->count += 1;
}

//- region scan for the main executable & dyld (valid before dyld has run)
internal void
mac_dmn_image_candidates_from_region_scan(Arena *arena, MAC_DMN_ImageCandidateList *list, MAC_DMN_Process *process, B32 want_executable)
{
  B32 found_exe = !want_executable;
  B32 found_dyld = 0;
  mach_vm_address_t addr = 0;
  for(U64 iter = 0; iter < 1000000 && !(found_exe && found_dyld); iter += 1)
  {
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info = {0};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name = MACH_PORT_NULL;
    kern_return_t kr = mach_vm_region(process->task, &addr, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object_name);
    if(object_name != MACH_PORT_NULL)
    {
      mach_port_deallocate(mach_task_self(), object_name);
    }
    if(kr != KERN_SUCCESS || size == 0)
    {
      break;
    }
    if(info.protection & VM_PROT_READ)
    {
      struct mach_header_64 header = {0};
      if(mac_dmn_read_struct(process->task, addr, &header) && header.magic == MH_MAGIC_64)
      {
        B32 is_exe  = (header.filetype == MH_EXECUTE && !found_exe);
        B32 is_dyld = (header.filetype == MH_DYLINKER && !found_dyld);
        if(is_exe || is_dyld)
        {
          char path_buf[PROC_PIDPATHINFO_MAXSIZE] = {0};
          int path_len = proc_regionfilename(process->pid, addr, path_buf, sizeof(path_buf));
          String8 path = str8((U8 *)path_buf, path_len > 0 ? (U64)path_len : 0);
          if(path.size == 0 && is_exe)
          {
            int len = proc_pidpath(process->pid, path_buf, sizeof(path_buf));
            path = str8((U8 *)path_buf, len > 0 ? (U64)len : 0);
          }
          if(path.size == 0 && is_dyld)
          {
            path = str8_lit("/usr/lib/dyld");
          }
          mac_dmn_image_candidate_push(arena, list, addr, path);
          found_exe |= is_exe;
          found_dyld |= is_dyld;
        }
      }
    }
    if(addr + size < addr) { break; }
    addr += size;
  }
}

//- dyld's image list (valid once dyld has initialized)
internal B32
mac_dmn_image_candidates_from_dyld(Arena *arena, MAC_DMN_ImageCandidateList *list, MAC_DMN_Process *process)
{
  B32 result = 0;
  kern_return_t kr = KERN_SUCCESS;
  MAC_DMN_DyldProcessInfo info = _dyld_process_info_create(process->task, 0, &kr);
  if(info != 0)
  {
    result = 1;
    __block Arena *block_arena = arena;
    __block MAC_DMN_ImageCandidateList *block_list = list;
    _dyld_process_info_for_each_image(info, ^(uint64_t mach_header_address, const uuid_t uuid, const char *path)
                                      {
                                        mac_dmn_image_candidate_push(block_arena, block_list, mach_header_address, str8_cstring(path ? path : ""));
                                      });
    _dyld_process_info_release(info);
  }
  return result;
}

internal void
mac_dmn_push_event_load_module(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process, MAC_DMN_Entity *thread, MAC_DMN_Entity *module, MAC_DMN_ImageInfo *image)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind        = DMN_EventKind_LoadModule;
  e->process     = mac_dmn_handle_from_entity(process);
  e->thread      = mac_dmn_handle_from_entity(thread);
  e->module      = mac_dmn_handle_from_entity(module);
  e->arch        = image->arch;
  e->address     = module->module.base_vaddr;
  e->size        = module->module.size;
  e->string      = push_str8_copy(arena, module->module.path);
  e->module_info = mac_dmn_module_info_from_image_info(arena, image, module->module.path);
}

internal void
mac_dmn_push_event_unload_module(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process, MAC_DMN_Entity *module)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind    = DMN_EventKind_UnloadModule;
  e->process = mac_dmn_handle_from_entity(process);
  e->module  = mac_dmn_handle_from_entity(module);
  e->string  = push_str8_copy(arena, module->module.path);
}

// NOTE: diffs the process' module list against `candidates` (matched by base
// address). new images are loaded w/ the main executable first, then dyld.
internal void
mac_dmn_sync_modules(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread, MAC_DMN_ImageCandidateList *candidates, B32 allow_unloads)
{
  Temp scratch = scratch_begin(&arena, 1);
  MAC_DMN_Process *process = &process_entity->process;

  //- mark live modules; gather new candidates
  for(MAC_DMN_Entity *m = process->first_module; m != 0; m = m->module.next)
  {
    m->module.is_live = m->module.is_pinned;
  }
  MAC_DMN_ImageCandidate **new_candidates = push_array(scratch.arena, MAC_DMN_ImageCandidate *, candidates->count);
  U64 new_count = 0;
  for(MAC_DMN_ImageCandidate *c = candidates->first; c != 0; c = c->next)
  {
    B32 found = 0;
    for(MAC_DMN_Entity *m = process->first_module; m != 0; m = m->module.next)
    {
      if(m->module.base_vaddr == c->base_vaddr)
      {
        m->module.is_live = 1;
        found = 1;
        break;
      }
    }
    for(U64 idx = 0; idx < new_count && !found; idx += 1)
    {
      found = (new_candidates[idx]->base_vaddr == c->base_vaddr);
    }
    if(!found)
    {
      new_candidates[new_count] = c;
      new_count += 1;
    }
  }

  //- unloads
  if(allow_unloads)
  {
    for(MAC_DMN_Entity *m = process->first_module, *next = 0; m != 0; m = next)
    {
      next = m->module.next;
      if(!m->module.is_live)
      {
        mac_dmn_push_event_unload_module(arena, events, process_entity, m);
        mac_dmn_module_release(m);
      }
    }
  }

  //- loads (main executable, then dyld, then the rest)
  for(U64 idx = 0; idx < new_count; idx += 1)
  {
    new_candidates[idx]->info = mac_dmn_image_info_from_vaddr(process->task, new_candidates[idx]->base_vaddr);
  }
  U32 filetype_order[] = {MH_EXECUTE, MH_DYLINKER, 0};
  for(U32 pass = 0; pass < ArrayCount(filetype_order); pass += 1)
  {
    for(U64 idx = 0; idx < new_count; idx += 1)
    {
      MAC_DMN_ImageCandidate *c = new_candidates[idx];
      if(c == 0 || !c->info.valid) { continue; }
      B32 matches_pass = (filetype_order[pass] == 0 || c->info.filetype == filetype_order[pass]);
      if(!matches_pass) { continue; }
      B32 is_pinned = (c->info.filetype == MH_EXECUTE || c->info.filetype == MH_DYLINKER);
      String8 path = c->path.size != 0 ? c->path : str8_lit("(unknown image)");
      MAC_DMN_Entity *module = mac_dmn_module_alloc(process_entity, c->base_vaddr, c->info.vsize, path, is_pinned);
      mac_dmn_push_event_load_module(arena, events, process_entity, thread, module, &c->info);
      new_candidates[idx] = 0;
    }
  }
  scratch_end(scratch);
}

//- dyld image-notification function discovery
internal void
mac_dmn_find_dyld_notifiers(MAC_DMN_Process *process)
{
  process->notifier_searched = 1;
  process->notifier_count = 0;
  Temp scratch = scratch_begin(0, 0);
  for(MAC_DMN_Entity *m = process->first_module; m != 0; m = m->module.next)
  {
    MAC_DMN_ImageInfo image = mac_dmn_image_info_from_vaddr(process->task, m->module.base_vaddr);
    if(!image.valid || image.filetype != MH_DYLINKER || image.nsyms == 0 || image.linkedit_vmaddr == 0)
    {
      continue;
    }
    if(image.symoff < image.linkedit_fileoff || image.stroff < image.linkedit_fileoff ||
       image.nsyms > 1000000 || image.strsize > MB(64))
    {
      continue;
    }
    U64 symtab_vaddr = image.linkedit_vmaddr + (image.symoff - image.linkedit_fileoff);
    U64 strtab_vaddr = image.linkedit_vmaddr + (image.stroff - image.linkedit_fileoff);
    U64 symtab_size = (U64)image.nsyms * sizeof(struct nlist_64);
    struct nlist_64 *syms = (struct nlist_64 *)push_array_no_zero(scratch.arena, U8, symtab_size);
    U8 *strs = push_array_no_zero(scratch.arena, U8, image.strsize);
    if(mac_dmn_read(process->task, r1u64(symtab_vaddr, symtab_vaddr + symtab_size), syms) != symtab_size ||
       mac_dmn_read(process->task, r1u64(strtab_vaddr, strtab_vaddr + image.strsize), strs) != image.strsize)
    {
      continue;
    }

    // NOTE: dyld calls one of these (depending on its version) whenever the
    // image list changes, before running initializers of new images.
    String8 names[] =
    {
      str8_lit_comp("_lldb_image_notifier"),
      str8_lit_comp("__dyld_debugger_notification"),
      str8_lit_comp("_gdb_image_notifier"),
    };
    for(U32 sym_idx = 0; sym_idx < image.nsyms; sym_idx += 1)
    {
      struct nlist_64 *sym = &syms[sym_idx];
      if((sym->n_type & N_STAB) || (sym->n_type & N_TYPE) != N_SECT || sym->n_un.n_strx >= image.strsize)
      {
        continue;
      }
      String8 sym_name = str8_cstring_capped(strs + sym->n_un.n_strx, strs + image.strsize);
      for(U32 name_idx = 0; name_idx < ArrayCount(names); name_idx += 1)
      {
        if(str8_match(sym_name, names[name_idx], 0) && process->notifier_count < MAC_DMN_MAX_NOTIFIER_TRAPS)
        {
          U64 vaddr = sym->n_value + image.slide;
          B32 dup = 0;
          for(U64 idx = 0; idx < process->notifier_count; idx += 1)
          {
            dup |= (process->notifier_vaddrs[idx] == vaddr);
          }
          if(!dup)
          {
            process->notifier_vaddrs[process->notifier_count] = vaddr;
            process->notifier_count += 1;
          }
        }
      }
    }
    break;
  }
  scratch_end(scratch);
  if(process->notifier_count == 0)
  {
    log_infof("mac demon: no dyld image notifier found (pid %i); module lists are refreshed on every stop\n", process->pid);
  }
}

////////////////////////////////
//~ Thread Tracking

internal void
mac_dmn_push_event_create_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process, MAC_DMN_Entity *thread)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind          = DMN_EventKind_CreateThread;
  e->process       = mac_dmn_handle_from_entity(process);
  e->thread        = mac_dmn_handle_from_entity(thread);
  e->arch          = Arch_x64;
  e->code          = (U32)thread->thread.tid;
  e->stack_pointer = thread->thread.regs_valid ? thread->thread.regs.rsp : 0;
}

internal void
mac_dmn_refresh_threads(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, B32 report_events)
{
  MAC_DMN_Process *process = &process_entity->process;
  thread_act_array_t list = 0;
  mach_msg_type_number_t count = 0;
  if(task_threads(process->task, &list, &count) == KERN_SUCCESS)
  {
    for(MAC_DMN_Entity *t = process->first_thread; t != 0; t = t->thread.next)
    {
      t->thread.is_live = 0;
    }
    for(mach_msg_type_number_t idx = 0; idx < count; idx += 1)
    {
      MAC_DMN_Entity *thread = mac_dmn_thread_from_port(process_entity, list[idx]);
      if(thread != 0)
      {
        thread->thread.is_live = 1;
        mach_port_deallocate(mach_task_self(), list[idx]); // drop the extra reference
      }
      else
      {
        thread = mac_dmn_thread_alloc(process_entity, list[idx]);
        mac_dmn_thread_read_regs(&thread->thread);
        if(report_events)
        {
          mac_dmn_push_event_create_thread(arena, events, process_entity, thread);
        }
      }
    }
    for(MAC_DMN_Entity *t = process->first_thread, *next = 0; t != 0; t = next)
    {
      next = t->thread.next;
      if(!t->thread.is_live)
      {
        if(report_events)
        {
          DMN_Event *e = dmn_event_list_push(arena, events);
          e->kind    = DMN_EventKind_ExitThread;
          e->process = mac_dmn_handle_from_entity(process_entity);
          e->thread  = mac_dmn_handle_from_entity(t);
        }
        mac_dmn_thread_release(t);
      }
    }
    mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)list, count * sizeof(list[0]));
  }
}

////////////////////////////////
//~ Signal Helpers

internal B32
mac_dmn_signal_is_fatal(pid_t pid, U32 signo)
{
  B32 result = 0;
  if(signo == SIGKILL)
  {
    result = 1;
  }
  else if(0 < signo && signo < NSIG)
  {
    // default action is termination?
    local_persist const U8 terminates_by_default[NSIG] =
    {
      [SIGHUP] = 1, [SIGINT] = 1, [SIGQUIT] = 1, [SIGILL] = 1, [SIGTRAP] = 1,
      [SIGABRT] = 1, [SIGEMT] = 1, [SIGFPE] = 1, [SIGBUS] = 1, [SIGSEGV] = 1,
      [SIGSYS] = 1, [SIGPIPE] = 1, [SIGALRM] = 1, [SIGTERM] = 1, [SIGXCPU] = 1,
      [SIGXFSZ] = 1, [SIGVTALRM] = 1, [SIGPROF] = 1, [SIGUSR1] = 1, [SIGUSR2] = 1,
    };
    if(terminates_by_default[signo])
    {
      // does the process catch or ignore it?
      struct kinfo_proc kp = {0};
      size_t kp_size = sizeof(kp);
      int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
      if(sysctl(mib, 4, &kp, &kp_size, 0, 0) == 0 && kp_size >= sizeof(kp))
      {
        U32 mask = (U32)1 << (signo - 1);
        B32 caught  = !!(kp.kp_proc.p_sigcatch & mask);
        B32 ignored = !!(kp.kp_proc.p_sigignore & mask);
        result = !caught && !ignored;
      }
      else
      {
        result = 1;
      }
    }
  }
  return result;
}

internal U32
mac_dmn_signo_from_exception(exception_type_t exception, S64 code0, S64 code1)
{
  U32 result = SIGTRAP;
  switch(exception)
  {
    default:{}break;
    case EXC_BAD_ACCESS:
    {
      result = (code0 == KERN_INVALID_ADDRESS || code0 == EXC_I386_GPFLT) ? SIGSEGV : SIGBUS;
    }break;
    case EXC_BAD_INSTRUCTION:{result = SIGILL;}break;
    case EXC_ARITHMETIC:     {result = SIGFPE;}break;
    case EXC_EMULATION:      {result = SIGEMT;}break;
    case EXC_BREAKPOINT:     {result = SIGTRAP;}break;
    case EXC_SOFTWARE:
    {
      switch(code0)
      {
        default:                  {result = SIGEMT;}break;
        case EXC_SOFT_SIGNAL:     {result = (U32)code1;}break;
        case MAC_DMN_EXC_UNIX_BAD_SYSCALL:{result = SIGSYS;}break;
        case MAC_DMN_EXC_UNIX_BAD_PIPE:   {result = SIGPIPE;}break;
        case MAC_DMN_EXC_UNIX_ABORT:      {result = SIGABRT;}break;
      }
    }break;
  }
  return result;
}

////////////////////////////////
//~ Process Reaping

// NOTE: waits (up to `timeout_ms`) for a child to become a zombie, & reaps it.
// a task port can die w/o the process exiting (exec replaces the task), so
// this never blocks indefinitely.
internal B32
mac_dmn_reap(pid_t pid, U64 timeout_ms, int *status_out)
{
  B32 result = 0;
  U64 deadline_us = now_time_us() + timeout_ms*1000;
  for(;;)
  {
    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if(r == pid)
    {
      *status_out = status;
      result = 1;
      break;
    }
    if(r < 0 && errno != EINTR)
    {
      break;
    }
    if(now_time_us() >= deadline_us)
    {
      break;
    }
    usleep(1000);
  }
  return result;
}

////////////////////////////////
//~ Exception Ports & Process Setup

internal B32
mac_dmn_process_install_exception_port(MAC_DMN_Process *process)
{
  exception_mask_t mask = (EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC |
                           EXC_MASK_EMULATION | EXC_MASK_SOFTWARE | EXC_MASK_BREAKPOINT);
  process->saved_exc_count = EXC_TYPES_COUNT;
  kern_return_t kr = task_get_exception_ports(process->task, mask,
                                              process->saved_exc_masks, &process->saved_exc_count,
                                              process->saved_exc_ports, process->saved_exc_behaviors, process->saved_exc_flavors);
  if(kr != KERN_SUCCESS)
  {
    process->saved_exc_count = 0;
  }
  kr = task_set_exception_ports(process->task, mask, mac_dmn_state->exception_port,
                                (exception_behavior_t)(EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES), THREAD_STATE_NONE);
  if(kr == KERN_SUCCESS)
  {
    mach_port_t previous = MACH_PORT_NULL;
    kern_return_t notify_kr = mach_port_request_notification(mach_task_self(), process->task, MACH_NOTIFY_DEAD_NAME, 0,
                                                             mac_dmn_state->notify_port, MACH_MSG_TYPE_MAKE_SEND_ONCE, &previous);
    if(notify_kr == KERN_SUCCESS && previous != MACH_PORT_NULL)
    {
      mach_port_deallocate(mach_task_self(), previous);
    }
  }
  return (kr == KERN_SUCCESS);
}

internal void
mac_dmn_process_restore_exception_ports(MAC_DMN_Process *process)
{
  exception_mask_t mask = (EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC |
                           EXC_MASK_EMULATION | EXC_MASK_SOFTWARE | EXC_MASK_BREAKPOINT);
  task_set_exception_ports(process->task, mask, MACH_PORT_NULL, EXCEPTION_DEFAULT, THREAD_STATE_NONE);
  for(U32 idx = 0; idx < process->saved_exc_count; idx += 1)
  {
    if(MACH_PORT_VALID(process->saved_exc_ports[idx]))
    {
      task_set_exception_ports(process->task, process->saved_exc_masks[idx], process->saved_exc_ports[idx],
                               process->saved_exc_behaviors[idx], process->saved_exc_flavors[idx]);
    }
  }

  // cancel the dead-name notification
  mach_port_t previous = MACH_PORT_NULL;
  if(mach_port_request_notification(mach_task_self(), process->task, MACH_NOTIFY_DEAD_NAME, 0,
                                    MACH_PORT_NULL, MACH_MSG_TYPE_MAKE_SEND_ONCE, &previous) == KERN_SUCCESS &&
     previous != MACH_PORT_NULL)
  {
    mach_port_deallocate(mach_task_self(), previous);
  }
}

internal void
mac_dmn_log_task_for_pid_failure(pid_t pid, kern_return_t kr)
{
  log_infof("mac demon: task_for_pid(%i) failed (%s, 0x%x).\n", pid, mach_error_string(kr), kr);
  fprintf(stderr,
          "raddbg: cannot debug process %i: task_for_pid failed (%s).\n"
          "raddbg: debugging on macOS requires either running as root, or a raddbg binary signed with the\n"
          "raddbg: com.apple.security.cs.debugger entitlement (build.sh does this) & developer mode enabled\n"
          "raddbg: (`sudo DevToolsSecurity -enable`). Targets w/ the hardened runtime must also be signed\n"
          "raddbg: w/ the com.apple.security.get-task-allow entitlement.\n",
          pid, mach_error_string(kr));
}

////////////////////////////////
//~ Held Exception Replies

internal void
mac_dmn_thread_reply_held(MAC_DMN_Thread *thread, B32 ignore_previous_exception)
{
  MAC_DMN_HeldException *held = &thread->held;
  if(held->active)
  {
    MAC_DMN_Process *process = &thread->process->process;
    held->active = 0;
    switch(held->kind)
    {
      default:
      case MAC_DMN_ReplyKind_Handled:
      {
        mac_dmn_reply_exception(held->reply_port, KERN_SUCCESS);
      }break;
      case MAC_DMN_ReplyKind_SwallowSignal:
      {
        ptrace(PT_THUPDATE, process->pid, (caddr_t)(uintptr_t)thread->port, 0);
        mac_dmn_reply_exception(held->reply_port, KERN_SUCCESS);
      }break;
      case MAC_DMN_ReplyKind_Signal:
      {
        int signo = ignore_previous_exception ? 0 : (int)held->signo;
        ptrace(PT_THUPDATE, process->pid, (caddr_t)(uintptr_t)thread->port, signo);
        mac_dmn_reply_exception(held->reply_port, KERN_SUCCESS);
      }break;
      case MAC_DMN_ReplyKind_HwException:
      {
        if(ignore_previous_exception)
        {
          mac_dmn_reply_exception(held->reply_port, KERN_SUCCESS);
        }
        else
        {
          // NOTE: not handled -> the kernel continues exception processing,
          // turning this into a signal, which is then reported back to us as
          // EXC_SOFT_SIGNAL (& automatically passed through).
          thread->expected_soft_signo = held->signo;
          mac_dmn_reply_exception(held->reply_port, KERN_FAILURE);
        }
      }break;
    }
    held->reply_port = MACH_PORT_NULL;
  }
}

internal void
mac_dmn_thread_hold(MAC_DMN_Thread *thread, mach_port_t reply_port, MAC_DMN_ReplyKind kind, U32 signo)
{
  if(thread->held.active)
  {
    // NOTE: should not happen (a thread can only have one exception in flight)
    mac_dmn_reply_exception(thread->held.reply_port, KERN_SUCCESS);
  }
  thread->held.active = 1;
  thread->held.kind = kind;
  thread->held.reply_port = reply_port;
  thread->held.signo = signo;
}

////////////////////////////////
//~ Message Receiving

// NOTE: classifies a received message. exceptions from tasks we don't track
// (e.g. children which inherited the exception port) are passed on here.
internal MAC_DMN_Msg *
mac_dmn_msg_from_recv_buffer(Arena *arena, MAC_DMN_RecvBuffer *buf)
{
  MAC_DMN_Msg *result = 0;
  mach_msg_header_t *head = &buf->head;
  if(head->msgh_local_port == mac_dmn_state->wakeup_port)
  {
    result = push_array(arena, MAC_DMN_Msg, 1);
    result->kind = MAC_DMN_MsgKind_Wakeup;
  }
  else if(head->msgh_local_port == mac_dmn_state->notify_port && head->msgh_id == MACH_NOTIFY_DEAD_NAME)
  {
    mach_port_t name = buf->dead_name.not_port;
    result = push_array(arena, MAC_DMN_Msg, 1);
    result->kind = MAC_DMN_MsgKind_DeadName;
    result->dead_name = name;
    // NOTE: the notification carries a reference on the dead name
    mach_port_deallocate(mach_task_self(), name);
  }
  else if(head->msgh_local_port == mac_dmn_state->exception_port &&
          head->msgh_id == MAC_DMN_MACH_EXCEPTION_RAISE_MSG_ID &&
          (head->msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
          buf->exc.body.msgh_descriptor_count == 2 &&
          head->msgh_size >= OffsetOf(MAC_DMN_ExcRequest, code))
  {
    MAC_DMN_ExcRequest *req = &buf->exc;
    MAC_DMN_Entity *process = mac_dmn_process_from_task(req->task.name);
    if(process != 0)
    {
      result = push_array(arena, MAC_DMN_Msg, 1);
      result->kind = MAC_DMN_MsgKind_Exception;
      MemoryCopyStruct(&result->exc, req);
      if(result->exc.code_count > 2) { result->exc.code_count = 2; }
      U64 code_bytes_avail = (head->msgh_size - OffsetOf(MAC_DMN_ExcRequest, code)) / sizeof(int64_t);
      if(result->exc.code_count > code_bytes_avail) { result->exc.code_count = (mach_msg_type_number_t)code_bytes_avail; }
    }
    else
    {
      // NOTE: not ours (e.g. a child of a debuggee, which inherited the
      // exception port) -> pass it on. traced-signal exceptions are returned
      // w/ the signal intact; hardware exceptions are left unhandled.
      B32 is_soft_signal = (req->exception == EXC_SOFTWARE && req->code_count >= 2 && req->code[0] == EXC_SOFT_SIGNAL);
      if(is_soft_signal)
      {
        int pid = 0;
        if(pid_for_task(req->task.name, &pid) == KERN_SUCCESS && pid > 0)
        {
          ptrace(PT_THUPDATE, pid, (caddr_t)(uintptr_t)req->thread.name, (int)req->code[1]);
        }
        mac_dmn_reply_exception(head->msgh_remote_port, KERN_SUCCESS);
      }
      else
      {
        mac_dmn_reply_exception(head->msgh_remote_port, KERN_FAILURE);
      }
      mach_port_deallocate(mach_task_self(), req->thread.name);
      mach_port_deallocate(mach_task_self(), req->task.name);
    }
  }
  else
  {
    mach_msg_destroy(head);
  }
  return result;
}

// NOTE: receives one message, w/ a timeout in milliseconds (max_U64 -> block)
internal kern_return_t
mac_dmn_recv(MAC_DMN_RecvBuffer *buf, U64 timeout_ms)
{
  mach_msg_option_t options = MACH_RCV_MSG;
  mach_msg_timeout_t timeout = MACH_MSG_TIMEOUT_NONE;
  if(timeout_ms != max_U64)
  {
    options |= MACH_RCV_TIMEOUT;
    timeout = (mach_msg_timeout_t)Min(timeout_ms, (U64)max_U32);
  }
  kern_return_t kr = mach_msg(&buf->head, options, 0, sizeof(*buf), mac_dmn_state->port_set, timeout, MACH_PORT_NULL);
  return kr;
}

internal void
mac_dmn_state_defer_msg(MAC_DMN_Msg *msg)
{
  MAC_DMN_Msg *copy = push_array(mac_dmn_state->deferred_arena, MAC_DMN_Msg, 1);
  MemoryCopyStruct(copy, msg);
  copy->next = 0;
  SLLQueuePush(mac_dmn_state->deferred_msgs.first, mac_dmn_state->deferred_msgs.last, copy);
  mac_dmn_state->deferred_msgs.count += 1;
}

////////////////////////////////
//~ Stop-The-World Helpers

// NOTE: suspensions are token-based (task_suspend2), so that they are undone
// by the kernel if the debugger dies; a task left suspended can't even exit.
internal B32
mac_dmn_process_suspend(MAC_DMN_Process *process)
{
  if(process->suspend_token == MACH_PORT_NULL && !process->is_dead)
  {
    task_suspension_token_t token = MACH_PORT_NULL;
    if(task_suspend2(process->task, &token) == KERN_SUCCESS)
    {
      process->suspend_token = token;
    }
  }
  return (process->suspend_token != MACH_PORT_NULL);
}

internal void
mac_dmn_process_resume(MAC_DMN_Process *process)
{
  if(process->suspend_token != MACH_PORT_NULL)
  {
    // NOTE: task_resume2 consumes the (send-once) token
    task_resume2(process->suspend_token);
    process->suspend_token = MACH_PORT_NULL;
  }
}

internal void
mac_dmn_suspend_all(void)
{
  for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
  {
    mac_dmn_process_suspend(&p->process);
  }
}

////////////////////////////////
//~ Exception Handling

typedef struct MAC_DMN_HwTrapSlots MAC_DMN_HwTrapSlots;
struct MAC_DMN_HwTrapSlots
{
  MAC_DMN_HwTrapSlots *next;
  MAC_DMN_Entity *process;
  DMN_Trap traps[4];
  U64 count;
};

typedef struct MAC_DMN_RunCtx MAC_DMN_RunCtx;
struct MAC_DMN_RunCtx
{
  DMN_RunCtrls *ctrls;
  MAC_DMN_WrittenTrapArray written_traps;
  MAC_DMN_HwTrapSlots *first_hw_slots;
};

internal B32
mac_dmn_written_trap_exists(MAC_DMN_WrittenTrapArray *traps, MAC_DMN_Entity *process, U64 vaddr)
{
  B32 result = 0;
  for(U64 idx = 0; idx < traps->count; idx += 1)
  {
    if(traps->v[idx].written && traps->v[idx].process == process && traps->v[idx].vaddr == vaddr)
    {
      result = 1;
      break;
    }
  }
  return result;
}

internal B32
mac_dmn_prev_trap_exists(MAC_DMN_Entity *process, U64 vaddr)
{
  B32 result = 0;
  DMN_Handle process_handle = mac_dmn_handle_from_entity(process);
  for(U64 idx = 0; idx < mac_dmn_state->prev_traps_count; idx += 1)
  {
    MAC_DMN_PrevTrap *trap = &mac_dmn_state->prev_traps[idx];
    if(dmn_handle_match(trap->process, process_handle) && trap->vaddr == vaddr)
    {
      result = 1;
      break;
    }
  }
  return result;
}

internal void
mac_dmn_push_handshake_events(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, B32 is_attach)
{
  MAC_DMN_Process *process = &process_entity->process;

  //- process
  {
    DMN_Event *e = dmn_event_list_push(arena, events);
    e->kind    = DMN_EventKind_CreateProcess;
    e->process = mac_dmn_handle_from_entity(process_entity);
    e->arch    = Arch_x64;
    e->code    = (U32)process->pid;
    e->size    = get_system_info()->page_size;
  }

  //- threads
  for(MAC_DMN_Entity *t = process->first_thread; t != 0; t = t->thread.next)
  {
    mac_dmn_push_event_create_thread(arena, events, process_entity, t);
  }

  //- modules
  {
    Temp scratch = scratch_begin(&arena, 1);
    MAC_DMN_ImageCandidateList candidates = {0};
    mac_dmn_image_candidates_from_region_scan(scratch.arena, &candidates, process, !is_attach);
    if(is_attach)
    {
      mac_dmn_image_candidates_from_dyld(scratch.arena, &candidates, process);
    }
    mac_dmn_sync_modules(arena, events, process_entity, process->first_thread, &candidates, 0);
    scratch_end(scratch);
  }

  //- dyld notification traps
  mac_dmn_find_dyld_notifiers(process);

  //- handshake
  {
    DMN_Event *e = dmn_event_list_push(arena, events);
    e->kind    = DMN_EventKind_HandshakeComplete;
    e->process = mac_dmn_handle_from_entity(process_entity);
    e->thread  = mac_dmn_handle_from_entity(process->first_thread);
  }
}

internal void
mac_dmn_push_exception_event(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity, U32 signo, U64 address, DMN_ExceptionKind exception_kind)
{
  MAC_DMN_Thread *thread = &thread_entity->thread;
  U64 ip = thread->regs.rip;
  B32 is_fatal = mac_dmn_signal_is_fatal(process_entity->process.pid, signo);
  B32 is_repeat = (thread->last_exception_ip == ip && thread->last_exception_signo == signo);
  thread->last_exception_ip = ip;
  thread->last_exception_signo = signo;
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_Exception;
  e->exception_kind      = exception_kind;
  e->process             = mac_dmn_handle_from_entity(process_entity);
  e->thread              = mac_dmn_handle_from_entity(thread_entity);
  e->instruction_pointer = ip;
  e->address             = address;
  e->code                = signo;
  e->exception_repeated  = (is_fatal || is_repeat);
}

internal void
mac_dmn_handle_exception(Arena *arena, DMN_EventList *events, MAC_DMN_RunCtx *run, MAC_DMN_ExcRequest *req)
{
  mach_port_t thread_port = req->thread.name;
  mach_port_t task_port = req->task.name;
  mach_port_t reply_port = req->head.msgh_remote_port;
  MAC_DMN_Entity *process_entity = mac_dmn_process_from_task(task_port);
  MAC_DMN_Entity *thread_entity = (process_entity != 0) ? mac_dmn_thread_from_port(process_entity, thread_port) : 0;
  if(process_entity != 0 && thread_entity == 0 && !process_entity->process.is_dead)
  {
    mac_dmn_refresh_threads(arena, events, process_entity, process_entity->process.state == MAC_DMN_ProcessState_Normal);
    thread_entity = mac_dmn_thread_from_port(process_entity, thread_port);
  }

  // NOTE: drop the references which came w/ the message; our entities own theirs.
  mach_port_deallocate(mach_task_self(), thread_port);
  mach_port_deallocate(mach_task_self(), task_port);

  if(process_entity == 0 || thread_entity == 0 || process_entity->process.is_dead)
  {
    mac_dmn_reply_exception(reply_port, KERN_FAILURE);
    return;
  }

  MAC_DMN_Process *process = &process_entity->process;
  MAC_DMN_Thread *thread = &thread_entity->thread;
  exception_type_t exception = req->exception;
  S64 code0 = req->code_count >= 1 ? req->code[0] : 0;
  S64 code1 = req->code_count >= 2 ? req->code[1] : 0;
  U32 signo = mac_dmn_signo_from_exception(exception, code0, code1);
  B32 is_soft_signal = (exception == EXC_SOFTWARE && code0 == EXC_SOFT_SIGNAL);
  if(!thread->regs_valid)
  {
    mac_dmn_thread_read_regs(thread);
  }

  //- process not yet announced: wait for the initial SIGSTOP, pass everything else
  if(process->state != MAC_DMN_ProcessState_Normal)
  {
    if(is_soft_signal && signo == SIGSTOP)
    {
      B32 is_attach = (process->state == MAC_DMN_ProcessState_Attaching);
      process->state = MAC_DMN_ProcessState_Normal;
      mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_SwallowSignal, signo);
      mac_dmn_push_handshake_events(arena, events, process_entity, is_attach);
    }
    else if(is_soft_signal)
    {
      ptrace(PT_THUPDATE, process->pid, (caddr_t)(uintptr_t)thread->port, (int)signo);
      mac_dmn_reply_exception(reply_port, KERN_SUCCESS);
    }
    else
    {
      mac_dmn_reply_exception(reply_port, KERN_FAILURE);
    }
    return;
  }

  //- any non-soft-signal exception cancels a pending pass-through expectation
  U32 expected_soft_signo = thread->expected_soft_signo;
  thread->expected_soft_signo = 0;

  switch(exception)
  {
    //- signals
    case EXC_SOFTWARE:
    {
      if(is_soft_signal && expected_soft_signo != 0 && signo == expected_soft_signo)
      {
        // NOTE: the signal for a hardware exception we already passed on
        ptrace(PT_THUPDATE, process->pid, (caddr_t)(uintptr_t)thread->port, (int)signo);
        mac_dmn_reply_exception(reply_port, KERN_SUCCESS);
      }
      else if(is_soft_signal)
      {
        mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_Signal, signo);
        mac_dmn_push_exception_event(arena, events, process_entity, thread_entity, signo, thread->regs.rip, DMN_ExceptionKind_Null);
      }
      else
      {
        mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_HwException, signo);
        mac_dmn_push_exception_event(arena, events, process_entity, thread_entity, signo, thread->regs.rip, DMN_ExceptionKind_Null);
      }
    }break;

    //- breakpoints / single steps / data breakpoints
    case EXC_BREAKPOINT:
    {
      if(code0 == EXC_I386_BPT)
      {
        U64 trap_vaddr = thread->regs.rip - 1;

        // user trap?
        DMN_Trap *hit_user_trap = 0;
        if(mac_dmn_written_trap_exists(&run->written_traps, process_entity, trap_vaddr))
        {
          DMN_Handle process_handle = mac_dmn_handle_from_entity(process_entity);
          for(DMN_TrapChunkNode *n = run->ctrls->traps.first; n != 0 && hit_user_trap == 0; n = n->next)
          {
            for(U64 idx = 0; idx < n->count; idx += 1)
            {
              DMN_Trap *trap = &n->v[idx];
              if(trap->flags == 0 && trap->vaddr == trap_vaddr && dmn_handle_match(trap->process, process_handle))
              {
                hit_user_trap = trap;
                break;
              }
            }
          }
        }

        // dyld image notifier?
        B32 hit_notifier = 0;
        if(hit_user_trap == 0 && mac_dmn_written_trap_exists(&run->written_traps, process_entity, trap_vaddr))
        {
          for(U64 idx = 0; idx < process->notifier_count; idx += 1)
          {
            hit_notifier |= (process->notifier_vaddrs[idx] == trap_vaddr);
          }
        }

        if(hit_user_trap != 0)
        {
          thread->regs.rip = trap_vaddr;
          thread->regs_dirty = 1;
          mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_Handled, 0);
          DMN_Event *e = dmn_event_list_push(arena, events);
          e->kind                = DMN_EventKind_Breakpoint;
          e->process             = mac_dmn_handle_from_entity(process_entity);
          e->thread              = mac_dmn_handle_from_entity(thread_entity);
          e->instruction_pointer = trap_vaddr;
          e->user_data           = hit_user_trap->id;
        }
        else if(hit_notifier)
        {
          // NOTE: emulate `ret` from the (empty) notifier function, then
          // re-enumerate the process' images
          U64 return_vaddr = 0;
          if(mac_dmn_read_struct(process->task, thread->regs.rsp, &return_vaddr))
          {
            thread->regs.rip = return_vaddr;
            thread->regs.rsp += 8;
          }
          else
          {
            thread->regs.rip = trap_vaddr;
          }
          thread->regs_dirty = 1;
          mac_dmn_reply_exception(reply_port, KERN_SUCCESS);
          Temp scratch = scratch_begin(&arena, 1);
          MAC_DMN_ImageCandidateList candidates = {0};
          if(mac_dmn_image_candidates_from_dyld(scratch.arena, &candidates, process))
          {
            mac_dmn_sync_modules(arena, events, process_entity, thread_entity, &candidates, 1);
          }
          scratch_end(scratch);
        }
        else if(mac_dmn_prev_trap_exists(process_entity, trap_vaddr) && !mac_dmn_written_trap_exists(&run->written_traps, process_entity, trap_vaddr))
        {
          // NOTE: ghost trap: hit during a previous run, but delivered after
          // that trap was removed. roll back & retry the original instruction.
          thread->regs.rip = trap_vaddr;
          thread->regs_dirty = 1;
          mac_dmn_reply_exception(reply_port, KERN_SUCCESS);
        }
        else
        {
          // NOTE: the debuggee's own int3 (e.g. __builtin_debugtrap)
          mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_Handled, 0);
          DMN_Event *e = dmn_event_list_push(arena, events);
          e->kind                = DMN_EventKind_Trap;
          e->process             = mac_dmn_handle_from_entity(process_entity);
          e->thread              = mac_dmn_handle_from_entity(thread_entity);
          e->instruction_pointer = thread->regs.rip;
        }
      }
      else if(code0 == EXC_I386_SGL)
      {
        // data breakpoint hit? (DR6 is captured by the kernel on #DB)
        B32 handled = 0;
        if(thread->debug_state_set)
        {
          x86_debug_state64_t ds = {0};
          mach_msg_type_number_t ds_count = x86_DEBUG_STATE64_COUNT;
          if(thread_get_state(thread->port, x86_DEBUG_STATE64, (thread_state_t)&ds, &ds_count) == KERN_SUCCESS && (ds.__dr6 & 0xF))
          {
            U64 slot = (ds.__dr6 & 1) ? 0 : (ds.__dr6 & 2) ? 1 : (ds.__dr6 & 4) ? 2 : 3;
            U64 dr_addrs[4] = {ds.__dr0, ds.__dr1, ds.__dr2, ds.__dr3};
            DMN_Trap *trap = 0;
            for(MAC_DMN_HwTrapSlots *s = run->first_hw_slots; s != 0; s = s->next)
            {
              if(s->process == process_entity && slot < s->count)
              {
                trap = &s->traps[slot];
                break;
              }
            }
            if(trap != 0 && (ds.__dr7 & (1ull << (slot*2))))
            {
              handled = 1;
              mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_Handled, 0);
              DMN_Event *e = dmn_event_list_push(arena, events);
              e->kind                = DMN_EventKind_Breakpoint;
              e->process             = mac_dmn_handle_from_entity(process_entity);
              e->thread              = mac_dmn_handle_from_entity(thread_entity);
              e->instruction_pointer = thread->regs.rip;
              e->address             = dr_addrs[slot];
              e->user_data           = trap->id;
            }
          }
        }

        // single step?
        if(!handled && thread->single_step_set)
        {
          handled = 1;
          mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_Handled, 0);
          DMN_Event *e = dmn_event_list_push(arena, events);
          e->kind                = DMN_EventKind_SingleStep;
          e->process             = mac_dmn_handle_from_entity(process_entity);
          e->thread              = mac_dmn_handle_from_entity(thread_entity);
          e->instruction_pointer = thread->regs.rip;
          e->address             = thread->regs.rip;
        }

        // otherwise: the debuggee's own trace trap -> SIGTRAP
        if(!handled)
        {
          mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_HwException, SIGTRAP);
          mac_dmn_push_exception_event(arena, events, process_entity, thread_entity, SIGTRAP, thread->regs.rip, DMN_ExceptionKind_Null);
        }
      }
      else
      {
        mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_HwException, SIGTRAP);
        mac_dmn_push_exception_event(arena, events, process_entity, thread_entity, SIGTRAP, thread->regs.rip, DMN_ExceptionKind_Null);
      }
    }break;

    //- memory faults
    case EXC_BAD_ACCESS:
    {
      DMN_ExceptionKind kind = DMN_ExceptionKind_Null;
      x86_exception_state64_t es = {0};
      mach_msg_type_number_t es_count = x86_EXCEPTION_STATE64_COUNT;
      if(thread_get_state(thread->port, x86_EXCEPTION_STATE64, (thread_state_t)&es, &es_count) == KERN_SUCCESS && es.__trapno == 14)
      {
        // NOTE: page fault error code: bit 1 -> write, bit 4 -> instruction fetch
        kind = (es.__err & 0x10) ? DMN_ExceptionKind_MemoryExecute : (es.__err & 0x2) ? DMN_ExceptionKind_MemoryWrite : DMN_ExceptionKind_MemoryRead;
      }
      mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_HwException, signo);
      mac_dmn_push_exception_event(arena, events, process_entity, thread_entity, signo, (U64)code1, kind);
    }break;

    //- other hardware exceptions
    default:
    {
      mac_dmn_thread_hold(thread, reply_port, MAC_DMN_ReplyKind_HwException, signo);
      mac_dmn_push_exception_event(arena, events, process_entity, thread_entity, signo, thread->regs.rip, DMN_ExceptionKind_Null);
    }break;
  }
}

internal void
mac_dmn_push_process_exit_events(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity)
{
  MAC_DMN_Process *process = &process_entity->process;
  if(process->state == MAC_DMN_ProcessState_Normal)
  {
    for(MAC_DMN_Entity *t = process->first_thread; t != 0; t = t->thread.next)
    {
      DMN_Event *e = dmn_event_list_push(arena, events);
      e->kind    = DMN_EventKind_ExitThread;
      e->process = mac_dmn_handle_from_entity(process_entity);
      e->thread  = mac_dmn_handle_from_entity(t);
    }
    for(MAC_DMN_Entity *m = process->first_module; m != 0; m = m->module.next)
    {
      mac_dmn_push_event_unload_module(arena, events, process_entity, m);
    }
    DMN_Event *e = dmn_event_list_push(arena, events);
    e->kind    = DMN_EventKind_ExitProcess;
    e->process = mac_dmn_handle_from_entity(process_entity);
    e->code    = process->exit_code;
  }
}

////////////////////////////////
//~ @dmn_os_hooks Main Layer Initialization (Implemented Per-OS)

internal void
dmn_init(void)
{
  Arena *arena = arena_alloc();
  mac_dmn_state = push_array(arena, MAC_DMN_State, 1);
  mac_dmn_state->arena            = arena;
  mac_dmn_state->access_mutex     = mutex_alloc();
  mac_dmn_state->halter_mutex     = mutex_alloc();
  mac_dmn_state->entities_arena   = arena_alloc(.reserve_size = GB(8), .commit_size = KB(64), .flags = ArenaFlag_NoChain);
  mac_dmn_state->entities_base    = push_array(mac_dmn_state->entities_arena, MAC_DMN_Entity, 0);
  mac_dmn_state->prev_traps_arena = arena_alloc();
  mac_dmn_state->deferred_arena   = arena_alloc();
  mac_dmn_entity_alloc(MAC_DMN_EntityKind_Null);

  //- ports: exception port (w/ a send right, given to debuggees), dead-name
  // notification port, wakeup port (for halts), & a port set of all three.
  mach_port_t self = mach_task_self();
  kern_return_t kr = KERN_SUCCESS;
  kr |= mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &mac_dmn_state->exception_port);
  kr |= mach_port_insert_right(self, mac_dmn_state->exception_port, mac_dmn_state->exception_port, MACH_MSG_TYPE_MAKE_SEND);
  kr |= mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &mac_dmn_state->notify_port);
  kr |= mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &mac_dmn_state->wakeup_port);
  kr |= mach_port_insert_right(self, mac_dmn_state->wakeup_port, mac_dmn_state->wakeup_port, MACH_MSG_TYPE_MAKE_SEND);
  kr |= mach_port_allocate(self, MACH_PORT_RIGHT_PORT_SET, &mac_dmn_state->port_set);
  kr |= mach_port_move_member(self, mac_dmn_state->exception_port, mac_dmn_state->port_set);
  kr |= mach_port_move_member(self, mac_dmn_state->notify_port, mac_dmn_state->port_set);
  kr |= mach_port_move_member(self, mac_dmn_state->wakeup_port, mac_dmn_state->port_set);
  if(kr != KERN_SUCCESS)
  {
    log_infof("mac demon: failed to set up Mach ports\n");
  }
}

////////////////////////////////
//~ @dmn_os_hooks Blocking Control Thread Operations (Implemented Per-OS)

internal DMN_CtrlCtx *
dmn_ctrl_begin(void)
{
  DMN_CtrlCtx *ctx = (DMN_CtrlCtx *)1;
  mac_dmn_ctrl_thread = 1;
  return ctx;
}

internal void
dmn_ctrl_exclusive_access_begin(void)
{
  MutexScope(mac_dmn_state->access_mutex)
  {
    mac_dmn_state->access_run_state = 1;
  }
}

internal void
dmn_ctrl_exclusive_access_end(void)
{
  MutexScope(mac_dmn_state->access_mutex)
  {
    mac_dmn_state->access_run_state = 0;
  }
}

internal U32
dmn_ctrl_launch(DMN_CtrlCtx *ctx, ProcessLaunchParams *params)
{
  Temp scratch = scratch_begin(0, 0);
  pid_t pid = 0;
  if(params->cmd_line.first != 0)
  {
    //- argv
    U64 argc = params->cmd_line.node_count;
    char **argv = push_array(scratch.arena, char *, argc + 1);
    {
      U64 idx = 0;
      for EachNode(n, String8Node, params->cmd_line.first)
      {
        argv[idx] = (char *)push_str8_copy(scratch.arena, n->string).str;
        idx += 1;
      }
    }

    //- envp: user-specified variables first, then inherited ones not overridden
    char **envp = 0;
    {
      U64 cap = params->env.node_count + (params->inherit_env ? mac_state.default_env_count : 0) + 1;
      envp = push_array(scratch.arena, char *, cap);
      U64 count = 0;
      for EachNode(n, String8Node, params->env.first)
      {
        envp[count] = (char *)push_str8_copy(scratch.arena, n->string).str;
        count += 1;
      }
      if(params->inherit_env)
      {
        U64 user_count = count;
        for(U64 idx = 0; idx < mac_state.default_env_count; idx += 1)
        {
          String8 var = str8_cstring(mac_state.default_env[idx]);
          String8 key = str8_prefix(var, str8_find_needle(var, 0, str8_lit("="), 0));
          B32 overridden = 0;
          for(U64 u_idx = 0; u_idx < user_count && !overridden; u_idx += 1)
          {
            String8 user_var = str8_cstring(envp[u_idx]);
            String8 user_key = str8_prefix(user_var, str8_find_needle(user_var, 0, str8_lit("="), 0));
            overridden = str8_match(key, user_key, 0);
          }
          if(!overridden)
          {
            envp[count] = mac_state.default_env[idx];
            count += 1;
          }
        }
      }
      envp[count] = 0;
    }

    //- spawn: fork, request tracing (PT_TRACE_ME), exec
    //
    // NOTE: the child stops (a ptrace stop) right after exec. unlike a
    // process spawned w/ POSIX_SPAWN_START_SUSPENDED, a process in a ptrace
    // stop can still be killed, if we then turn out not to be allowed to get
    // its task port. between fork & exec, the child only makes
    // async-signal-safe calls.
    //
    char *work_dir = (params->path.size != 0) ? (char *)push_str8_copy(scratch.arena, params->path).str : 0;
    int std_fds[3] = {-1, -1, -1};
    {
      File std_files[3] = {params->stdin_file, params->stdout_file, params->stderr_file};
      for(int fd = 0; fd < 3; fd += 1)
      {
        if(!file_match(std_files[fd], file_zero()))
        {
          std_fds[fd] = (int)std_files[fd].u64[0];
        }
      }
    }
    int error_pipe[2] = {-1, -1};
    if(pipe(error_pipe) != 0)
    {
      log_infof("mac demon: pipe() failed: %s\n", strerror(errno));
    }
    else
    {
      fcntl(error_pipe[0], F_SETFD, FD_CLOEXEC);
      fcntl(error_pipe[1], F_SETFD, FD_CLOEXEC);
      pid = fork();
      if(pid == 0)
      {
        int child_error = 0;
        
        // default signal state
        sigset_t no_signals;
        sigemptyset(&no_signals);
        sigprocmask(SIG_SETMASK, &no_signals, 0);
        struct sigaction default_action = {0};
        default_action.sa_handler = SIG_DFL;
        for(int signo = 1; signo < NSIG; signo += 1)
        {
          if(signo != SIGKILL && signo != SIGSTOP)
          {
            sigaction(signo, &default_action, 0);
          }
        }
        
        // working directory, std handles, no other inherited fds
        if(child_error == 0 && work_dir != 0 && chdir(work_dir) != 0) { child_error = errno; }
        for(int fd = 0; fd < 3 && child_error == 0; fd += 1)
        {
          if(std_fds[fd] >= 0 && std_fds[fd] != fd && dup2(std_fds[fd], fd) < 0) { child_error = errno; }
        }
        if(child_error == 0)
        {
          int max_fd = getdtablesize();
          for(int fd = 3; fd < max_fd; fd += 1)
          {
            if(fd != error_pipe[1])
            {
              close(fd);
            }
          }
        }
        
        // trace & exec
        if(child_error == 0 && ptrace(PT_TRACE_ME, 0, 0, 0) != 0) { child_error = errno; }
        if(child_error == 0)
        {
          execve(argv[0], argv, envp);
          child_error = errno;
        }
        write(error_pipe[1], &child_error, sizeof(child_error));
        _exit(127);
      }
      close(error_pipe[1]);
      if(pid < 0)
      {
        log_infof("mac demon: fork() failed: %s\n", strerror(errno));
        pid = 0;
      }
      
      // did the child fail before/at exec?
      if(pid != 0)
      {
        int child_error = 0;
        ssize_t read_size = 0;
        do { read_size = read(error_pipe[0], &child_error, sizeof(child_error)); } while(read_size < 0 && errno == EINTR);
        if(read_size == sizeof(child_error))
        {
          log_infof("mac demon: failed to launch \"%s\": %s\n", argv[0], strerror(child_error));
          int status = 0;
          waitpid(pid, &status, 0);
          pid = 0;
        }
      }
      close(error_pipe[0]);
    }
    
    //- wait for the post-exec stop
    if(pid != 0)
    {
      int status = 0;
      pid_t wait_result = 0;
      do { wait_result = waitpid(pid, &status, WUNTRACED); } while(wait_result < 0 && errno == EINTR);
      if(wait_result != pid || !WIFSTOPPED(status))
      {
        log_infof("mac demon: \"%s\" did not stop after exec\n", argv[0]);
        if(wait_result == pid && !WIFEXITED(status) && !WIFSIGNALED(status))
        {
          kill(pid, SIGKILL);
          waitpid(pid, &status, 0);
        }
        pid = 0;
      }
    }
    
    //- get the task port; switch from ptrace-stop tracing to Mach exceptions
    //
    // NOTE: PT_ATTACHEXC re-attaches w/ signals delivered as exceptions; it
    // sends SIGSTOP, which arrives (as an exception) before the process runs
    // any code of its own, & completes the launch handshake.
    //
    if(pid != 0)
    {
      task_t task = MACH_PORT_NULL;
      kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
      B32 good = (kr == KERN_SUCCESS);
      if(!good)
      {
        mac_dmn_log_task_for_pid_failure(pid, kr);
      }
      MAC_DMN_Entity *process_entity = 0;
      if(good)
      {
        process_entity = mac_dmn_process_alloc(pid, task, MAC_DMN_ProcessState_Launching, 1);
        good = (mac_dmn_process_suspend(&process_entity->process) &&
                mac_dmn_process_install_exception_port(&process_entity->process));
      }
      if(good)
      {
        good = (ptrace(PT_DETACH, pid, (caddr_t)1, 0) == 0 &&
                ptrace(PT_ATTACHEXC, pid, 0, 0) == 0);
        if(!good)
        {
          log_infof("mac demon: re-attaching to %i failed: %s\n", pid, strerror(errno));
        }
      }
      if(!good)
      {
        kill(pid, SIGKILL);
        if(process_entity != 0)
        {
          mac_dmn_process_restore_exception_ports(&process_entity->process);
          mac_dmn_process_release(process_entity, 0);
        }
        else if(task != MACH_PORT_NULL)
        {
          mach_port_deallocate(mach_task_self(), task);
        }
        int status = 0;
        mac_dmn_reap(pid, 2000, &status);
        pid = 0;
      }
    }
  }
  scratch_end(scratch);
  return (U32)pid;
}

internal B32
dmn_ctrl_attach(DMN_CtrlCtx *ctx, U32 pid)
{
  B32 result = 0;
  task_t task = MACH_PORT_NULL;
  kern_return_t kr = task_for_pid(mach_task_self(), (pid_t)pid, &task);
  if(kr != KERN_SUCCESS)
  {
    mac_dmn_log_task_for_pid_failure((pid_t)pid, kr);
  }
  else
  {
    MAC_DMN_Entity *process_entity = mac_dmn_process_alloc((pid_t)pid, task, MAC_DMN_ProcessState_Attaching, 0);
    B32 good = mac_dmn_process_install_exception_port(&process_entity->process);
    if(good)
    {
      good = (ptrace(PT_ATTACHEXC, (pid_t)pid, 0, 0) == 0);
      if(!good)
      {
        log_infof("mac demon: ptrace(PT_ATTACHEXC, %u) failed: %s\n", pid, strerror(errno));
      }
    }
    if(!good)
    {
      mac_dmn_process_restore_exception_ports(&process_entity->process);
      mac_dmn_process_release(process_entity, 0);
    }
    result = good;
  }
  return result;
}

internal B32
dmn_ctrl_kill(DMN_CtrlCtx *ctx, DMN_Handle process_handle, U32 exit_code)
{
  B32 result = 0;
  mutex_take(mac_dmn_state->halter_mutex);
  MAC_DMN_Entity *process_entity = mac_dmn_entity_from_handle(process_handle, MAC_DMN_EntityKind_Process);
  if(process_entity != 0 && !process_entity->process.is_dead)
  {
    MAC_DMN_Process *process = &process_entity->process;
    result = (kill(process->pid, SIGKILL) == 0);
    if(result)
    {
      // NOTE: let it run to its termination; the exit is reported once the
      // task port dies.
      mac_dmn_process_resume(process);
    }
  }
  mutex_drop(mac_dmn_state->halter_mutex);
  return result;
}

internal B32
dmn_ctrl_detach(DMN_CtrlCtx *ctx, DMN_Handle process_handle)
{
  B32 result = 0;
  mutex_take(mac_dmn_state->halter_mutex);
  MAC_DMN_Entity *process_entity = mac_dmn_entity_from_handle(process_handle, MAC_DMN_EntityKind_Process);
  if(process_entity != 0 && !process_entity->process.is_dead)
  {
    MAC_DMN_Process *process = &process_entity->process;

    //- flush register changes; give back all threads blocked in exceptions
    // (w/ the reply they'd get if the debugger simply continued)
    for(MAC_DMN_Entity *t = process->first_thread; t != 0; t = t->thread.next)
    {
      mac_dmn_thread_flush_regs(&t->thread);
      mac_dmn_thread_reply_held(&t->thread, 0);
    }

    //- PT_DETACH requires the process to be stopped (in a signal stop), so
    // stop it w/ SIGSTOP, & wait for that to arrive as an exception.
    Temp scratch = scratch_begin(0, 0);
    typedef struct HeldReply HeldReply;
    struct HeldReply { HeldReply *next; mach_port_t reply_port; B32 is_hw; };
    HeldReply *first_held = 0;
    B32 got_stop = 0;
    B32 died = 0;
    kill(process->pid, SIGSTOP);
    mac_dmn_process_resume(process);
    U64 deadline_us = now_time_us() + 5000000;
    MAC_DMN_RecvBuffer *buf = push_array(scratch.arena, MAC_DMN_RecvBuffer, 1);
    for(;!got_stop && !died;)
    {
      U64 now = now_time_us();
      if(now >= deadline_us) { break; }
      kern_return_t kr = mac_dmn_recv(buf, (deadline_us - now)/1000 + 1);
      if(kr == MACH_RCV_TIMED_OUT) { break; }
      if(kr != KERN_SUCCESS) { continue; }
      mach_msg_header_t *head = &buf->head;
      if(head->msgh_local_port == mac_dmn_state->exception_port &&
         head->msgh_id == MAC_DMN_MACH_EXCEPTION_RAISE_MSG_ID &&
         (head->msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
         buf->exc.task.name == process->task)
      {
        MAC_DMN_ExcRequest *req = &buf->exc;
        B32 is_stop = (req->exception == EXC_SOFTWARE && req->code_count >= 2 &&
                       req->code[0] == EXC_SOFT_SIGNAL && req->code[1] == SIGSTOP);
        HeldReply *h = push_array(scratch.arena, HeldReply, 1);
        h->reply_port = head->msgh_remote_port;
        h->is_hw = (req->exception != EXC_SOFTWARE);
        SLLStackPush(first_held, h);
        mach_port_deallocate(mach_task_self(), req->thread.name);
        mach_port_deallocate(mach_task_self(), req->task.name);
        got_stop = is_stop;
      }
      else
      {
        // NOTE: everything else (other processes, notifications, halts) is
        // deferred to the next run.
        MAC_DMN_Msg *msg = mac_dmn_msg_from_recv_buffer(scratch.arena, buf);
        if(msg != 0)
        {
          if(msg->kind == MAC_DMN_MsgKind_DeadName && msg->dead_name == process->task)
          {
            died = 1;
          }
          mac_dmn_state_defer_msg(msg);
        }
      }
    }

    //- detach
    if(got_stop && !died)
    {
      mac_dmn_process_suspend(process);
      mac_dmn_process_restore_exception_ports(process);
      result = (ptrace(PT_DETACH, process->pid, (caddr_t)1, 0) == 0);
      if(!result)
      {
        log_infof("mac demon: ptrace(PT_DETACH, %i) failed: %s\n", process->pid, strerror(errno));
      }
    }

    //- release exceptions caught while detaching
    for(HeldReply *h = first_held; h != 0; h = h->next)
    {
      mac_dmn_reply_exception(h->reply_port, h->is_hw ? KERN_FAILURE : KERN_SUCCESS);
    }
    if(got_stop && !died)
    {
      mac_dmn_process_resume(process);
    }

    //- on success, move the process to the detached list (reported as an
    // exit on the next run); on failure, re-install our exception port & stop
    if(result)
    {
      DLLRemove_NP(mac_dmn_state->first_process, mac_dmn_state->last_process, process_entity, process.next, process.prev);
      mac_dmn_state->process_count -= 1;
      DLLPushBack_NP(mac_dmn_state->first_detached_process, mac_dmn_state->last_detached_process, process_entity, process.next, process.prev);
    }
    else if(!died)
    {
      if(got_stop)
      {
        mac_dmn_process_install_exception_port(process);
      }
      mac_dmn_process_suspend(process);
    }
    scratch_end(scratch);
  }
  mutex_drop(mac_dmn_state->halter_mutex);
  return result;
}

internal B32
mac_dmn_entity_is_live_process(MAC_DMN_Entity *entity)
{
  return (entity != 0 && entity->kind == MAC_DMN_EntityKind_Process && !entity->process.is_dead);
}

internal DMN_EventList
dmn_ctrl_run(Arena *arena, DMN_CtrlCtx *ctx, DMN_RunCtrls *ctrls)
{
  Temp scratch = scratch_begin(&arena, 1);
  DMN_EventList events = {0};
  
  //////////////////////////////
  //- prevent halts from racing w/ debuggee state changes; they are
  // re-allowed while waiting for messages.
  //
  mutex_take(mac_dmn_state->halter_mutex);
  
  //////////////////////////////
  //- detached processes -> report as exited
  //
  for(MAC_DMN_Entity *p = mac_dmn_state->first_detached_process, *next = 0; p != 0; p = next)
  {
    next = p->process.next;
    mac_dmn_push_process_exit_events(arena, &events, p);
    mac_dmn_process_release(p, 1);
  }
  
  //////////////////////////////
  //- run until we have events
  //
  for(;events.count == 0 && mac_dmn_state->process_count > 0;)
  {
    Temp iter_temp = temp_begin(scratch.arena);
    MAC_DMN_RunCtx run = {0};
    run.ctrls = ctrls;
    
    ////////////////////////////
    //- gather software traps (user traps + dyld notifier traps)
    //
    {
      U64 cap = ctrls->traps.trap_count;
      for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
      {
        cap += p->process.notifier_count;
      }
      run.written_traps.v = push_array(iter_temp.arena, MAC_DMN_WrittenTrap, cap);
      for(DMN_TrapChunkNode *n = ctrls->traps.first; n != 0; n = n->next)
      {
        for(U64 idx = 0; idx < n->count; idx += 1)
        {
          DMN_Trap *trap = &n->v[idx];
          MAC_DMN_Entity *process = mac_dmn_entity_from_handle(trap->process, MAC_DMN_EntityKind_Process);
          if(trap->flags == 0 && mac_dmn_entity_is_live_process(process) && run.written_traps.count < cap)
          {
            MAC_DMN_WrittenTrap *wt = &run.written_traps.v[run.written_traps.count];
            wt->process = process;
            wt->vaddr = trap->vaddr;
            run.written_traps.count += 1;
          }
        }
      }
      for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
      {
        if(p->process.state != MAC_DMN_ProcessState_Normal || p->process.is_dead) { continue; }
        for(U64 idx = 0; idx < p->process.notifier_count && run.written_traps.count < cap; idx += 1)
        {
          MAC_DMN_WrittenTrap *wt = &run.written_traps.v[run.written_traps.count];
          wt->process = p;
          wt->vaddr = p->process.notifier_vaddrs[idx];
          run.written_traps.count += 1;
        }
      }
    }
    
    ////////////////////////////
    //- read original bytes at all trap locations, then write all traps
    //
    // NOTE: all reads happen before any writes, so duplicate traps at one
    // address all record the original byte.
    //
    {
      B32 *readable = push_array(iter_temp.arena, B32, run.written_traps.count);
      for(U64 idx = 0; idx < run.written_traps.count; idx += 1)
      {
        MAC_DMN_WrittenTrap *wt = &run.written_traps.v[idx];
        wt->og_byte = MAC_DMN_INT3;
        readable[idx] = (mac_dmn_read(wt->process->process.task, r1u64(wt->vaddr, wt->vaddr+1), &wt->og_byte) == 1);
      }
      for(U64 idx = 0; idx < run.written_traps.count; idx += 1)
      {
        MAC_DMN_WrittenTrap *wt = &run.written_traps.v[idx];
        if(readable[idx])
        {
          U8 int3 = MAC_DMN_INT3;
          wt->written = mac_dmn_write(wt->process->process.task, r1u64(wt->vaddr, wt->vaddr+1), &int3);
        }
      }
    }
    
    ////////////////////////////
    //- hardware (flagged) traps -> debug registers
    //
    {
      for(DMN_TrapChunkNode *n = ctrls->traps.first; n != 0; n = n->next)
      {
        for(U64 idx = 0; idx < n->count; idx += 1)
        {
          DMN_Trap *trap = &n->v[idx];
          if(trap->flags == 0) { continue; }
          MAC_DMN_Entity *process = mac_dmn_entity_from_handle(trap->process, MAC_DMN_EntityKind_Process);
          if(!mac_dmn_entity_is_live_process(process)) { continue; }
          MAC_DMN_HwTrapSlots *slots = 0;
          for(MAC_DMN_HwTrapSlots *s = run.first_hw_slots; s != 0; s = s->next)
          {
            if(s->process == process) { slots = s; break; }
          }
          if(slots == 0)
          {
            slots = push_array(iter_temp.arena, MAC_DMN_HwTrapSlots, 1);
            slots->process = process;
            SLLStackPush(run.first_hw_slots, slots);
          }
          B32 dup = 0;
          for(U64 s_idx = 0; s_idx < slots->count; s_idx += 1)
          {
            dup |= (slots->traps[s_idx].id == trap->id);
          }
          if(!dup && slots->count < ArrayCount(slots->traps))
          {
            slots->traps[slots->count] = *trap;
            slots->count += 1;
          }
        }
      }
      for(MAC_DMN_HwTrapSlots *s = run.first_hw_slots; s != 0; s = s->next)
      {
        x86_debug_state64_t ds = {0};
        U64 *addr_regs[4] = {&ds.__dr0, &ds.__dr1, &ds.__dr2, &ds.__dr3};
        U64 dr7 = (1ull << 8) | (1ull << 9) | (1ull << 10); // LE, GE, reserved-1
        for(U64 slot = 0; slot < s->count; slot += 1)
        {
          DMN_Trap *trap = &s->traps[slot];
          addr_regs[slot][0] = trap->vaddr;
          dr7 |= (1ull << (slot*2)); // local enable
          U64 rw = 0;
          switch(trap->flags)
          {
            default:{rw = 0;}break; // execute
            case DMN_TrapFlag_BreakOnWrite:
            case DMN_TrapFlag_BreakOnWrite|DMN_TrapFlag_BreakOnExecute:{rw = 1;}break;
            case DMN_TrapFlag_BreakOnRead:
            case DMN_TrapFlag_BreakOnRead|DMN_TrapFlag_BreakOnWrite:
            case DMN_TrapFlag_BreakOnRead|DMN_TrapFlag_BreakOnWrite|DMN_TrapFlag_BreakOnExecute:{rw = 3;}break;
          }
          U64 len = 0;
          switch(trap->size)
          {
            default:
            case 1:{len = 0;}break;
            case 2:{len = 1;}break;
            case 4:{len = 3;}break;
            case 8:{len = 2;}break;
          }
          if(rw == 0) { len = 0; }
          dr7 |= (rw << (16 + slot*4));
          dr7 |= (len << (18 + slot*4));
        }
        ds.__dr7 = dr7;
        for(MAC_DMN_Entity *t = s->process->process.first_thread; t != 0; t = t->thread.next)
        {
          if(thread_set_state(t->thread.port, x86_DEBUG_STATE64, (thread_state_t)&ds, x86_DEBUG_STATE64_COUNT) == KERN_SUCCESS)
          {
            t->thread.debug_state_set = 1;
          }
        }
      }
    }
    
    ////////////////////////////
    //- single-stepping -> set trap flag
    //
    MAC_DMN_Entity *single_step_thread = mac_dmn_entity_from_handle(ctrls->single_step_thread, MAC_DMN_EntityKind_Thread);
    if(single_step_thread != 0 && single_step_thread->thread.regs_valid)
    {
      single_step_thread->thread.regs.rflags |= MAC_DMN_X64_TRAP_FLAG;
      single_step_thread->thread.regs_dirty = 1;
      single_step_thread->thread.single_step_set = 1;
    }
    
    ////////////////////////////
    //- write all modified registers
    //
    for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
    {
      for(MAC_DMN_Entity *t = p->process.first_thread; t != 0; t = t->thread.next)
      {
        mac_dmn_thread_flush_regs(&t->thread);
      }
    }
    
    ////////////////////////////
    //- freeze / unfreeze threads, reply to held exceptions, resume
    //
    for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
    {
      MAC_DMN_Process *process = &p->process;
      if(process->is_dead) { continue; }
      
      // determine if this process is frozen
      B32 process_is_frozen = 0;
      if(ctrls->run_entities_are_processes)
      {
        DMN_Handle process_handle = mac_dmn_handle_from_entity(p);
        for(U64 idx = 0; idx < ctrls->run_entity_count; idx += 1)
        {
          if(dmn_handle_match(ctrls->run_entities[idx], process_handle))
          {
            process_is_frozen = 1;
            break;
          }
        }
      }
      
      // schedule threads
      U64 unfrozen_count = 0;
      for(MAC_DMN_Entity *t = process->first_thread; t != 0; t = t->thread.next)
      {
        MAC_DMN_Thread *thread = &t->thread;
        B32 is_frozen = 0;
        if(single_step_thread == 0)
        {
          if(ctrls->run_entities_are_processes)
          {
            is_frozen = process_is_frozen;
          }
          else
          {
            DMN_Handle thread_handle = mac_dmn_handle_from_entity(t);
            for(U64 idx = 0; idx < ctrls->run_entity_count; idx += 1)
            {
              if(dmn_handle_match(ctrls->run_entities[idx], thread_handle))
              {
                is_frozen = 1;
                break;
              }
            }
          }
          if(ctrls->run_entities_are_unfrozen)
          {
            is_frozen ^= 1;
          }
        }
        else
        {
          is_frozen = (t != single_step_thread);
        }
        if(is_frozen)
        {
          if(thread_suspend(thread->port) == KERN_SUCCESS)
          {
            thread->is_frozen_by_us = 1;
          }
        }
        else
        {
          unfrozen_count += 1;
          mac_dmn_thread_reply_held(thread, ctrls->ignore_previous_exception);
        }
      }
      
      // resume the task, if anything in it may run (processes which
      // haven't been announced yet have no known threads, & always run)
      B32 should_run = (unfrozen_count != 0 || process->thread_count == 0);
      if(should_run)
      {
        mac_dmn_process_resume(process);
      }
    }
    
    ////////////////////////////
    //- wait for messages; stop the world on the first, then drain
    //
    MAC_DMN_MsgList msgs = {0};
    B32 world_stopped = 0;
    B32 recv_error = 0;
    {
      // messages deferred from outside of runs
      for(MAC_DMN_Msg *m = mac_dmn_state->deferred_msgs.first; m != 0; m = m->next)
      {
        if(m->kind == MAC_DMN_MsgKind_Wakeup && !mac_dmn_state->halt_requested) { continue; }
        MAC_DMN_Msg *copy = push_array(iter_temp.arena, MAC_DMN_Msg, 1);
        MemoryCopyStruct(copy, m);
        copy->next = 0;
        SLLQueuePush(msgs.first, msgs.last, copy);
        msgs.count += 1;
      }
      MemoryZeroStruct(&mac_dmn_state->deferred_msgs);
      arena_clear(mac_dmn_state->deferred_arena);
      if(msgs.count != 0)
      {
        mac_dmn_suspend_all();
        world_stopped = 1;
      }
      
      // receive
      MAC_DMN_RecvBuffer *buf = push_array(iter_temp.arena, MAC_DMN_RecvBuffer, 1);
      for(;;)
      {
        kern_return_t kr = KERN_SUCCESS;
        if(world_stopped)
        {
          kr = mac_dmn_recv(buf, 0);
        }
        else
        {
          mutex_drop(mac_dmn_state->halter_mutex);
          kr = mac_dmn_recv(buf, max_U64);
          mutex_take(mac_dmn_state->halter_mutex);
        }
        if(kr == MACH_RCV_TIMED_OUT) { break; }
        if(kr == MACH_RCV_INTERRUPTED || kr == MACH_RCV_TOO_LARGE) { continue; }
        if(kr != KERN_SUCCESS)
        {
          log_infof("mac demon: mach_msg receive failed (%s, 0x%x)\n", mach_error_string(kr), kr);
          recv_error = 1;
          break;
        }
        MAC_DMN_Msg *msg = mac_dmn_msg_from_recv_buffer(iter_temp.arena, buf);
        if(msg == 0) { continue; }
        if(msg->kind == MAC_DMN_MsgKind_Wakeup && !mac_dmn_state->halt_requested) { continue; }
        SLLQueuePush(msgs.first, msgs.last, msg);
        msgs.count += 1;
        if(!world_stopped)
        {
          mac_dmn_suspend_all();
          world_stopped = 1;
        }
      }
      if(!world_stopped)
      {
        mac_dmn_suspend_all();
        world_stopped = 1;
      }
    }
    
    ////////////////////////////
    //- dead tasks -> mark processes as dead
    //
    for(MAC_DMN_Msg *m = msgs.first; m != 0; m = m->next)
    {
      if(m->kind == MAC_DMN_MsgKind_DeadName)
      {
        MAC_DMN_Entity *p = mac_dmn_process_from_task(m->dead_name);
        if(p != 0)
        {
          p->process.is_dead = 1;
        }
      }
    }
    
    ////////////////////////////
    //- refresh thread lists & read all registers
    //
    for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
    {
      if(p->process.is_dead) { continue; }
      mac_dmn_refresh_threads(arena, &events, p, p->process.state == MAC_DMN_ProcessState_Normal);
      for(MAC_DMN_Entity *t = p->process.first_thread; t != 0; t = t->thread.next)
      {
        mac_dmn_thread_read_regs(&t->thread);
      }
    }
    
    ////////////////////////////
    //- exceptions -> events
    //
    for(MAC_DMN_Msg *m = msgs.first; m != 0; m = m->next)
    {
      if(m->kind == MAC_DMN_MsgKind_Exception)
      {
        mac_dmn_handle_exception(arena, &events, &run, &m->exc);
      }
    }
    
    ////////////////////////////
    //- halts
    //
    {
      B32 got_wakeup = 0;
      for(MAC_DMN_Msg *m = msgs.first; m != 0; m = m->next)
      {
        got_wakeup |= (m->kind == MAC_DMN_MsgKind_Wakeup);
      }
      if(got_wakeup && mac_dmn_state->halt_requested)
      {
        DMN_Event *e = dmn_event_list_push(arena, &events);
        e->kind      = DMN_EventKind_Halt;
        e->code      = mac_dmn_state->halt_code;
        e->user_data = mac_dmn_state->halt_user_data;
        mac_dmn_state->halt_requested = 0;
        mac_dmn_state->halt_code = 0;
        mac_dmn_state->halt_user_data = 0;
      }
    }
    
    ////////////////////////////
    //- processes w/o a dyld notifier -> refresh module lists at every stop
    //
    for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
    {
      MAC_DMN_Process *process = &p->process;
      if(!process->is_dead && process->state == MAC_DMN_ProcessState_Normal && process->notifier_searched && process->notifier_count == 0)
      {
        // NOTE: once dyld has initialized, its image info struct points at
        // its notification function; use it if the symbol search failed.
        {
          task_dyld_info_data_t dyld_info = {0};
          mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
          struct { U32 version; U32 info_array_count; U64 info_array; U64 notification; } all_image_infos = {0};
          if(task_info(process->task, TASK_DYLD_INFO, (task_info_t)&dyld_info, &count) == KERN_SUCCESS &&
             dyld_info.all_image_info_addr != 0 &&
             mac_dmn_read_struct(process->task, dyld_info.all_image_info_addr, &all_image_infos) &&
             all_image_infos.notification != 0)
          {
            process->notifier_vaddrs[0] = all_image_infos.notification;
            process->notifier_count = 1;
          }
        }
        MAC_DMN_ImageCandidateList candidates = {0};
        if(mac_dmn_image_candidates_from_dyld(iter_temp.arena, &candidates, process))
        {
          mac_dmn_sync_modules(arena, &events, p, process->first_thread, &candidates, 1);
        }
      }
    }
    
    ////////////////////////////
    //- unset all traps - restore original bytes
    //
    for(U64 idx = 0; idx < run.written_traps.count; idx += 1)
    {
      MAC_DMN_WrittenTrap *wt = &run.written_traps.v[idx];
      if(wt->written && wt->og_byte != MAC_DMN_INT3 && mac_dmn_entity_is_live_process(wt->process))
      {
        mac_dmn_write(wt->process->process.task, r1u64(wt->vaddr, wt->vaddr+1), &wt->og_byte);
      }
    }
    
    ////////////////////////////
    //- clear debug registers, trap flags, & thread freezes
    //
    for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
    {
      for(MAC_DMN_Entity *t = p->process.first_thread; t != 0; t = t->thread.next)
      {
        MAC_DMN_Thread *thread = &t->thread;
        if(thread->debug_state_set)
        {
          x86_debug_state64_t ds = {0};
          thread_set_state(thread->port, x86_DEBUG_STATE64, (thread_state_t)&ds, x86_DEBUG_STATE64_COUNT);
          thread->debug_state_set = 0;
          thread->regs.dr0 = thread->regs.dr1 = thread->regs.dr2 = thread->regs.dr3 = 0;
          thread->regs.dr6 = thread->regs.dr7 = 0;
        }
        if(thread->single_step_set)
        {
          if(thread->regs_valid && (thread->regs.rflags & MAC_DMN_X64_TRAP_FLAG))
          {
            thread->regs.rflags &= ~MAC_DMN_X64_TRAP_FLAG;
            thread->regs_dirty = 1;
          }
          thread->single_step_set = 0;
        }
        if(thread->is_frozen_by_us)
        {
          thread_resume(thread->port);
          thread->is_frozen_by_us = 0;
        }
      }
    }
    
    ////////////////////////////
    //- dead processes -> exit events, release
    //
    for(MAC_DMN_Entity *p = mac_dmn_state->first_process, *next = 0; p != 0; p = next)
    {
      next = p->process.next;
      if(p->process.is_dead)
      {
        int status = 0;
        if(mac_dmn_reap(p->process.pid, 1000, &status))
        {
          if(WIFEXITED(status))
          {
            p->process.exit_code = (U32)WEXITSTATUS(status);
          }
          else if(WIFSIGNALED(status))
          {
            p->process.exit_code = 128 + (U32)WTERMSIG(status);
          }
        }
        mac_dmn_push_process_exit_events(arena, &events, p);
        mac_dmn_process_release(p, 0);
      }
    }
    
    ////////////////////////////
    //- remember this run's software traps (for ghost trap detection)
    //
    {
      arena_clear(mac_dmn_state->prev_traps_arena);
      mac_dmn_state->prev_traps = push_array(mac_dmn_state->prev_traps_arena, MAC_DMN_PrevTrap, run.written_traps.count);
      mac_dmn_state->prev_traps_count = 0;
      for(U64 idx = 0; idx < run.written_traps.count; idx += 1)
      {
        MAC_DMN_WrittenTrap *wt = &run.written_traps.v[idx];
        if(wt->written && wt->og_byte != MAC_DMN_INT3 && wt->process->kind == MAC_DMN_EntityKind_Process)
        {
          MAC_DMN_PrevTrap *pt = &mac_dmn_state->prev_traps[mac_dmn_state->prev_traps_count];
          pt->process = mac_dmn_handle_from_entity(wt->process);
          pt->vaddr = wt->vaddr;
          mac_dmn_state->prev_traps_count += 1;
        }
      }
    }
    
    temp_end(iter_temp);
    
    ////////////////////////////
    //- receive errors -> bail
    //
    if(recv_error)
    {
      DMN_Event *e = dmn_event_list_push(arena, &events);
      e->kind = DMN_EventKind_Error;
      e->error_kind = DMN_ErrorKind_UnexpectedFailure;
      break;
    }
  }
  
  //////////////////////////////
  //- no events, no processes -> not attached
  //
  if(events.count == 0 && mac_dmn_state->process_count == 0)
  {
    DMN_Event *e = dmn_event_list_push(arena, &events);
    e->kind = DMN_EventKind_Error;
    e->error_kind = DMN_ErrorKind_NotAttached;
  }
  
  //////////////////////////////
  //- gather thread names
  //
  for(MAC_DMN_Entity *p = mac_dmn_state->first_process; p != 0; p = p->process.next)
  {
    if(p->process.state != MAC_DMN_ProcessState_Normal || p->process.is_dead) { continue; }
    for(MAC_DMN_Entity *t = p->process.first_thread; t != 0; t = t->thread.next)
    {
      MAC_DMN_Thread *thread = &t->thread;
      if(thread->last_name_hash != 0 && thread->name_gather_time_us + 1000000 > now_time_us())
      {
        continue;
      }
      thread->name_gather_time_us = now_time_us();
      thread_extended_info_data_t ext = {0};
      mach_msg_type_number_t count = THREAD_EXTENDED_INFO_COUNT;
      if(thread_info(thread->port, THREAD_EXTENDED_INFO, (thread_info_t)&ext, &count) == KERN_SUCCESS)
      {
        String8 name = str8_cstring_capped(ext.pth_name, ext.pth_name + sizeof(ext.pth_name));
        U64 hash = u64_hash_from_str8(name);
        if(name.size != 0 && hash != thread->last_name_hash)
        {
          DMN_Event *e = dmn_event_list_push(arena, &events);
          e->kind    = DMN_EventKind_SetThreadName;
          e->process = mac_dmn_handle_from_entity(p);
          e->thread  = mac_dmn_handle_from_entity(t);
          e->string  = push_str8_copy(arena, name);
        }
        thread->last_name_hash = (hash != 0 ? hash : 1);
      }
    }
  }
  
  mutex_drop(mac_dmn_state->halter_mutex);
  scratch_end(scratch);
  return events;
}

////////////////////////////////
//~ @dmn_os_hooks Halting (Implemented Per-OS)

internal B32
dmn_halt(U64 code, U64 user_data)
{
  B32 result = 0;
  MutexScope(mac_dmn_state->halter_mutex)
  {
    if(mac_dmn_state->process_count != 0)
    {
      result = 1;
      mac_dmn_state->halt_requested = 1;
      mac_dmn_state->halt_code = code;
      mac_dmn_state->halt_user_data = user_data;
      mach_msg_header_t msg = {0};
      msg.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
      msg.msgh_size        = sizeof(msg);
      msg.msgh_remote_port = mac_dmn_state->wakeup_port;
      msg.msgh_local_port  = MACH_PORT_NULL;
      msg.msgh_id          = MAC_DMN_WAKEUP_MSG_ID;
      mach_msg(&msg, MACH_SEND_MSG|MACH_SEND_TIMEOUT, sizeof(msg), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
    }
  }
  return result;
}

////////////////////////////////
//~ @dmn_os_hooks Introspection Functions (Implemented Per-OS)

//- non-blocking-control-thread access barriers

internal B32
dmn_access_open(void)
{
  B32 result = 0;
  if(mac_dmn_ctrl_thread)
  {
    result = 1;
  }
  else
  {
    mutex_take(mac_dmn_state->access_mutex);
    result = !mac_dmn_state->access_run_state;
  }
  return result;
}

internal void
dmn_access_close(void)
{
  if(!mac_dmn_ctrl_thread)
  {
    mutex_drop(mac_dmn_state->access_mutex);
  }
}

//- processes

internal vm_prot_t
mac_dmn_vm_prot_from_access_flags(AccessFlags flags)
{
  vm_prot_t prot = VM_PROT_NONE;
  if(flags & AccessFlag_Read)    { prot |= VM_PROT_READ; }
  if(flags & AccessFlag_Write)   { prot |= VM_PROT_WRITE; }
  if(flags & AccessFlag_Execute) { prot |= VM_PROT_EXECUTE; }
  return prot;
}

internal U64
dmn_process_memory_reserve(DMN_Handle process, U64 vaddr, U64 size)
{
  U64 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(process, MAC_DMN_EntityKind_Process);
    if(mac_dmn_entity_is_live_process(entity))
    {
      mach_vm_address_t addr = vaddr;
      int flags = (vaddr != 0) ? VM_FLAGS_FIXED : VM_FLAGS_ANYWHERE;
      if(mach_vm_allocate(entity->process.task, &addr, size, flags) == KERN_SUCCESS)
      {
        result = addr;
      }
    }
  }
  return result;
}

internal void
dmn_process_memory_commit(DMN_Handle process, U64 vaddr, U64 size)
{
  // NOTE: Mach memory is committed on first touch.
}

internal void
dmn_process_memory_decommit(DMN_Handle process, U64 vaddr, U64 size)
{
  DMN_AccessScope
  {
    MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(process, MAC_DMN_EntityKind_Process);
    if(mac_dmn_entity_is_live_process(entity))
    {
      mach_vm_behavior_set(entity->process.task, vaddr, size, VM_BEHAVIOR_REUSABLE);
    }
  }
}

internal void
dmn_process_memory_release(DMN_Handle process, U64 vaddr, U64 size)
{
  DMN_AccessScope
  {
    MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(process, MAC_DMN_EntityKind_Process);
    if(mac_dmn_entity_is_live_process(entity))
    {
      mach_vm_deallocate(entity->process.task, vaddr, size);
    }
  }
}

internal void
dmn_process_memory_protect(DMN_Handle process, U64 vaddr, U64 size, AccessFlags flags)
{
  DMN_AccessScope
  {
    MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(process, MAC_DMN_EntityKind_Process);
    if(mac_dmn_entity_is_live_process(entity))
    {
      mach_vm_protect(entity->process.task, vaddr, size, 0, mac_dmn_vm_prot_from_access_flags(flags));
    }
  }
}

internal U64
dmn_process_read(DMN_Handle process, Rng1U64 range, void *dst)
{
  U64 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(process, MAC_DMN_EntityKind_Process);
    if(mac_dmn_entity_is_live_process(entity))
    {
      result = mac_dmn_read(entity->process.task, range, dst);
    }
  }
  return result;
}

internal B32
dmn_process_write(DMN_Handle process, Rng1U64 range, void *src)
{
  B32 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(process, MAC_DMN_EntityKind_Process);
    if(mac_dmn_entity_is_live_process(entity))
    {
      result = mac_dmn_write(entity->process.task, range, src);
    }
  }
  return result;
}

//- threads

internal B32
dmn_thread_read_reg_block(DMN_Handle handle, void *reg_block)
{
  B32 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(handle, MAC_DMN_EntityKind_Thread);
    if(entity != 0 && entity->thread.regs_valid)
    {
      MemoryCopy(reg_block, &entity->thread.regs, sizeof(X64_RegBlock));
      result = 1;
    }
  }
  return result;
}

internal B32
dmn_thread_write_reg_block(DMN_Handle handle, void *reg_block)
{
  B32 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(handle, MAC_DMN_EntityKind_Thread);
    if(entity != 0 && entity->thread.regs_valid)
    {
      MemoryCopy(&entity->thread.regs, reg_block, sizeof(X64_RegBlock));
      entity->thread.regs_dirty = 1;
      result = 1;
    }
  }
  return result;
}

internal B32
dmn_thread_get_module_tls_vaddr(DMN_Handle thread, DMN_Handle module, U64 *vaddr_out)
{
  // TODO: Mach-O thread-local variables (TLV descriptors, resolved lazily by
  // dyld) are not yet supported.
  return 0;
}

//- system process listing

typedef struct MAC_DMN_ProcessIterState MAC_DMN_ProcessIterState;
struct MAC_DMN_ProcessIterState
{
  U64 count;
  U64 idx;
  pid_t pids[1];
};

internal void
dmn_process_iter_begin(DMN_ProcessIter *iter)
{
  MemoryZeroStruct(iter);
  int needed = proc_listallpids(0, 0);
  if(needed > 0)
  {
    U64 cap = (U64)needed + 64;
    MAC_DMN_ProcessIterState *state = (MAC_DMN_ProcessIterState *)malloc(sizeof(MAC_DMN_ProcessIterState) + cap*sizeof(pid_t));
    if(state != 0)
    {
      int count = proc_listallpids(state->pids, (int)(cap*sizeof(pid_t)));
      state->count = (count > 0) ? Min((U64)count, cap) : 0;
      state->idx = 0;
      iter->v[0] = IntFromPtr(state);
    }
  }
}

internal B32
dmn_process_iter_next(Arena *arena, DMN_ProcessIter *iter, DMN_ProcessInfo *info_out)
{
  B32 result = 0;
  MAC_DMN_ProcessIterState *state = (MAC_DMN_ProcessIterState *)PtrFromInt(iter->v[0]);
  if(state != 0)
  {
    for(;state->idx < state->count && !result;)
    {
      pid_t pid = state->pids[state->idx];
      state->idx += 1;
      if(pid <= 0) { continue; }
      char path[PROC_PIDPATHINFO_MAXSIZE] = {0};
      int len = proc_pidpath(pid, path, sizeof(path));
      String8 name = {0};
      if(len > 0)
      {
        name = str8((U8 *)path, (U64)len);
      }
      else
      {
        char short_name[2*MAXCOMLEN + 1] = {0};
        int short_len = proc_name(pid, short_name, sizeof(short_name));
        name = (short_len > 0) ? str8((U8 *)short_name, (U64)short_len) : str8_lit("(unknown process)");
      }
      info_out->name = push_str8_copy(arena, name);
      info_out->pid = (U32)pid;
      result = 1;
    }
  }
  return result;
}

internal void
dmn_process_iter_end(DMN_ProcessIter *iter)
{
  MAC_DMN_ProcessIterState *state = (MAC_DMN_ProcessIterState *)PtrFromInt(iter->v[0]);
  if(state != 0)
  {
    free(state);
  }
  MemoryZeroStruct(iter);
}
