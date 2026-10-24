"""Require the selected real-worker omission to fail its own checkpoint assertion."""
import subprocess
import sys

scenario = sys.argv[2]
result = subprocess.run([sys.argv[1], "--omit-sites", scenario], capture_output=True, text=True, timeout=25)
sys.stdout.write(result.stdout)
sys.stderr.write(result.stderr)
expected = "Missing scenario checkpoints: " + scenario
if result.returncode != 1 or expected not in result.stderr:
    raise SystemExit("Worker negative control did not reject the selected missing injections")
if f"scenario={scenario} interrupted=0 terminal_message=0 denied=0" not in result.stdout:
    raise SystemExit("Worker negative control did not observe an empty scenario delta")
print("Worker negative control: missing " + scenario + " checkpoints rejected")
