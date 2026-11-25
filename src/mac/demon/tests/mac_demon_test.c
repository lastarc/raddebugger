// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ macOS Demon Tests
//
// Drives the demon layer directly against `mac_demon_test_target` (see that
// file), checking events, breakpoints, single steps, data breakpoints,
// signals, module loads, unwinding, exceptions, halts, attach/detach & kill.
//
// usage: mac_demon_test <path to mac_demon_test_target>
// (needs task_for_pid rights: run as root, or signed w/ the debugger entitlement)

////////////////////////////////
//~ Build Options

#define BUILD_TITLE "mac_demon_test"
#define BUILD_CONSOLE_INTERFACE 1

////////////////////////////////
//~ Includes

//- [h]
#include "base/base_inc.h"
#include "x64/x64_inc.h"
#include "coff/coff_inc.h"
#include "pe/pe.h"
#include "macho/macho.h"
#include "macho/macho_parse.h"
#include "dwarf/dwarf_inc.h"
#include "eh_frame/eh_frame.h"
#include "arch/arch_inc.h"
#include "unwind/unwind_inc.h"
#include "demon/demon_inc.h"

//- [c]
#include "base/base_inc.c"
#include "x64/x64_inc.c"
#include "coff/coff_inc.c"
#include "pe/pe.c"
#include "macho/macho.c"
#include "macho/macho_parse.c"
#include "dwarf/dwarf_inc.c"
#include "eh_frame/eh_frame.c"
#include "arch/arch_inc.c"
#include "unwind/unwind_inc.c"
#include "demon/demon_inc.c"

#if !OS_MAC
# error mac_demon_test is macOS-only.
#endif

////////////////////////////////
//~ Test State

global U64 t_failures = 0;
global U64 t_passes = 0;
global volatile U64 t_deadline_us = 0;
global Arena *t_arena = 0;
global DMN_CtrlCtx *t_ctx = 0;
global B32 t_in_github_actions = 0;
global char *t_section = "";
global volatile pid_t t_pids[64] = {0};
global volatile U32 t_pid_count = 0;
global char t_last_events[512] = {0};

// NOTE: keeps a short, single-line trail of recent events, for annotations
internal void
t_note_event(String8 string)
{
  U64 cur_len = strlen(t_last_events);
  U64 add_len = Min(string.size, 120);
  if(cur_len + add_len + 3 >= sizeof(t_last_events))
  {
    U64 drop = cur_len + add_len + 3 - sizeof(t_last_events) + 1;
    drop = Min(drop, cur_len);
    memmove(t_last_events, t_last_events + drop, cur_len - drop + 1);
    cur_len -= drop;
  }
  if(cur_len != 0) { strcat(t_last_events, " ; "); cur_len += 3; }
  for(U64 idx = 0; idx < add_len; idx += 1)
  {
    char c = (char)string.str[idx];
    t_last_events[cur_len + idx] = (c == '\n' || c == '\r' || c == '%') ? ' ' : c;
  }
  t_last_events[cur_len + add_len] = 0;
}

// NOTE: on GitHub Actions, failures are also emitted as annotations
#define T_CHECK(cond, ...) do { B32 t__ok = !!(cond); if(t__ok) { t_passes += 1; printf("  [PASS] "); } else { t_failures += 1; printf("  [FAIL] "); } printf(__VA_ARGS__); printf("\n"); if(!t__ok && t_in_github_actions) { printf("::error title=mac_demon_test (%s)::", t_section); printf(__VA_ARGS__); printf(" [recent events: %s]\n", t_last_events); } fflush(stdout); } while(0)

internal void
t_track_pid(pid_t pid)
{
  if(pid > 0 && t_pid_count < ArrayCount(t_pids))
  {
    t_pids[t_pid_count] = pid;
    t_pid_count += 1;
  }
}

// NOTE: debuggees inherit stdout; leftovers would keep CI steps from finishing
internal void
t_kill_all_targets(void)
{
  for(U32 idx = 0; idx < t_pid_count; idx += 1)
  {
    kill(t_pids[idx], SIGKILL);
  }
}

