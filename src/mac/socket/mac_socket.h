// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_SOCKET_H
#define MAC_SOCKET_H

////////////////////////////////
//~ Includes

#pragma push_macro("internal")
#pragma push_macro("global")
#undef internal
#undef global
#include <sys/socket.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#pragma pop_macro("global")
#pragma pop_macro("internal")

////////////////////////////////
//~ Implementation Types

typedef struct MAC_SOCK_Connection MAC_SOCK_Connection;
struct MAC_SOCK_Connection
{
  MAC_SOCK_Connection *next;
  MAC_SOCK_Connection *prev;
  struct MAC_SOCK_Session *session;
  SOCK_Endpoint endpoint;
  SOCK_Protocol protocol;
  int socket;
  U8 *recv_buffer;
  U64 recv_buffer_pos;
};

typedef struct MAC_SOCK_ConnectionSlot MAC_SOCK_ConnectionSlot;
struct MAC_SOCK_ConnectionSlot
{
  MAC_SOCK_Connection *first;
  MAC_SOCK_Connection *last;
};

typedef struct MAC_SOCK_Session MAC_SOCK_Session;
struct MAC_SOCK_Session
{
  Arena *arena;
  SOCK_WakeupFunctionType *wakeup_hook;
  U64 recv_packet_size;
  GuardedRing *recv_ring;
  
  // connection cache
  StripeArray connection_stripes;
  U64 connection_slots_count;
  MAC_SOCK_ConnectionSlot *connection_slots;
  
  // listen socket
  int tcp_listen_socket;
};

typedef struct MAC_SOCK_State MAC_SOCK_State;
struct MAC_SOCK_State
{
  Arena *arena;
  GuardedRing *u2s_ring;
  int kqueue_fd;
  Thread listen_thread;
  Thread sender_thread;
};

////////////////////////////////
//~ Globals

global MAC_SOCK_State *mac_sock_state = 0;

////////////////////////////////
//~ Threads

internal void mac_sock_listen_thread_entry_point(void *p);
internal void mac_sock_sender_thread_entry_point(void *p);

#endif // MAC_SOCKET_H
