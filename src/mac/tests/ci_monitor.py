# CI diagnostics (macOS, run as root): every second, mirrors test progress into
# a GitHub check run, without spawning processes for the report itself, and
# records whether spawning a process still works & which tasks are suspended.
# usage: GH_TOKEN=... ci_monitor.py <owner/repo> <check run id> <progress file> <log file>
import ctypes, ctypes.util, json, os, subprocess, sys, time, urllib.error, urllib.request
repo, check_id, progress_path, log_path = sys.argv[1:5]
token = os.environ["GH_TOKEN"]
libc = ctypes.CDLL(ctypes.util.find_library("c"))
libproc = ctypes.CDLL("/usr/lib/libproc.dylib")
mach_task_self = ctypes.c_uint.in_dll(libc, "mach_task_self_").value

class TaskBasicInfo(ctypes.Structure): # mach_task_basic_info
  _pack_ = 4
  _fields_ = [("virtual_size", ctypes.c_uint64), ("resident_size", ctypes.c_uint64), ("resident_size_max", ctypes.c_uint64),
              ("user_time", ctypes.c_int32 * 2), ("system_time", ctypes.c_int32 * 2), ("policy", ctypes.c_int32), ("suspend_count", ctypes.c_int32)]
MACH_TASK_BASIC_INFO = 20

def all_pids():
  buf = (ctypes.c_int * 8192)()
  n = libproc.proc_listallpids(buf, ctypes.sizeof(buf))
  return [buf[i] for i in range(max(n, 0)) if buf[i] > 0]

def name_of(pid):
  b = ctypes.create_string_buffer(4096)
  n = libproc.proc_pidpath(pid, b, 4096)
  return b.value.decode(errors="replace") if n > 0 else "?"

def _unused_suspended_tasks():
  out = []
  for pid in all_pids():
    port = ctypes.c_uint(0)
    if libc.task_name_for_pid(mach_task_self, pid, ctypes.byref(port)) != 0:
      continue
    info = TaskBasicInfo()
    count = ctypes.c_uint(ctypes.sizeof(info) // 4)
    kr = libc.task_info(port, MACH_TASK_BASIC_INFO, ctypes.byref(info), ctypes.byref(count))
    libc.mach_port_deallocate(mach_task_self, port)
    if kr == 0 and info.suspend_count > 0:
      out.append(f"{pid} suspend_count={info.suspend_count} {name_of(pid)}")
  return out

def sample_summary(path):
  try:
    with open(path, errors="replace") as f: text = f.read()
  except OSError: return ""
  a = text.find("Sort by top of stack")
  b = text.find("Binary Images")
  head = text[:3000]
  c = text.find("Call graph:")
  def depth(l):
    for i, ch in enumerate(l):
      if ch.isdigit(): return i
    return 999
  graph = "\n".join(l[:150] for l in text[c:a].splitlines() if depth(l) <= 28)
  return (text[a:b] if a >= 0 else "")[:3000] + "\n--- call graph (shallow)\n" + graph[:30000]

def syslog_tail(path, n):
  # fault/error-level lines, & anything from the kernel
  try:
    with open(path, "rb") as f:
      f.seek(0, 2); size = f.tell(); f.seek(max(0, size - 4000000))
      lines = f.read().decode(errors="replace").splitlines()
  except OSError: return ""
  keep = [l[:200] for l in lines if (" F " in l[:40] or " E " in l[:40] or "kernel[" in l) and "tcp_connection" not in l and "tcp conn" not in l and "map_with_linking" not in l]
  return "\n".join(keep[-n:])

def tail(path, n):
  try:
    with open(path, errors="replace") as f: return "".join(f.readlines()[-n:])
  except OSError: return ""

def post(text):
  body = json.dumps({"output": {"title": "e2e progress", "summary": text[-60000:]}}).encode()
  req = urllib.request.Request(f"https://api.github.com/repos/{repo}/check-runs/{check_id}", data=body, method="PATCH",
                               headers={"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json"})
  try:
    urllib.request.urlopen(req, timeout=10).read()
  except urllib.error.URLError as e:
    if "CERTIFICATE" not in str(e): raise
    # no CA bundle for this python; hand the request to curl instead
    subprocess.run(["curl", "-sS", "-X", "PATCH", "-H", f"Authorization: Bearer {token}", "-H", "Accept: application/vnd.github+json",
                    "--data-binary", "@-", f"https://api.github.com/repos/{repo}/check-runs/{check_id}"], input=body, timeout=10, capture_output=True)

import threading
last_spawn_ok = 0.0
def spawner():
  global last_spawn_ok
  while True:
    try:
      subprocess.run(["/usr/bin/true"], timeout=3)
      last_spawn_ok = time.time()
    except Exception:
      pass
    time.sleep(0.5)
threading.Thread(target=spawner, daemon=True).start()

# NOTE: querying a task can block forever if that task is wedged in the kernel;
# each query runs on its own thread, & a query that doesn't return in time is
# reported (once a pid is known to block, it's skipped).
stuck = {}
susp = []
def query(pid, out):
  port = ctypes.c_uint(0)
  if libc.task_name_for_pid(mach_task_self, pid, ctypes.byref(port)) != 0:
    out.append(None); return
  info = TaskBasicInfo()
  count = ctypes.c_uint(ctypes.sizeof(info) // 4)
  kr = libc.task_info(port, MACH_TASK_BASIC_INFO, ctypes.byref(info), ctypes.byref(count))
  libc.mach_port_deallocate(mach_task_self, port)
  out.append(info.suspend_count if kr == 0 else None)
def scanner():
  global susp
  while True:
    found = []
    for pid in all_pids():
      if pid in stuck: continue
      out = []
      t = threading.Thread(target=query, args=(pid, out), daemon=True)
      t.start(); t.join(1.0)
      if not out:
        stuck[pid] = f"{time.strftime('%H:%M:%S')}"
        stuck[pid] += f" {name_of(pid)}"
      elif out[0]:
        found.append(f"{pid} suspend_count={out[0]} {name_of(pid)}")
    susp = found
    time.sleep(1)
threading.Thread(target=scanner, daemon=True).start()

while True:
  spawn_state = f"last successful spawn {time.time()-last_spawn_ok:.1f} s ago"
  text = "```\n" + time.strftime("%H:%M:%S") + f"\nspawn: {spawn_state}\n--- tasks whose task_info blocks\n" + "\n".join(f"{k} since {v}" for k, v in stuck.items()) + \
         "\n--- suspended tasks\n" + "\n".join(susp) + \
         "\n--- progress\n" + tail(progress_path, 25) + "\n--- syslog\n" + syslog_tail(os.path.join(os.path.dirname(progress_path), "e2e", "syslog.txt"), 60) + "```"
  try: post(text)
  except Exception: pass
  time.sleep(1)
