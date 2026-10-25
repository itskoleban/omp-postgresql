set pagination off
set confirm off
set disable-randomization off
set environment TSAN_OPTIONS halt_on_error=1:exitcode=66
set logging file /investigation/fixture-experiments/shadow-collision-gdb.log
set logging overwrite on
set logging enabled on
break main
run
python
import gdb
target = int(gdb.parse_and_eval("&value"))
main_thread = gdb.selected_thread().num
print("SHADOW_COLLISION target_value=0x%x main_thread=%d" % (target, main_thread))
gdb.execute("delete breakpoints")
snapshot = gdb.Breakpoint("*((char*)__tsan_write4 + 0x4c)")
snapshot.condition = "$rax == 0x%x && $_thread != %d" % (target, main_thread)
gdb.execute("continue")
first = gdb.selected_thread()
print("SHADOW_COLLISION first_snapshot_thread=%d" % first.num)
gdb.execute("info registers rax r8 r9 xmm0")
gdb.execute("x/4wx $r9+$r8*2")
workers = []
for thread in gdb.selected_inferior().threads():
    if thread.num in (main_thread, first.num):
        continue
    thread.switch()
    frame = gdb.newest_frame()
    names = []
    while frame:
        names.append(frame.name() or "")
        frame = frame.older()
    print("SHADOW_COLLISION candidate_thread=%d frames=%s" % (thread.num, names))
    if any("_M_run" in name and "main" in name for name in names):
        workers.append(thread)
if len(workers) != 1:
    raise RuntimeError("Expected one other fixture worker, got %d" % len(workers))
gdb.execute("set scheduler-locking on")
workers[0].switch()
gdb.execute("continue")
second = gdb.selected_thread()
print("SHADOW_COLLISION second_snapshot_thread=%d" % second.num)
gdb.execute("info registers rax r8 r9 xmm0")
gdb.execute("x/4wx $r9+$r8*2")
if second.num != workers[0].num:
    raise RuntimeError("Unexpected snapshot thread")
snapshot.delete()
gdb.execute("set scheduler-locking off")
print("SHADOW_COLLISION both_callbacks_hold_old_shadow_snapshot; resume original program")
gdb.execute("continue")
end
set logging enabled off
quit
