// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_BASE_H
#define MAC_BASE_H

////////////////////////////////
//~ Includes

// NOTE: Darwin system headers use `internal` & `global` as identifiers (e.g.
// task_vm_info.internal), which collide with this codebase's keywords.
// NOTE: keep AssertMacros.h (pulled in by some system frameworks) from
// defining unprefixed `check`/`verify`/`require` macros.
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#pragma push_macro("internal")
#pragma push_macro("global")
#pragma push_macro("local_persist")
#undef internal
#undef global
#undef local_persist
#include <copyfile.h>
#include <crt_externs.h>
#include <dirent.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <libproc.h>
#include <limits.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/ucontext.h>
#pragma pop_macro("local_persist")
#pragma pop_macro("global")
#pragma pop_macro("internal")

#if ARCH_X64 || ARCH_X86
# include "x64/x64_exceptions.h"
#endif

typedef struct tm tm;
typedef struct timespec timespec;

////////////////////////////////
//~ Darwin futex-style wait/wake (exported by libsystem_kernel since 10.12)

#define MAC_UL_COMPARE_AND_WAIT 1
#define MAC_ULF_WAKE_ALL        0x00000100
int __ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout_us);
int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value);

////////////////////////////////
//~ Call Interruption Retry Helper

#define MAC_RETRY_ON_EINTR(expr)             \
(__extension__({                           \
__typeof__(expr) __ret;                    \
do {                                       \
__ret = (expr);                          \
} while ((__ret == -1) && errno == EINTR); \
__ret;                                     \
}))

////////////////////////////////
//~ File Iterator

typedef struct MAC_FileIter MAC_FileIter;
struct MAC_FileIter
{
  DIR *dir;
  struct dirent *dp;
  String8 path;
};
StaticAssert(sizeof(Member(FileIter, memory)) >= sizeof(MAC_FileIter), mac_file_iter_size_check);

////////////////////////////////
//~ Safe Call Handler Chain

typedef struct MAC_SafeCallChain MAC_SafeCallChain;
struct MAC_SafeCallChain
{
  MAC_SafeCallChain *next;
  ThreadEntryPointFunctionType *fail_handler;
  void *ptr;
};

////////////////////////////////
//~ Entities

typedef enum MAC_EntityKind
{
  MAC_EntityKind_Thread,
  MAC_EntityKind_Mutex,
  MAC_EntityKind_RWMutex,
  MAC_EntityKind_ConditionVariable,
  MAC_EntityKind_Barrier,
  MAC_EntityKind_Semaphore,
}
MAC_EntityKind;

typedef struct MAC_Entity MAC_Entity;
struct MAC_Entity
{
  MAC_Entity *next;
  MAC_EntityKind kind;
  union
  {
    struct
    {
      pthread_t handle;
      ThreadEntryPointFunctionType *func;
      void *ptr;
    } thread;
    pthread_mutex_t mutex_handle;
    pthread_rwlock_t rwmutex_handle;
    struct
    {
      pthread_cond_t cond_handle;
      pthread_mutex_t rwlock_mutex_handle;
    } cv;
    struct
    {
      // NOTE: macOS has no pthread_barrier_t
      pthread_mutex_t mutex;
      pthread_cond_t cond;
      U64 count;
      U64 waiting;
      U64 generation;
    } barrier;
    struct
    {
      // NOTE: macOS does not implement unnamed POSIX semaphores (sem_init),
      // nor timed waits on named ones (sem_timedwait). unnamed semaphores
      // are implemented with libdispatch; named (cross-process) ones use
      // sem_open, with timed waits implemented by polling.
      B32 is_named;
      B32 is_owner;
      dispatch_semaphore_t unnamed;
      sem_t *named;
    } semaphore;
  };
};

////////////////////////////////
//~ On Demand Memory

#define MAC_MEMORY_FAULT_WORKER_LIMIT 32

typedef struct
{
  void *address;
  U32 result;
} MAC_MemoryFaultRequest;

typedef struct
{
  MemoryReadFaultFunction *fault;
  void *user_data;
  Thread workers[MAC_MEMORY_FAULT_WORKER_LIMIT];
  U32 worker_count;
  U32 stops_sent;
  U32 workers_joined;
  int requests[2];
  U32 active;
  pid_t owner_pid;
} MAC_DemandMemory;

////////////////////////////////
//~ State

typedef struct MAC_State MAC_State;
struct MAC_State
{
  Arena *arena;
  SystemInfo system_info;
  ProcessInfo process_info;
  pthread_mutex_t entity_mutex;
  Arena *entity_arena;
  MAC_Entity *entity_free;
  U64 default_env_count;
  char **default_env;
  MAC_DemandMemory demand_memory;
  mach_timebase_info_data_t timebase;
};

////////////////////////////////
//~ Globals

global MAC_State mac_state = {0};
thread_static MAC_SafeCallChain *mac_safe_call_chain = 0;
thread_static B32 mac_in_memory_fault_callback;

////////////////////////////////
//~ Helpers

internal DateTime mac_date_time_from_tm(tm in, U32 msec);
internal tm mac_tm_from_date_time(DateTime dt);
internal timespec mac_timespec_from_date_time(DateTime dt);
internal DenseTime mac_dense_time_from_timespec(timespec in);
internal FileProperties mac_file_properties_from_stat(struct stat *s);
internal String8 mac_ipc_name_from_name(Arena *arena, String8 name);
internal B32 mac_dispatch_memory_read_fault(int sig, siginfo_t *info, void *context);
internal void mac_safe_call_sig_handler(int sig, siginfo_t *info, void *context);

////////////////////////////////
//~ Entities

internal MAC_Entity *mac_entity_alloc(MAC_EntityKind kind);
internal void mac_entity_release(MAC_Entity *entity);

////////////////////////////////
//~ Thread Entry Point

internal void *mac_thread_entry_point(void *ptr);

#endif // MAC_BASE_H
