# Caps the memory of test processes (macOS, run as root): applies a fatal
# jetsam limit to every process whose command line matches the pattern, so a
# runaway allocation kills that process rather than starving the machine.
# usage: memory_limiter.py <pgrep -f pattern> <limit MB>
import ctypes, subprocess, sys, time
MEMORYSTATUS_CMD_SET_JETSAM_TASK_LIMIT = 6
libc = ctypes.CDLL(None)
pattern, limit_mb = sys.argv[1], int(sys.argv[2])
seen = set()
while True:
  out = subprocess.run(["pgrep", "-f", pattern], capture_output=True, text=True).stdout.split()
  for pid in map(int, out):
    if pid not in seen:
      seen.add(pid)
      r = libc.memorystatus_control(MEMORYSTATUS_CMD_SET_JETSAM_TASK_LIMIT, pid, limit_mb, None, 0)
      print(f"limit {limit_mb} MB -> pid {pid}: {r}", flush=True)
  time.sleep(0.05)
