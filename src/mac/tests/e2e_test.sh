#!/bin/bash
# End-to-end macOS test: drives raddbg_non_graphical over IPC against
# mac_demon_test_target (function breakpoint, run, debug info lookups,
# expression evaluation, step out). Needs task_for_pid rights (run w/ sudo).
# usage: e2e_test.sh <build dir>
set -u
build="$(cd "$1" && pwd)"
out="$build/e2e"
mkdir -p "$out"
failures=0
check() { # check <description> <file> <pattern>
  if grep -q -- "$3" "$2"; then echo "  [PASS] $1"; else echo "  [FAIL] $1"; echo "::error title=e2e::$1"; failures=$((failures+1)); fi
}

port=$((20000 + RANDOM % 20000))
rm -f "$out/user.raddbg_user"
"$build/raddbg_non_graphical" --ipc_port:$port --no_focus_on_stop --user:"$out/user.raddbg_user" --logs:"$out/logs" > "$out/debugger.txt" 2>&1 < /dev/null &
dbg=$!
# NOTE: every IPC call is time-limited, so a wedged debugger can't hang the test
ipc() { perl -e 'alarm 20; exec @ARGV' "$build/raddbg_non_graphical" --ipc --ipc_port:$port "$@"; }
echo "== waiting for the debugger (pid $dbg, ipc port $port)"
for i in $(seq 1 60); do ipc state 2>&1 | grep -q stop_count && break; sleep 1; done
wait_stop() { # wait_stop <previous stop count> <output file>
  for i in $(seq 1 60); do
    ipc state > "$2" 2>&1
    count=$(sed -n 's/^ *stop_count: *\([0-9]*\).*/\1/p' "$2" | head -1)
    running=$(sed -n 's/^ *running: *\([0-9]*\).*/\1/p' "$2" | head -1)
    if [[ -n "$count" && "$count" != "$1" && "$running" == "0" ]]; then return 0; fi
    sleep 1
  done
  return 1
}

ipc state > "$out/state0.txt" 2>&1
check "debugger answers IPC" "$out/state0.txt" "stop_count"
stop0=$(sed -n 's/^ *stop_count: *\([0-9]*\).*/\1/p' "$out/state0.txt" | head -1)

echo "== add_target, add_function_breakpoint, run"
ipc add_target "$build/mac_demon_test_target" > "$out/cmd_target.txt" 2>&1
ipc add_function_breakpoint target_func > "$out/cmd_bp.txt" 2>&1
ipc run > "$out/cmd_run.txt" 2>&1
wait_stop "$stop0" "$out/state1.txt"
check "stopped in target_func" "$out/state1.txt" 'ip_voff_symbol: "target_func"'
check "line info for the stop" "$out/state1.txt" 'mac_demon_test_target.c'
stop1=$(sed -n 's/^ *stop_count: *\([0-9]*\).*/\1/p' "$out/state1.txt" | head -1)

idle="${E2E_IDLE_AFTER_STOP:-0}"
if [[ "$idle" != 0 ]]; then
  echo "== idle for ${idle}s after the stop"
  end=$((SECONDS + idle))
  while (( SECONDS < end )); do
    if [[ -n "${E2E_SAMPLE:-}" ]]; then
      sample $dbg 2 -mayDie -file "$out/sample.tmp" > /dev/null 2>&1 && mv "$out/sample.tmp" "$out/sample.txt"
      echo "   sampled at $SECONDS s"
    fi
    sleep 2
  done
  echo "== idle done"
fi
if [[ -n "${E2E_EVAL_ONLY:-}" ]]; then
  for i in $(seq 1 ${E2E_EVAL_COUNT:-15}); do echo "== eval $E2E_EVAL_ONLY ($i)"; ipc eval "$E2E_EVAL_ONLY" 2>&1 | grep -E "value|msgs"; sleep 2; done
  echo "== eval-only done"
  kill -9 $dbg 2>/dev/null; pkill -9 -f mac_demon_test_target 2>/dev/null
  exit 0
fi
echo "== eval"
# NOTE: debug info loads asynchronously; evaluations fail until it's in
for i in $(seq 1 20); do
  ipc eval g_counter > "$out/eval.txt" 2>&1
  grep -q 'value: ""' "$out/eval.txt" || break
  sleep 1
done
check "evaluate a global (g_counter == 0 on the first call)" "$out/eval.txt" 'value: "0"'


echo "== step_out"
ipc step_out > "$out/cmd_step_out.txt" 2>&1
wait_stop "$stop1" "$out/state2.txt"
check "step out to target_caller" "$out/state2.txt" 'ip_voff_symbol: "target_caller"'

kill -9 $dbg 2>/dev/null
pkill -9 -f mac_demon_test_target 2>/dev/null
for f in state0 cmd_target cmd_bp cmd_run state1 eval cmd_step_out state2; do echo "--- $f"; cat "$out/$f.txt"; done
echo "e2e: $failures failed"
exit $failures