internal void *
t_watchdog_main(void *p)
{
  for(;;)
  {
    usleep(100000);
    U64 deadline = t_deadline_us;
    if(deadline != 0 && now_time_us() > deadline)
    {
      printf("  [FAIL] timed out\n");
      if(t_in_github_actions) { printf("::error title=mac_demon_test (%s)::timed out [recent events: %s]\n", t_section, t_last_events); }
      printf("\n%llu passed, %llu failed (+timeout)\n", t_passes, t_failures + 1);
      fflush(stdout);
      t_kill_all_targets();
      _exit(1);
    }
  }
  return 0;
}

internal void
t_arm_timeout(U64 seconds)
{
  t_deadline_us = now_time_us() + seconds*1000000;
}

////////////////////////////////
//~ Event Queue

global DMN_EventList t_events = {0};
global DMN_EventNode *t_next_event = 0;

internal String8
t_string_from_event(Arena *arena, DMN_Event *e)
{
  String8 kind = (e->kind < DMN_EventKind_COUNT) ? dmn_event_kind_string_table[e->kind] : str8_lit("?");
  return push_str8f(arena, "%S (code: %u, address: 0x%I64x, ip: 0x%I64x, string: \"%S\")", kind, e->code, e->address, e->instruction_pointer, e->string);
}

internal DMN_Event *
t_next(DMN_RunCtrls *ctrls)
{
  if(t_next_event == 0)
  {
    t_events = dmn_ctrl_run(t_arena, t_ctx, ctrls);
    t_next_event = t_events.first;
  }
  DMN_Event *result = 0;
  if(t_next_event != 0)
  {
    result = &t_next_event->v;
    t_next_event = t_next_event->next;
    Temp scratch = scratch_begin(0, 0);
    String8 event_string = t_string_from_event(scratch.arena, result);
    printf("    event: %.*s\n", str8_varg(event_string));
    fflush(stdout);
    t_note_event(event_string);
    scratch_end(scratch);
  }
  return result;
}

// NOTE: pops events until one of `kind` (null on an unexpected terminal event)
internal DMN_Event *
t_expect(DMN_RunCtrls *ctrls, DMN_EventKind kind, B32 allow_exceptions)
{
  DMN_Event *result = 0;
  for(;;)
  {
    DMN_Event *e = t_next(ctrls);
    if(e == 0)
    {
      continue;
    }
    if(e->kind == kind)
    {
      result = e;
      break;
    }
    if(e->kind == DMN_EventKind_Error ||
       (e->kind == DMN_EventKind_ExitProcess && kind != DMN_EventKind_ExitProcess) ||
       (e->kind == DMN_EventKind_Exception && !allow_exceptions))
    {
      break;
    }
  }
  return result;
}

internal void
t_drain(void)
{
  t_next_event = 0;
}

////////////////////////////////
//~ Target Symbols

typedef struct T_Symbols T_Symbols;
struct T_Symbols
{
  U64 target_func;
  U64 target_caller;
  U64 target_caller_end;
  U64 main;
  U64 main_end;
  U64 g_counter;
};

