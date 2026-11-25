// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_DEMON_H
#define MAC_DEMON_H

////////////////////////////////
//~ Overview
//
// The macOS demon drives debuggees through Mach:
//
// - processes are launched suspended (posix_spawn), or attached to, then
//   traced w/ ptrace(PT_ATTACHEXC), which turns all Unix signals into Mach
//   EXC_SOFTWARE/EXC_SOFT_SIGNAL exceptions.
// - all exceptions (signals, breakpoints, faults) are delivered to one
//   exception port owned by the demon. the raising thread blocks until the
//   demon replies, so exception messages are "held" across stops, & replied
//   to once the debuggee is resumed.
// - stopping is done w/ task_suspend; freezing threads w/ thread_suspend.
// - process exit is observed w/ a dead-name notification on the task port.
// - module loads/unloads are observed via a breakpoint on dyld's debugger
//   notification function, followed by a re-enumeration of loaded images.
//
// Only x86-64 debuggees are supported (as is the case for the codebase).

////////////////////////////////
//~ Includes

#pragma push_macro("internal")
#pragma push_macro("global")
#pragma push_macro("local_persist")
#undef internal
#undef global
#undef local_persist
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/exception_types.h>
#include <mach/thread_status.h>
#include <mach/notify.h>
#include <mach/ndr.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <libproc.h>
#include <spawn.h>
#include <signal.h>
#include <uuid/uuid.h>
#pragma pop_macro("local_persist")
#pragma pop_macro("global")
#pragma pop_macro("internal")

////////////////////////////////
//~ dyld Process Info (exported by libdyld; declared in a private SDK header)

typedef const struct dyld_process_info_base *MAC_DMN_DyldProcessInfo;
extern MAC_DMN_DyldProcessInfo _dyld_process_info_create(task_t task, uint64_t timestamp, kern_return_t *kernel_error);
extern void _dyld_process_info_for_each_image(MAC_DMN_DyldProcessInfo info, void (^callback)(uint64_t mach_header_address, const uuid_t uuid, const char *path));
extern void _dyld_process_info_release(MAC_DMN_DyldProcessInfo info);

////////////////////////////////
//~ Mach Exception Messages (mach_exc subsystem, w/ MACH_EXCEPTION_CODES)

#define MAC_DMN_MACH_EXCEPTION_RAISE_MSG_ID 2405
#define MAC_DMN_WAKEUP_MSG_ID               0x72616400

#pragma pack(push, 4)
typedef struct MAC_DMN_ExcRequest MAC_DMN_ExcRequest;
struct MAC_DMN_ExcRequest
{
  mach_msg_header_t head;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t thread;
  mach_msg_port_descriptor_t task;
  NDR_record_t ndr;
  exception_type_t exception;
  mach_msg_type_number_t code_count;
  int64_t code[2];
};

typedef struct MAC_DMN_ExcReply MAC_DMN_ExcReply;
struct MAC_DMN_ExcReply
{
  mach_msg_header_t head;
  NDR_record_t ndr;
  kern_return_t ret_code;
};
#pragma pack(pop)

typedef union MAC_DMN_RecvBuffer MAC_DMN_RecvBuffer;
union MAC_DMN_RecvBuffer
{
  mach_msg_header_t head;
  MAC_DMN_ExcRequest exc;
  mach_dead_name_notification_t dead_name;
  U8 bytes[4096];
};

////////////////////////////////
//~ Received Messages

typedef enum MAC_DMN_MsgKind
{
  MAC_DMN_MsgKind_Null,
  MAC_DMN_MsgKind_Exception,
  MAC_DMN_MsgKind_DeadName,
  MAC_DMN_MsgKind_Wakeup,
}
MAC_DMN_MsgKind;

typedef struct MAC_DMN_Msg MAC_DMN_Msg;
struct MAC_DMN_Msg
{
  MAC_DMN_Msg *next;
  MAC_DMN_MsgKind kind;
  MAC_DMN_ExcRequest exc;
  mach_port_t dead_name;
};

typedef struct MAC_DMN_MsgList MAC_DMN_MsgList;
struct MAC_DMN_MsgList
{
  MAC_DMN_Msg *first;
  MAC_DMN_Msg *last;
  U64 count;
};

////////////////////////////////
//~ Held Exceptions
//
// A thread which raised an exception is blocked until we reply. Replies are
// deferred until the thread is next allowed to run.

