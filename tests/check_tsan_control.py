"""Require an actual TSan race report, never accept a crash as detection."""
import os
import subprocess
import sys

result = subprocess.run([sys.argv[1]], capture_output=True, text=True,
                        env=dict(os.environ, TSAN_OPTIONS="halt_on_error=1:exitcode=66"), timeout=15)
sys.stdout.write(result.stdout)
sys.stderr.write(result.stderr)
if result.returncode != 66 or "WARNING: ThreadSanitizer: data race" not in result.stderr:
    raise SystemExit("TSan negative control did not detect the intentional race")
print("TSan detector control: intentional race rejected with exit 66")