internal T_Symbols
t_symbols_from_path(String8 path)
{
  T_Symbols syms = {0};
  String8 data = data_from_file_path(t_arena, path);
  MACHO_Bin bin = macho_bin_from_data(t_arena, data);
  U64 text_vaddr = macho_text_vaddr_from_bin(&bin);
  if(bin.is_valid && bin.has_symtab)
  {
    // NOTE: voffs, & sorted function addresses (for function end bounds)
    U64 *addrs = push_array(t_arena, U64, bin.nsyms+1);
    U64 addr_count = 0;
    for(U64 idx = 0; idx < bin.nsyms; idx += 1)
    {
      U64 off = bin.symoff + idx*sizeof(MACHO_Nlist64);
      if(off + sizeof(MACHO_Nlist64) > bin.data.size) { break; }
      MACHO_Nlist64 *sym = (MACHO_Nlist64 *)(bin.data.str + off);
      if((sym->n_type & 0xe0) || (sym->n_type & 0x0e) != 0x0e || sym->n_strx >= bin.strsize) { continue; }
      String8 name = str8_cstring_capped(bin.data.str + bin.stroff + sym->n_strx, bin.data.str + bin.stroff + bin.strsize);
      U64 voff = sym->n_value - text_vaddr;
      addrs[addr_count] = voff;
      addr_count += 1;
      if(str8_match(name, str8_lit("_target_func"), 0))   { syms.target_func = voff; }
      if(str8_match(name, str8_lit("_target_caller"), 0)) { syms.target_caller = voff; }
      if(str8_match(name, str8_lit("_main"), 0))          { syms.main = voff; }
      if(str8_match(name, str8_lit("_g_counter"), 0))     { syms.g_counter = voff; }
    }
    U64 caller_end = max_U64;
    U64 main_end = max_U64;
    for(U64 idx = 0; idx < addr_count; idx += 1)
    {
      if(addrs[idx] > syms.target_caller && addrs[idx] < caller_end) { caller_end = addrs[idx]; }
      if(addrs[idx] > syms.main && addrs[idx] < main_end) { main_end = addrs[idx]; }
    }
    syms.target_caller_end = caller_end;
    syms.main_end = main_end;
  }
  return syms;
}

////////////////////////////////
//~ Unwinding

typedef struct T_Module T_Module;
struct T_Module
{
  U64 base;
  U64 size;
  MACHO_UWND_ModuleUnwindInfo unwind_info;
};

internal T_Module
t_module_from_event(DMN_Handle process, DMN_Event *e)
{
  T_Module m = {0};
  m.base = e->address;
  m.size = e->size;
  m.unwind_info.unwind_info_vaddr_range = e->module_info->macho_unwind_info_vaddr_range;
  m.unwind_info.eh_frame_vaddr_range = e->module_info->macho_eh_frame_vaddr_range;
  Rng1U64 fs_range = e->module_info->macho_function_starts_vaddr_range;
  if(dim_1u64(fs_range) != 0)
  {
    String8 data = {push_array(t_arena, U8, dim_1u64(fs_range)), dim_1u64(fs_range)};
    dmn_process_read(process, fs_range, data.str);
    m.unwind_info.function_starts = macho_uwnd_function_starts_from_data(t_arena, data, m.base, &m.unwind_info.function_starts_count);
  }
  return m;
}

internal B32
t_unwind_step(DMN_Handle process, T_Module *module, X64_RegBlock *regs)
{
  Temp scratch = scratch_begin(0, 0);
  MemoryMap map = {0};
  UWND_ModuleInfo module_info = {module->base, &module->unwind_info};
  B32 result = 0;
  for(U64 attempt = 0; attempt < 256; attempt += 1)
  {
    X64_RegBlock regs_copy = *regs;
    U64 cfa = 0;
    UWND_StepResult step = macho_uwnd_step(Arch_x64, &map, &module_info, 0, &regs_copy, &cfa);
    if(step.status == UWND_StepStatus_Good)
    {
      *regs = regs_copy;
      result = 1;
      break;
    }
    if(step.status != UWND_StepStatus_FailedMemoryRead) { break; }
    Rng1U64 range = r1u64(AlignDownPow2(step.missed_read_vaddr_range.min, KB(4)), AlignPow2(step.missed_read_vaddr_range.max, KB(4)));
    U8 *buf = push_array(scratch.arena, U8, dim_1u64(range));
    if(dmn_process_read(process, range, buf) != dim_1u64(range)) { break; }
    memory_map_push(scratch.arena, &map, range, buf);
  }
  scratch_end(scratch);
  return result;
}

////////////////////////////////
//~ Helpers