typedef enum MAC_DMN_ReplyKind
{
  MAC_DMN_ReplyKind_Handled,       // reply KERN_SUCCESS (thread continues w/ its current state)
  MAC_DMN_ReplyKind_SwallowSignal, // signal exception, never delivered to the debuggee
  MAC_DMN_ReplyKind_Signal,        // signal exception; delivered unless ignore_previous_exception
  MAC_DMN_ReplyKind_HwException,   // hardware exception; passed on (-> signal) unless ignore_previous_exception
}
MAC_DMN_ReplyKind;

typedef struct MAC_DMN_HeldException MAC_DMN_HeldException;
struct MAC_DMN_HeldException
{
  B32 active;
  MAC_DMN_ReplyKind kind;
  mach_port_t reply_port; // send-once right
  U32 signo;
};

////////////////////////////////
//~ Entities

typedef enum MAC_DMN_EntityKind
{
  MAC_DMN_EntityKind_Null,
  MAC_DMN_EntityKind_Process,
  MAC_DMN_EntityKind_Thread,
  MAC_DMN_EntityKind_Module,
  MAC_DMN_EntityKind_COUNT
}
MAC_DMN_EntityKind;

typedef enum MAC_DMN_ProcessState
{
  MAC_DMN_ProcessState_Null,
  MAC_DMN_ProcessState_Launching, // spawned suspended; waiting for the initial SIGSTOP (from PT_ATTACHEXC)
  MAC_DMN_ProcessState_Attaching, // running; waiting for the initial SIGSTOP (from PT_ATTACHEXC)
  MAC_DMN_ProcessState_Normal,
}
MAC_DMN_ProcessState;

typedef enum MAC_DMN_FloatFlavor
{
  MAC_DMN_FloatFlavor_Null,
  MAC_DMN_FloatFlavor_Float,
  MAC_DMN_FloatFlavor_AVX,
  MAC_DMN_FloatFlavor_AVX512,
}
MAC_DMN_FloatFlavor;

typedef struct MAC_DMN_Entity MAC_DMN_Entity;

typedef struct MAC_DMN_Thread MAC_DMN_Thread;
struct MAC_DMN_Thread
{
  MAC_DMN_Entity *next;
  MAC_DMN_Entity *prev;
  MAC_DMN_Entity *process;
  mach_port_t port;          // one send right, owned by us
  U64 tid;                   // system-wide thread ID
  B32 is_live;               // used during thread list refreshes

  // register cache (valid while stopped)
  B32 regs_valid;
  B32 regs_dirty;
  X64_RegBlock regs;
  MAC_DMN_FloatFlavor float_flavor;
  _STRUCT_X86_AVX512_STATE64 float_state; // prefix-compatible w/ the AVX & float flavors

  // run-control state
  B32 is_frozen_by_us;       // thread_suspend'd for this run
  B32 debug_state_set;       // DR registers written for this run
  B32 single_step_set;       // TF written for this run
  MAC_DMN_HeldException held;
  U32 expected_soft_signo;   // signal expected after passing a hardware exception
  U64 last_exception_ip;
  U32 last_exception_signo;

  // names
  U64 last_name_hash;
  U64 name_gather_time_us;
};

typedef struct MAC_DMN_Module MAC_DMN_Module;
struct MAC_DMN_Module
{
  MAC_DMN_Entity *next;
  MAC_DMN_Entity *prev;
  MAC_DMN_Entity *process;
  U64 base_vaddr;
  U64 size;
  String8 path;              // allocated w/ the demon's path arena (heap)
  B32 is_pinned;             // main executable & dyld; never unloaded by image list diffs
  B32 is_live;               // used during module list refreshes
};

#define MAC_DMN_MAX_NOTIFIER_TRAPS 4

typedef struct MAC_DMN_Process MAC_DMN_Process;
struct MAC_DMN_Process
{
  MAC_DMN_Entity *next;
  MAC_DMN_Entity *prev;
  pid_t pid;
  task_t task;
  MAC_DMN_ProcessState state;
  B32 is_suspended_by_us;
  B32 is_dead;
  B32 is_child;               // launched by us (as opposed to attached to)
  B32 detach_requested;
  U32 exit_code;

