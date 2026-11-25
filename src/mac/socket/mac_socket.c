// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Helpers

internal void
mac_sock_kqueue_add(int fd, void *udata)
{
  struct kevent change = {0};
  EV_SET(&change, (uintptr_t)fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, udata);
  MAC_RETRY_ON_EINTR(kevent(mac_sock_state->kqueue_fd, &change, 1, 0, 0, 0));
}

internal void
mac_sock_kqueue_remove(int fd)
{
  struct kevent change = {0};
  EV_SET(&change, (uintptr_t)fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
  MAC_RETRY_ON_EINTR(kevent(mac_sock_state->kqueue_fd, &change, 1, 0, 0, 0));
}

internal void
mac_sock_configure_new_socket(int fd)
{
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
  fcntl(fd, F_SETFD, FD_CLOEXEC);
}

////////////////////////////////
//~ Listener Thread

internal void
mac_sock_listen_thread_entry_point(void *p)
{
  ThreadNameF("mac_sock_listen_thread");
  for(;;)
  {
    //- wait for next event
    struct kevent evt = {0};
    int wait_result = kevent(mac_sock_state->kqueue_fd, 0, 0, &evt, 1, 0);
    if(wait_result <= 0)
    {
      continue;
    }

    //- unpack event data
    //
    // 0 bit64 -> just a session pointer, no connection
    // 1 bit64 -> connection pointer, which refers to its session
    //
    MAC_SOCK_Session *session = 0;
    MAC_SOCK_Connection *con = 0;
    {
      U64 udata = (U64)evt.udata;
      if(udata & bit64)
      {
        con = (MAC_SOCK_Connection *)(udata & ~bit64);
        session = con->session;
      }
      else
      {
        session = (MAC_SOCK_Session *)udata;
      }
    }
    if(session == 0)
    {
      continue;
    }

    //- connection is 0 -> session's listener is ready for accept
    if(con == 0)
    {
      struct sockaddr_storage addr = {0};
      socklen_t addr_size = sizeof(addr);
      int new_socket = MAC_RETRY_ON_EINTR(accept(session->tcp_listen_socket, (struct sockaddr *)&addr, &addr_size));
      if(new_socket >= 0)
      {
        mac_sock_configure_new_socket(new_socket);

        // unpack socket's endpoint info
        SOCK_Endpoint endpoint = {0};
        switch(addr.ss_family)
        {
          default:{}break;
          case AF_INET:
          {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)&addr;
            endpoint.kind = SOCK_EndpointKind_IPv4;
            endpoint.port = ntohs(ipv4->sin_port);
            endpoint.address_u32[0] = ipv4->sin_addr.s_addr;
          }break;
          case AF_INET6:
          {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&addr;
            endpoint.kind = SOCK_EndpointKind_IPv6;
            endpoint.port = ntohs(ipv6->sin6_port);
            MemoryCopy(endpoint.address_u8, ipv6->sin6_addr.s6_addr, sizeof(endpoint.address_u8));
          }break;
        }

        // unpack endpoint
        U64 hash = u64_hash_from_str8(str8_struct(&endpoint));
        U64 slot_idx = hash%session->connection_slots_count;
        MAC_SOCK_ConnectionSlot *slot = &session->connection_slots[slot_idx];
        Stripe *stripe = stripe_from_slot_idx(&session->connection_stripes, slot_idx);

        // store new connection
        U64 con_ptr_u64 = 0;
        RWMutexScope(stripe->rw_mutex, 1)
        {
          MAC_SOCK_Connection *new_con = (MAC_SOCK_Connection *)stripe->free;
          if(new_con != 0)
          {
            stripe->free = new_con->next;
          }
          else
          {
            new_con = push_array(stripe->arena, MAC_SOCK_Connection, 1);
            new_con->recv_buffer = push_array(stripe->arena, U8, session->recv_packet_size);
          }
          new_con->session = session;
          new_con->endpoint = endpoint;
          new_con->protocol = SOCK_Protocol_TCP;
          new_con->socket = new_socket;
          new_con->recv_buffer_pos = 0;
          DLLPushBack(slot->first, slot->last, new_con);
          con_ptr_u64 = (U64)new_con;
        }

        // add socket to kqueue
        mac_sock_kqueue_add(new_socket, (void *)(con_ptr_u64 | bit64));
      }
    }

    //- connection is nonzero -> ready to read on a connection.
    else
    {
      SOCK_Endpoint endpoint = con->endpoint;
      U64 hash = u64_hash_from_str8(str8_struct(&endpoint));
      U64 slot_idx = hash%session->connection_slots_count;
      MAC_SOCK_ConnectionSlot *slot = &session->connection_slots[slot_idx];
      Stripe *stripe = stripe_from_slot_idx(&session->connection_stripes, slot_idx);

      // do read
      ssize_t recv_result = MAC_RETRY_ON_EINTR(recv(con->socket, con->recv_buffer + con->recv_buffer_pos, session->recv_packet_size - con->recv_buffer_pos, MSG_DONTWAIT));
      B32 would_block = (recv_result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

      // recv buffer nonzero -> increment recv buffer position
      if(recv_result > 0)
      {
        con->recv_buffer_pos += (U64)recv_result;
      }

      // recv buffer full -> push to user, reset
      if(con->recv_buffer_pos >= session->recv_packet_size)
      {
        RingGuard g = guarded_ring_open(session->recv_ring);
        {
          SOCK_Protocol protocol = SOCK_Protocol_TCP;
          guarded_ring_write_struct_or_wait(&g, &endpoint, max_U64);
          guarded_ring_write_struct_or_wait(&g, &protocol, max_U64);
          guarded_ring_write_or_wait(&g, session->recv_packet_size, con->recv_buffer, max_U64);
        }
        guarded_ring_close(&g);
        con->recv_buffer_pos = 0;
      }

      // error -> socket was disconnected
      if(recv_result <= 0 && !would_block) RWMutexScope(stripe->rw_mutex, 1)
      {
        mac_sock_kqueue_remove(con->socket);
        close(con->socket);
        DLLRemove(slot->first, slot->last, con);
        con->next = stripe->free;
        stripe->free = con;
      }
    }

    //- call wakeup hook
    if(session->wakeup_hook)
    {
      session->wakeup_hook();
    }
  }
}

////////////////////////////////
//~ Sender Thread

internal void
mac_sock_sender_thread_entry_point(void *p)
{
  ThreadNameF("mac_sock_sender_thread");
  for(;;)
  {
    Temp scratch = scratch_begin(0, 0);

    //- wait for next send payload
    MAC_SOCK_Session *session = 0;
    SOCK_Endpoint endpoint = {0};
    SOCK_Protocol protocol = {0};
    SOCK_SendCompletionParams completion_params = {0};
    String8 data = {0};
    {
      RingGuard g = guarded_ring_open(mac_sock_state->u2s_ring);
      {
        guarded_ring_read_struct_or_wait(&g, &session, max_U64);
        guarded_ring_read_struct_or_wait(&g, &endpoint, max_U64);
        guarded_ring_read_struct_or_wait(&g, &protocol, max_U64);
        guarded_ring_read_struct_or_wait(&g, &completion_params, max_U64);
        guarded_ring_read_struct_or_wait(&g, &data.size, max_U64);
        data.str = push_array_no_zero(scratch.arena, U8, data.size);
        guarded_ring_read_or_wait(&g, data.size, data.str, max_U64);
      }
      guarded_ring_close(&g);
    }

    //- unpack endpoint
    U64 hash = u64_hash_from_str8(str8_struct(&endpoint));
    U64 slot_idx = hash%session->connection_slots_count;
    Stripe *stripe = stripe_from_slot_idx(&session->connection_stripes, slot_idx);
    MAC_SOCK_ConnectionSlot *slot = &session->connection_slots[slot_idx];

    //- endpoint -> socket
    int ep_socket = -1;
    RWMutexScope(stripe->rw_mutex, 0)
    {
      for(MAC_SOCK_Connection *c = slot->first; c != 0; c = c->next)
      {
        if(sock_endpoint_match(c->endpoint, endpoint) && c->protocol == protocol)
        {
          ep_socket = c->socket;
          break;
        }
      }
    }

    //- if we didn't find a socket -> create one & connect
    if(ep_socket == -1) RWMutexScope(stripe->rw_mutex, 1)
    {
      for(MAC_SOCK_Connection *c = slot->first; c != 0; c = c->next)
      {
        if(sock_endpoint_match(c->endpoint, endpoint) && c->protocol == protocol)
        {
          ep_socket = c->socket;
          break;
        }
      }
      if(ep_socket == -1)
      {
        // convert endpoint -> sockaddr
        struct sockaddr_storage endpoint_sockaddr = {0};
        socklen_t endpoint_sockaddr_size = 0;
        int family = AF_INET;
        switch(endpoint.kind)
        {
          default:
          case SOCK_EndpointKind_IPv4:
          {
            struct sockaddr_in *dst = (struct sockaddr_in *)(&endpoint_sockaddr);
            endpoint_sockaddr_size = sizeof(*dst);
            dst->sin_len = sizeof(*dst);
            dst->sin_family = AF_INET;
            dst->sin_port = htons(endpoint.port);
            MemoryCopy(&dst->sin_addr, &endpoint.address_u32[0], sizeof(U32));
            family = AF_INET;
          }break;
          case SOCK_EndpointKind_IPv6:
          {
            struct sockaddr_in6 *dst = (struct sockaddr_in6 *)(&endpoint_sockaddr);
            endpoint_sockaddr_size = sizeof(*dst);
            dst->sin6_len = sizeof(*dst);
            dst->sin6_family = AF_INET6;
            dst->sin6_port = htons(endpoint.port);
            MemoryCopy(&dst->sin6_addr, &endpoint.address_u128[0], sizeof(U128));
            family = AF_INET6;
          }break;
        }

        // create & connect
        int new_socket = socket(family, SOCK_STREAM, 0);
        if(new_socket >= 0)
        {
          mac_sock_configure_new_socket(new_socket);
          if(MAC_RETRY_ON_EINTR(connect(new_socket, (struct sockaddr *)&endpoint_sockaddr, endpoint_sockaddr_size)) != 0)
          {
            close(new_socket);
            new_socket = -1;
          }
        }

        // allocate & store in cache
        if(new_socket >= 0)
        {
          MAC_SOCK_Connection *con = (MAC_SOCK_Connection *)stripe->free;
          if(con != 0)
          {
            stripe->free = con->next;
          }
          else
          {
            con = push_array(stripe->arena, MAC_SOCK_Connection, 1);
            con->recv_buffer = push_array(stripe->arena, U8, session->recv_packet_size);
          }
          con->session = session;
          con->endpoint = endpoint;
          con->protocol = protocol;
          con->socket = new_socket;
          con->recv_buffer_pos = 0;
          DLLPushBack(slot->first, slot->last, con);
          mac_sock_kqueue_add(new_socket, (void *)((U64)con | bit64));
          ep_socket = con->socket;
        }
      }
    }

    //- got socket -> send
    B32 send_failed = 0;
    if(ep_socket != -1)
    {
      U64 off = 0;
      for(;off < data.size;)
      {
        ssize_t send_result = MAC_RETRY_ON_EINTR(send(ep_socket, (char *)data.str + off, data.size - off, 0));
        if(send_result <= 0)
        {
          send_failed = 1;
          break;
        }
        off += (U64)send_result;
      }
    }

    //- got socket, but detected bad connection? -> connection closed
    if(ep_socket != -1 && send_failed)
    {
      RWMutexScope(stripe->rw_mutex, 1)
      {
        for(MAC_SOCK_Connection *c = slot->first, *next = 0; c != 0; c = next)
        {
          next = c->next;
          if(sock_endpoint_match(c->endpoint, endpoint) && c->protocol == protocol)
          {
            mac_sock_kqueue_remove(c->socket);
            close(c->socket);
            DLLRemove(slot->first, slot->last, c);
            c->next = stripe->free;
            stripe->free = c;
          }
        }
      }
    }

    //- write completion of this send into the completion ring buffer
    if(completion_params.out_ring != 0)
    {
      SOCK_SendCompletion completion = {.id = completion_params.id, .good = (ep_socket != -1 && !send_failed)};
      RingGuard g = guarded_ring_open(completion_params.out_ring);
      guarded_ring_write_struct_or_wait(&g, &completion, max_U64);
      guarded_ring_close(&g);
    }

    scratch_end(scratch);
  }
}

////////////////////////////////
//~ @per_os_impl Top-Level Layer Calls

internal void
sock_init(void)
{
  Arena *arena = arena_alloc();
  mac_sock_state = push_array(arena, MAC_SOCK_State, 1);
  mac_sock_state->arena = arena;
  mac_sock_state->u2s_ring = guarded_ring_alloc(arena, KB(256));
  mac_sock_state->kqueue_fd = kqueue();
  fcntl(mac_sock_state->kqueue_fd, F_SETFD, FD_CLOEXEC);
  mac_sock_state->listen_thread = thread_launch(mac_sock_listen_thread_entry_point, 0);
  mac_sock_state->sender_thread = thread_launch(mac_sock_sender_thread_entry_point, 0);
}

////////////////////////////////
//~ @per_os_impl Session Creation/Closing

internal SOCK_Session
sock_session_open(U16 port, SOCK_WakeupFunctionType *wakeup_hook, U64 recv_packet_size)
{
  //- set up state
  Arena *arena = arena_alloc();
  MAC_SOCK_Session *session = push_array(arena, MAC_SOCK_Session, 1);
  session->arena = arena;
  session->wakeup_hook = wakeup_hook;
  session->recv_packet_size = recv_packet_size;
  session->recv_ring = guarded_ring_alloc(arena, KB(256));
  session->tcp_listen_socket = -1;

  //- set up connection cache
  session->connection_slots_count = 8;
  session->connection_stripes = stripe_array_alloc(arena);
  session->connection_slots = push_array(arena, MAC_SOCK_ConnectionSlot, session->connection_slots_count);

  //- set up listener
  //
  // NOTE: listening sockets bind to the loopback interface only by default, so
  // that other machines on the network cannot send commands (which can launch
  // & control processes) to this machine. set RADDBG_SOCKET_BIND_ANY=1 to
  // listen on all interfaces (e.g. for remote debugging setups).
  //
  if(port != 0)
  {
    int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_socket >= 0)
    {
      mac_sock_configure_new_socket(listen_socket);
      int reuse = 1;
      setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
      char *bind_any_env = getenv("RADDBG_SOCKET_BIND_ANY");
      B32 bind_any = (bind_any_env != 0 && bind_any_env[0] == '1');
      struct sockaddr_in server_addr = {0};
      server_addr.sin_len = sizeof(server_addr);
      server_addr.sin_family = AF_INET;
      server_addr.sin_addr.s_addr = htonl(bind_any ? INADDR_ANY : INADDR_LOOPBACK);
      server_addr.sin_port = htons(port);
      if(bind(listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0 &&
         listen(listen_socket, SOMAXCONN) == 0)
      {
        session->tcp_listen_socket = listen_socket;
        mac_sock_kqueue_add(listen_socket, session);
      }
      else
      {
        close(listen_socket);
      }
    }
  }

  //- bundle as handle
  SOCK_Session s = {(U64)session};
  return s;
}

internal void
sock_session_close(SOCK_Session session)
{
  // TODO(rjf)
}

////////////////////////////////
//~ @per_os_impl Sends / Receives

internal B32
sock_send_(SOCK_Session session, SOCK_Endpoint endpoint, SOCK_Protocol protocol, String8 data, U64 endt_us, SOCK_SendCompletionParams *completion_params)
{
  B32 result = 0;
  {
    U64 needed_size = sizeof(session) + sizeof(endpoint) + sizeof(protocol) + sizeof(*completion_params) + sizeof(data.size) + data.size;
    RingGuard g = guarded_ring_open(mac_sock_state->u2s_ring);
    if(guarded_ring_write_or_wait(&g, needed_size, 0, endt_us))
    {
      result = 1;
      guarded_ring_try_write_struct(&g, &session);
      guarded_ring_try_write_struct(&g, &endpoint);
      guarded_ring_try_write_struct(&g, &protocol);
      guarded_ring_try_write_struct(&g, completion_params);
      guarded_ring_try_write_struct(&g, &data.size);
      guarded_ring_try_write(&g, data.size, data.str);
    }
    guarded_ring_close(&g);
  }
  return result;
}

internal B32
sock_recv(SOCK_Session session_handle, SOCK_Endpoint *endpoint_out, SOCK_Protocol *protocol_out, U8 *data_out, U64 endt_us)
{
  B32 result = 0;
  {
    MAC_SOCK_Session *session = (MAC_SOCK_Session *)session_handle.u64[0];
    RingGuard g = guarded_ring_open(session->recv_ring);
    {
      SOCK_Endpoint endpoint = {0};
      SOCK_Protocol protocol = SOCK_Protocol_TCP;
      U64 recv_size = sizeof(endpoint) + sizeof(protocol) + session->recv_packet_size;
      if(guarded_ring_read_or_wait(&g, recv_size, 0, endt_us))
      {
        result = 1;
        guarded_ring_try_read_struct(&g, &endpoint);
        guarded_ring_try_read_struct(&g, &protocol);
        guarded_ring_try_read(&g, session->recv_packet_size, data_out);
        if(endpoint_out != 0)
        {
          endpoint_out[0] = endpoint;
        }
        if(protocol_out != 0)
        {
          protocol_out[0] = protocol;
        }
      }
    }
    guarded_ring_close(&g);
  }
  return result;
}