internal U32
t_launch(String8 target_path, String8 arg)
{
  ProcessLaunchParams params = {0};
  str8_list_push(t_arena, &params.cmd_line, target_path);
  if(arg.size != 0)
  {
    str8_list_push(t_arena, &params.cmd_line, arg);
  }
  params.path = str8_chop_last_slash(target_path);
  params.inherit_env = 1;
  U32 pid = dmn_ctrl_launch(t_ctx, &params);
  t_track_pid((pid_t)pid);
  return pid;
}

typedef struct T_Handshake T_Handshake;
struct T_Handshake
{
  B32 good;
  DMN_Handle process;
  DMN_Handle main_thread;
  T_Module exe;
  B32 saw_exe_first;
  B32 saw_dyld;
};

internal T_Handshake
t_handshake(DMN_RunCtrls *ctrls, String8 target_name)
{
  T_Handshake h = {0};
  DMN_Event *e = t_expect(ctrls, DMN_EventKind_CreateProcess, 0);
  if(e != 0)
  {
    h.process = e->process;
    B32 saw_first_module = 0;
    for(;;)
    {
      e = t_next(ctrls);
      if(e == 0) { continue; }
      if(e->kind == DMN_EventKind_CreateThread && dmn_handle_match(h.main_thread, dmn_handle_zero()))
      {
        h.main_thread = e->thread;
      }
      if(e->kind == DMN_EventKind_LoadModule)
      {
        if(!saw_first_module)
        {
          saw_first_module = 1;
          h.saw_exe_first = str8_match(str8_skip_last_slash(e->string), target_name, 0);
          h.exe = t_module_from_event(h.process, e);
        }
        if(str8_match(str8_skip_last_slash(e->string), str8_lit("dyld"), 0))
        {
          h.saw_dyld = 1;
        }
      }
      if(e->kind == DMN_EventKind_HandshakeComplete)
      {
        h.good = 1;
        break;
      }
      if(e->kind == DMN_EventKind_Error || e->kind == DMN_EventKind_ExitProcess)
      {
        break;
      }
    }
  }
  return h;
}

internal void
t_push_trap(DMN_RunCtrls *ctrls, DMN_Handle process, U64 vaddr, U64 id, DMN_TrapFlags flags, U32 size)
{
  DMN_Trap trap = {process, vaddr, id, flags, size};
  dmn_trap_chunk_list_push(t_arena, &ctrls->traps, 16, &trap);
}

internal void *
t_halter_main(void *p)
{
  usleep(300000);
  dmn_halt(0, 0);
  return 0;
}

////////////////////////////////
//~ Tests