  // entity lists
  MAC_DMN_Entity *first_thread;
  MAC_DMN_Entity *last_thread;
  U64 thread_count;
  MAC_DMN_Entity *first_module;
  MAC_DMN_Entity *last_module;
  U64 module_count;

  // dyld image notification traps
  U64 notifier_vaddrs[MAC_DMN_MAX_NOTIFIER_TRAPS];
  U64 notifier_count;
  B32 notifier_searched;

  // previously-installed exception ports, restored on detach
  mach_msg_type_number_t saved_exc_count;
  exception_mask_t saved_exc_masks[EXC_TYPES_COUNT];
  mach_port_t saved_exc_ports[EXC_TYPES_COUNT];
  exception_behavior_t saved_exc_behaviors[EXC_TYPES_COUNT];
  thread_state_flavor_t saved_exc_flavors[EXC_TYPES_COUNT];
};

struct MAC_DMN_Entity
{
  union
  {
    MAC_DMN_Process process;
    MAC_DMN_Thread thread;
    MAC_DMN_Module module;
    MAC_DMN_Entity *next_free;
  };
  U32 gen;
  MAC_DMN_EntityKind kind;
};

////////////////////////////////
//~ Software Trap Records (traps written for the duration of one run)

typedef struct MAC_DMN_WrittenTrap MAC_DMN_WrittenTrap;
struct MAC_DMN_WrittenTrap
{
  MAC_DMN_Entity *process;
  U64 vaddr;
  U8 og_byte;
  B32 written;
};

typedef struct MAC_DMN_PrevTrap MAC_DMN_PrevTrap;
struct MAC_DMN_PrevTrap
{
  DMN_Handle process;
  U64 vaddr;
};

typedef struct MAC_DMN_WrittenTrapArray MAC_DMN_WrittenTrapArray;
struct MAC_DMN_WrittenTrapArray
{
  MAC_DMN_WrittenTrap *v;
  U64 count;
};

////////////////////////////////
//~ Top-Level State

typedef struct MAC_DMN_State MAC_DMN_State;
struct MAC_DMN_State
{
  Arena *arena;

  // access locking (non-ctrl-thread introspection is disallowed while running)
  Mutex access_mutex;
  B32 access_run_state;

  // entity storage
  Arena *entities_arena;
  MAC_DMN_Entity *entities_base;
  U64 entities_count;
  MAC_DMN_Entity *free_entity;

  // process list
  MAC_DMN_Entity *first_process;
  MAC_DMN_Entity *last_process;
  U64 process_count;

  // processes which were detached, & which need exit events
  MAC_DMN_Entity *first_detached_process;
  MAC_DMN_Entity *last_detached_process;

  // ports
  mach_port_t exception_port;
  mach_port_t notify_port;
  mach_port_t wakeup_port;
  mach_port_t port_set;

  // messages received outside of a run (while detaching), handled by the next run
  Arena *deferred_arena;
  MAC_DMN_MsgList deferred_msgs;
  
  // halting
  Mutex halter_mutex;
  B32 halt_requested;
  U64 halt_code;
  U64 halt_user_data;

  // software trap addresses from the previous run, for ghost traps
  Arena *prev_traps_arena;
  MAC_DMN_PrevTrap *prev_traps;
  U64 prev_traps_count;
};

////////////////////////////////
//~ Globals

global MAC_DMN_State *mac_dmn_state = 0;
thread_static B32 mac_dmn_ctrl_thread = 0;

////////////////////////////////
//~ Helpers

//- memory
internal U64 mac_dmn_read(task_t task, Rng1U64 range, void *dst);
internal B32 mac_dmn_write(task_t task, Rng1U64 range, void *src);
#define mac_dmn_read_struct(task, vaddr, ptr) (mac_dmn_read((task), r1u64((vaddr), (vaddr)+sizeof(*(ptr))), (ptr)) == sizeof(*(ptr)))

//- entities
internal MAC_DMN_Entity *mac_dmn_entity_alloc(MAC_DMN_EntityKind kind);
internal void mac_dmn_entity_release(MAC_DMN_Entity *entity);
internal DMN_Handle mac_dmn_handle_from_entity(MAC_DMN_Entity *entity);
internal MAC_DMN_Entity *mac_dmn_entity_from_handle(DMN_Handle handle, MAC_DMN_EntityKind kind);

#endif // MAC_DEMON_H
