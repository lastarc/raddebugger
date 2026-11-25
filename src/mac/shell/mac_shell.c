// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Helpers

internal B32
mac_sh_run(Arena *arena, char **argv, String8 *stdout_out)
{
  B32 result = 0;
  int pipe_fds[2] = {-1, -1};
  B32 capture = (stdout_out != 0);
  if(capture && pipe(pipe_fds) != 0)
  {
    return 0;
  }
  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_init(&file_actions);
  if(capture)
  {
    posix_spawn_file_actions_adddup2(&file_actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, pipe_fds[0]);
    posix_spawn_file_actions_addclose(&file_actions, pipe_fds[1]);
  }
  pid_t pid = 0;
  int spawn_code = posix_spawn(&pid, argv[0], &file_actions, 0, argv, *_NSGetEnviron());
  posix_spawn_file_actions_destroy(&file_actions);
  if(capture)
  {
    close(pipe_fds[1]);
  }
  if(spawn_code == 0)
  {
    if(capture)
    {
      String8List chunks = {0};
      Temp scratch = scratch_begin(&arena, 1);
      for(;;)
      {
        U8 *buffer = push_array_no_zero(scratch.arena, U8, KB(4));
        ssize_t read_size = MAC_RETRY_ON_EINTR(read(pipe_fds[0], buffer, KB(4)));
        if(read_size <= 0) { break; }
        str8_list_push(scratch.arena, &chunks, str8(buffer, (U64)read_size));
      }
      *stdout_out = str8_list_join(arena, &chunks, 0);
      scratch_end(scratch);
    }
    int status = 0;
    if(MAC_RETRY_ON_EINTR(waitpid(pid, &status, 0)) == pid)
    {
      result = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
  }
  if(capture)
  {
    close(pipe_fds[0]);
  }
  return result;
}

internal CFStringRef
mac_sh_cfstring_from_str8(String8 string)
{
  CFStringRef result = CFStringCreateWithBytes(kCFAllocatorDefault, string.str, (CFIndex)string.size, kCFStringEncodingUTF8, 0);
  if(result == 0)
  {
    result = CFStringCreateWithCString(kCFAllocatorDefault, "", kCFStringEncodingUTF8);
  }
  return result;
}

////////////////////////////////
//~ @per_os_impl Shell Operations

internal void
sh_message(B32 error, String8 title, String8 message)
{
  //- always mirror to stderr, for terminal launches
  if(error)
  {
    fprintf(stderr, "[X] ");
  }
  fprintf(stderr, "%.*s\n", str8_varg(title));
  fprintf(stderr, "%.*s\n\n", str8_varg(message));

  //- show a native alert
  CFStringRef title_cf = mac_sh_cfstring_from_str8(title);
  CFStringRef message_cf = mac_sh_cfstring_from_str8(message);
  CFOptionFlags response = 0;
  CFUserNotificationDisplayAlert(0, error ? kCFUserNotificationStopAlertLevel : kCFUserNotificationNoteAlertLevel,
                                 0, 0, 0, title_cf, message_cf, 0, 0, 0, &response);
  CFRelease(title_cf);
  CFRelease(message_cf);
}

internal String8
sh_pick_file(Arena *arena, String8 title, String8 initial_path)
{
  String8 result = {0};
  Temp scratch = scratch_begin(&arena, 1);

  //- determine starting folder
  String8 initial_folder = initial_path;
  if(initial_folder.size != 0 && !folder_path_exists(initial_folder))
  {
    initial_folder = str8_chop_last_slash(initial_folder);
  }
  if(initial_folder.size != 0 && !folder_path_exists(initial_folder))
  {
    initial_folder = str8_zero();
  }

  //- run AppleScript's file picker; strings are passed as arguments, never
  // spliced into the script text.
  char *argv[16] = {0};
  U64 argc = 0;
  argv[argc++] = "/usr/bin/osascript";
  argv[argc++] = "-e"; argv[argc++] = "on run argv";
  if(initial_folder.size != 0)
  {
    argv[argc++] = "-e"; argv[argc++] = "set p to POSIX path of (choose file with prompt (item 1 of argv) default location (POSIX file (item 2 of argv)))";
  }
  else
  {
    argv[argc++] = "-e"; argv[argc++] = "set p to POSIX path of (choose file with prompt (item 1 of argv))";
  }
  argv[argc++] = "-e"; argv[argc++] = "return p";
  argv[argc++] = "-e"; argv[argc++] = "end run";
  argv[argc++] = (char *)push_str8_copy(scratch.arena, title).str;
  if(initial_folder.size != 0)
  {
    argv[argc++] = (char *)push_str8_copy(scratch.arena, initial_folder).str;
  }
  argv[argc] = 0;
  String8 output = {0};
  if(mac_sh_run(scratch.arena, argv, &output))
  {
    String8 path = str8_skip_chop_whitespace(output);
    // NOTE: POSIX path of a package/folder ends with '/'; strip it
    if(path.size > 1 && path.str[path.size-1] == '/')
    {
      path.size -= 1;
    }
    result = str8_copy(arena, path);
  }
  scratch_end(scratch);
  return result;
}

internal void
sh_show_in_file_browser(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  char *argv[] = {"/usr/bin/open", "-R", (char *)push_str8_copy(scratch.arena, path).str, 0};
  if(!mac_sh_run(scratch.arena, argv, 0))
  {
    sh_message(1, s("Error"), str8f(scratch.arena, "Could not reveal %S in Finder.", path));
  }
  scratch_end(scratch);
}

internal void
sh_open_in_browser(String8 url)
{
  Temp scratch = scratch_begin(0, 0);
  B32 is_web_url = (str8_match(url, s("https://"), StringMatchFlag_RightSideSloppy|StringMatchFlag_CaseInsensitive) ||
                    str8_match(url, s("http://"), StringMatchFlag_RightSideSloppy|StringMatchFlag_CaseInsensitive));
  char *argv[] = {"/usr/bin/open", (char *)push_str8_copy(scratch.arena, url).str, 0};
  if(!is_web_url || !mac_sh_run(scratch.arena, argv, 0))
  {
    sh_message(1, s("Error"), str8f(scratch.arena, "Could not open %S in a browser.", url));
  }
  scratch_end(scratch);
}

internal B32
sh_install_or_uninstall_self(B32 write, B32 install)
{
  B32 install_state = 0;
  Temp scratch = scratch_begin(0, 0);

  //- installation == a symlink to this executable in ~/.local/bin
  char *home = getenv("HOME");
  if(home == 0) { home = ""; }
  String8 bin_path = str8f(scratch.arena, "%s/.local/bin", home);
  String8 application_name = str8_chop_last_dot(str8_skip_last_slash(get_process_info()->binary_file_path));
  String8 symlink_path = str8f(scratch.arena, "%S/%S", bin_path, application_name);

  if(write)
  {
    if(install)
    {
      make_directory(str8f(scratch.arena, "%s/.local", home));
      make_directory(bin_path);
      unlink((char *)symlink_path.str);
      install_state = (symlink((char *)get_process_info()->binary_file_path.str, (char *)symlink_path.str) == 0);
    }
    else
    {
      struct stat st = {0};
      if(lstat((char *)symlink_path.str, &st) == 0 && S_ISLNK(st.st_mode))
      {
        unlink((char *)symlink_path.str);
      }
      install_state = 0;
    }
  }
  else
  {
    struct stat st = {0};
    install_state = (lstat((char *)symlink_path.str, &st) == 0 && S_ISLNK(st.st_mode));
  }

  scratch_end(scratch);
  return install_state;
}