internal void
t_test_launch(String8 target_path)
{
  t_section = "launch";
  printf("launch: breakpoints, stepping, data breakpoints, unwinding, signals, modules, traps, exit\n");
  String8 target_name = str8_skip_last_slash(target_path);
  T_Symbols syms = t_symbols_from_path(target_path);
  T_CHECK(syms.target_func != 0 && syms.target_caller != 0 && syms.main != 0 && syms.g_counter != 0, "target symbols found");

  //- launch & handshake
  t_arm_timeout(30);
  U32 pid = t_launch(target_path, str8_zero());
  T_CHECK(pid != 0, "launch (pid %u)", pid);
  if(pid == 0) { return; }
  DMN_RunCtrls ctrls = {0};
  T_Handshake h = t_handshake(&ctrls, target_name);
  T_CHECK(h.good, "handshake");
  T_CHECK(h.saw_exe_first, "executable is the first module");
  T_CHECK(h.saw_dyld, "dyld is reported");
  if(!h.good) { dmn_ctrl_kill(t_ctx, h.process, 1); return; }
  U64 target_func = h.exe.base + syms.target_func;

  //- breakpoint (in main thread) -> also implies dyld load notifications work
  t_arm_timeout(30);
  MemoryZeroStruct(&ctrls);
  t_push_trap(&ctrls, h.process, target_func, 1, 0, 0);
  DMN_Event *e = t_expect(&ctrls, DMN_EventKind_Breakpoint, 0);
  T_CHECK(e != 0 && e->user_data == 1 && e->instruction_pointer == target_func, "breakpoint at target_func");
  if(e == 0) { dmn_ctrl_kill(t_ctx, h.process, 1); return; }
  DMN_Handle bp_thread = e->thread;
  T_CHECK(dmn_handle_match(bp_thread, h.main_thread), "breakpoint hit by the main thread");

  //- registers & unwinding: target_func -> target_caller -> main
  X64_RegBlock regs = {0};
  T_CHECK(dmn_thread_read_reg_block(bp_thread, &regs) && regs.rip == target_func, "registers (rip = 0x%llx)", regs.rip);
  {
    X64_RegBlock u = regs;
    B32 step1 = t_unwind_step(h.process, &h.exe, &u);
    U64 caller_voff = u.rip - h.exe.base;
    T_CHECK(step1 && syms.target_caller < caller_voff && caller_voff < syms.target_caller_end, "unwind: frame 1 in target_caller (voff 0x%llx)", caller_voff);
    B32 step2 = t_unwind_step(h.process, &h.exe, &u);
    U64 main_voff = u.rip - h.exe.base;
    T_CHECK(step2 && syms.main < main_voff && main_voff < syms.main_end, "unwind: frame 2 in main (voff 0x%llx)", main_voff);
  }

  //- memory read/write
  {
    U8 byte = 0;
    T_CHECK(dmn_process_read(h.process, r1u64(target_func, target_func+1), &byte) == 1 && byte != 0xCC, "read code (breakpoint byte removed between runs)");
    S32 value = 0;
    U64 g_counter = h.exe.base + syms.g_counter;
    T_CHECK(dmn_process_read(h.process, r1u64(g_counter, g_counter+4), &value) == 4 && value == 0, "read data (g_counter = %i)", value);
  }

  //- single step
  t_arm_timeout(30);
  t_drain();
  MemoryZeroStruct(&ctrls);
  ctrls.single_step_thread = bp_thread;
  e = t_expect(&ctrls, DMN_EventKind_SingleStep, 0);
  T_CHECK(e != 0 && e->instruction_pointer != target_func && e->instruction_pointer - target_func < 16, "single step (ip = 0x%llx)", e ? e->instruction_pointer : 0);
  if(e == 0) { dmn_ctrl_kill(t_ctx, h.process, 1); return; }
  T_CHECK(dmn_thread_read_reg_block(bp_thread, &regs) && !(regs.rflags & (1<<8)), "trap flag cleared after the step");

  //- data breakpoint on g_counter
  t_arm_timeout(30);
  t_drain();
  MemoryZeroStruct(&ctrls);
  U64 g_counter = h.exe.base + syms.g_counter;
  t_push_trap(&ctrls, h.process, g_counter, 7, DMN_TrapFlag_BreakOnWrite, 4);
  e = t_expect(&ctrls, DMN_EventKind_Breakpoint, 0);
  T_CHECK(e != 0 && e->user_data == 7 && e->address == g_counter, "data breakpoint on write to g_counter");
  if(e == 0) { dmn_ctrl_kill(t_ctx, h.process, 1); return; }
  {
    S32 value = 0;
    dmn_process_read(h.process, r1u64(g_counter, g_counter+4), &value);
    T_CHECK(value == 1, "data breakpoint reported after the write (g_counter = %i)", value);
  }

  //- breakpoint in a second thread
  t_arm_timeout(30);
  t_drain();
  MemoryZeroStruct(&ctrls);
  t_push_trap(&ctrls, h.process, target_func, 2, 0, 0);
  B32 saw_create_thread = 0;
  for(;;)
  {
    e = t_next(&ctrls);
    if(e == 0) { continue; }
    if(e->kind == DMN_EventKind_CreateThread) { saw_create_thread = 1; }
    if(e->kind == DMN_EventKind_Breakpoint || e->kind == DMN_EventKind_Error || e->kind == DMN_EventKind_ExitProcess || e->kind == DMN_EventKind_Exception) { break; }
  }
  T_CHECK(saw_create_thread, "thread creation reported");
  T_CHECK(e->kind == DMN_EventKind_Breakpoint && e->user_data == 2 && !dmn_handle_match(e->thread, h.main_thread), "breakpoint hit by the second thread");

  //- signal (handled by the target) -> pass it through
  t_arm_timeout(30);
  t_drain();
  MemoryZeroStruct(&ctrls);
  e = t_expect(&ctrls, DMN_EventKind_Exception, 1);
  T_CHECK(e != 0 && e->code == SIGUSR1 && !e->exception_repeated, "SIGUSR1 reported as a first-chance exception (code %u)", e ? e->code : 0);

  //- dlopen -> module load, then the target's own debug trap
  t_arm_timeout(30);
  t_drain();
  MemoryZeroStruct(&ctrls);
  ctrls.ignore_previous_exception = 0;
  B32 saw_libz = 0;
  for(;;)
  {
    e = t_next(&ctrls);
    if(e == 0) { continue; }
    if(e->kind == DMN_EventKind_LoadModule && str8_find_needle(e->string, 0, str8_lit("libz"), 0) < e->string.size) { saw_libz = 1; }
    if(e->kind == DMN_EventKind_Trap || e->kind == DMN_EventKind_Error || e->kind == DMN_EventKind_ExitProcess || e->kind == DMN_EventKind_Exception) { break; }
  }
  T_CHECK(saw_libz, "dlopen'd module reported");
  T_CHECK(e->kind == DMN_EventKind_Trap, "debug trap reported");

  //- exit
  t_arm_timeout(30);
  t_drain();
  MemoryZeroStruct(&ctrls);
  ctrls.ignore_previous_exception = 1;
  e = t_expect(&ctrls, DMN_EventKind_ExitProcess, 0);
  T_CHECK(e != 0 && e->code == 42, "exit code 42 (all target steps ran; got %u)", e ? e->code : 0);

  //- not attached
  t_drain();
  e = t_next(&ctrls);
  T_CHECK(e != 0 && e->kind == DMN_EventKind_Error && e->error_kind == DMN_ErrorKind_NotAttached, "not attached after exit");
  t_drain();
}

