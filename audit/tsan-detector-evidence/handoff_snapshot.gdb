set pagination off
set confirm off
set disable-randomization off
set environment TSAN_OPTIONS halt_on_error=1:exitcode=66
python
import gdb
import hashlib
import json
from pathlib import Path
events = {}
def record_exit(event):
    events["exit_code"] = getattr(event, "exit_code", None)
gdb.events.exited.connect(record_exit)
end
break main
run
python
target = int(gdb.parse_and_eval("&value"))
phase = int(gdb.parse_and_eval("&phase"))
main_thread = gdb.selected_thread().num
print("HANDOFF_SNAPSHOT target_value=0x%x phase=0x%x" % (target, phase))
gdb.execute("delete breakpoints")
snapshot = gdb.Breakpoint("*((char*)__tsan_write4 + 0x4c)")
snapshot.condition = "$rax == 0x%x && $_thread != %d" % (target, main_thread)
gdb.execute("continue")
first = gdb.selected_thread()
first_num = first.num
phase_before = int(gdb.parse_and_eval("*((unsigned*)0x%x)" % phase))
print("HANDOFF_SNAPSHOT first_thread=%d phase=%d" % (first.num, phase_before))
gdb.execute("info registers rax r8 r9 xmm0")
if phase_before != 0:
    raise RuntimeError("First callback did not precede phase publication")
print("HANDOFF_SNAPSHOT second writer cannot reach value while phase remains zero")
gdb.execute("continue")
second = gdb.selected_thread()
second_num = second.num
phase_after = int(gdb.parse_and_eval("*((unsigned*)0x%x)" % phase))
print("HANDOFF_SNAPSHOT second_thread=%d phase=%d" % (second.num, phase_after))
gdb.execute("info registers rax r8 r9 xmm0")
if second.num == first_num or phase_after != 1:
    raise RuntimeError("Second callback did not follow phase publication")
snapshot.delete()
print("HANDOFF_SNAPSHOT resumed second callback must detect first worker shadow")
gdb.execute("continue")
binary = Path(gdb.current_progspace().filename)
events.update(binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
              gdb_version=gdb.VERSION, phase_at_first_snapshot=phase_before,
              phase_at_second_snapshot=phase_after,
              first_thread=first_num, second_thread=second_num,
              scope="Debugger schedule verification, not full component qualification")
Path("/investigation/fixture-experiments/handoff-snapshot-result.json").write_text(json.dumps(events, indent=2)+"\n")
end
quit