internal void
t_test_crash(String8 target_path)
{
  t_section = "crash";
  printf("crash: memory fault, pass-through to the target\n");
  t_arm_timeout(30);
  U32 pid = t_launch(target_path, str8_lit("crash"));
  T_CHECK(pid != 0, "launch (pid %u)", pid);
  if(pid == 0) { return; }
  DMN_RunCtrls ctrls = {0};
  T_Handshake h = t_handshake(&ctrls, str8_skip_last_slash(target_path));
  T_CHECK(h.good, "handshake");
  if(!h.good) { return; }
  t_drain();
  DMN_Event *e = t_expect(&ctrls, DMN_EventKind_Exception, 1);
  T_CHECK(e != 0 && e->code == SIGSEGV && e->address == 0 && e->exception_kind == DMN_ExceptionKind_MemoryWrite, "null write -> SIGSEGV exception (code %u, kind %u)", e ? e->code : 0, e ? e->exception_kind : 0);
  T_CHECK(e != 0 && e->exception_repeated, "unhandled fault is reported as fatal");
  t_drain();
  MemoryZeroStruct(&ctrls);
  ctrls.ignore_previous_exception = 0;
  e = t_expect(&ctrls, DMN_EventKind_ExitProcess, 0);
  T_CHECK(e != 0 && e->code == 128 + SIGSEGV, "passed fault terminates the target (exit code %u)", e ? e->code : 0);
  t_drain();
}

internal void
t_test_halt_kill(String8 target_path)
{
  t_section = "halt_kill";
  printf("halt & kill\n");
  t_arm_timeout(30);
  U32 pid = t_launch(target_path, str8_lit("spin"));
  T_CHECK(pid != 0, "launch (pid %u)", pid);
  if(pid == 0) { return; }
  DMN_RunCtrls ctrls = {0};
  T_Handshake h = t_handshake(&ctrls, str8_skip_last_slash(target_path));
  T_CHECK(h.good, "handshake");
  if(!h.good) { return; }
  t_drain();
  pthread_t halter;
  pthread_create(&halter, 0, t_halter_main, 0);
  DMN_Event *e = t_expect(&ctrls, DMN_EventKind_Halt, 0);
  pthread_join(halter, 0);
  T_CHECK(e != 0, "halt");
  T_CHECK(dmn_ctrl_kill(t_ctx, h.process, 0), "kill");
  t_drain();
  e = t_expect(&ctrls, DMN_EventKind_ExitProcess, 0);
  T_CHECK(e != 0 && e->code == 128 + SIGKILL, "killed target exits (code %u)", e ? e->code : 0);
  t_drain();
}

internal void
t_test_attach_detach(String8 target_path)
{
  t_section = "attach_detach";
  printf("attach & detach\n");
  t_arm_timeout(30);
  Temp scratch = scratch_begin(0, 0);
  char *argv[] = {(char *)push_str8_copy(scratch.arena, target_path).str, "spin", 0};
  pid_t pid = 0;
  int spawn_error = posix_spawn(&pid, argv[0], 0, 0, argv, *_NSGetEnviron());
  t_track_pid(pid);
  T_CHECK(spawn_error == 0, "spawn target");
  if(spawn_error != 0) { scratch_end(scratch); return; }
  usleep(200000);
  T_CHECK(dmn_ctrl_attach(t_ctx, (U32)pid), "attach");
  DMN_RunCtrls ctrls = {0};
  T_Handshake h = t_handshake(&ctrls, str8_skip_last_slash(target_path));
  T_CHECK(h.good, "handshake");
  T_CHECK(h.saw_exe_first, "executable is the first module");
  if(h.good)
  {
    t_drain();
    pthread_t halter;
    pthread_create(&halter, 0, t_halter_main, 0);
    DMN_Event *e = t_expect(&ctrls, DMN_EventKind_Halt, 0);
    pthread_join(halter, 0);
    T_CHECK(e != 0, "halt");
    T_CHECK(dmn_ctrl_detach(t_ctx, h.process), "detach");
    t_drain();
    e = t_expect(&ctrls, DMN_EventKind_ExitProcess, 0);
    T_CHECK(e != 0, "detached process reported as exited");
    usleep(200000);
    T_CHECK(kill(pid, 0) == 0, "target keeps running after detach");
    t_drain();
  }
  kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  scratch_end(scratch);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmdline)
{
  t_arena = arena_alloc();
  t_in_github_actions = (getenv("GITHUB_ACTIONS") != 0);
  String8 target_path = str8_list_first(&cmdline->inputs);
  if(target_path.size == 0)
  {
    fprintf(stderr, "usage: mac_demon_test <path to mac_demon_test_target>\n");
    exit(2);
  }
  {
    char buf[PATH_MAX] = {0};
    if(realpath((char *)push_str8_copy(t_arena, target_path).str, buf) != 0)
    {
      target_path = push_str8_copy(t_arena, str8_cstring(buf));
    }
  }
  pthread_t watchdog;
  pthread_create(&watchdog, 0, t_watchdog_main, 0);
  dmn_init();
  t_ctx = dmn_ctrl_begin();
  t_test_launch(target_path);
  t_test_crash(target_path);
  t_test_halt_kill(target_path);
  t_test_attach_detach(target_path);
  t_deadline_us = 0;
  t_kill_all_targets();
  printf("\n%llu passed, %llu failed\n", t_passes, t_failures);
  if(t_in_github_actions) { printf("::notice title=mac_demon_test::%llu passed, %llu failed\n", t_passes, t_failures); }
  fflush(stdout);
  exit(t_failures == 0 ? 0 : 1);
}
