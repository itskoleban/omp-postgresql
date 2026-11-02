"""Run explicit Windows resource controls; the caller owns the disposable fixture.

Set OMP_PG_TEST_SERVICE and PGSERVICEFILE for an isolated disposable PostgreSQL
cluster before invocation. This supervisor never provisions or stops PostgreSQL,
changes profiles, or copies service credentials into evidence. Handle counts are
observations, not statistical assertions of leak freedom.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


MODES = ('observer', 'empty-engine', 'thread', 'shell', 'winsock',
         'libpq-plain', 'libpq-tls', 'libpq-thread', 'engine')
DEFAULT_MODES = ('observer', 'shell', 'libpq-thread', 'engine')


def repetitions(value):
    count = int(value)
    if not 1 <= count <= 10:
        raise argparse.ArgumentTypeError('Repetitions must be in 1..10')
    return count


def postexit_handles(child):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.GetProcessHandleCount.restype = wintypes.BOOL
    count = wintypes.DWORD()
    ctypes.set_last_error(0)
    success = kernel.GetProcessHandleCount(wintypes.HANDLE(int(child._handle)), ctypes.byref(count))
    return dict(ok=bool(success), count=count.value if success else None,
                error=ctypes.get_last_error())


def run_one(binary, mode, log_path):
    record = dict(mode=mode, pid=None, exit_code=None, status='FAIL', timed_out=False,
                  process_shutdown_confirmed=False, postexit_handles=None)
    child = None
    try:
        with log_path.open('xb') as output:
            child = subprocess.Popen([str(binary), mode], stdout=output, stderr=subprocess.STDOUT)
            record['pid'] = child.pid
            try:
                record['exit_code'] = child.wait(timeout=60)
            except subprocess.TimeoutExpired:
                record['timed_out'] = True
                child.kill()
                record['exit_code'] = child.wait(timeout=10)
        record['process_shutdown_confirmed'] = child.poll() is not None
        if record['process_shutdown_confirmed']:
            record['postexit_handles'] = postexit_handles(child)
        if (record['exit_code'] == 0 and not record['timed_out'] and
                record['process_shutdown_confirmed'] and record['postexit_handles']['ok']):
            record['status'] = 'PASS'
    except Exception as error:
        record['error'] = type(error).__name__ + ': ' + str(error)
        # A supervisor-side error must not abandon a child it already launched.
        if child is not None and child.poll() is None:
            try:
                child.kill()
                record['exit_code'] = child.wait(timeout=10)
                record['process_shutdown_confirmed'] = child.poll() is not None
            except Exception as cleanup_error:
                record['cleanup_error'] = type(cleanup_error).__name__ + ': ' + str(cleanup_error)
    try:
        if log_path.is_file():
            record['log'] = dict(name=log_path.name, sha256=hashlib.sha256(log_path.read_bytes()).hexdigest())
    except OSError as error:
        record['status'] = 'FAIL'
        record['log_error'] = type(error).__name__ + ': ' + str(error)
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path, help='Built Windows resource diagnostic executable')
    parser.add_argument('--output', required=True, type=Path, help='New or empty evidence directory')
    parser.add_argument('--repetitions', default=3, type=repetitions)
    parser.add_argument('--mode', action='append', choices=MODES, help='Repeat for multiple modes')
    args = parser.parse_args(argv)
    if os.name != 'nt':
        parser.error('Windows is required')
    service_file = os.environ.get('PGSERVICEFILE')
    if not os.environ.get('OMP_PG_TEST_SERVICE') or not service_file or not Path(service_file).is_file():
        parser.error('Set OMP_PG_TEST_SERVICE and an existing PGSERVICEFILE for a disposable fixture')
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error('Diagnostic binary does not exist')
    modes = args.mode or list(DEFAULT_MODES)
    if len(modes) != len(set(modes)):
        parser.error('Each mode may be selected only once')
    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('Evidence output must be new or empty')
    output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[1]
    inputs = [Path(__file__).resolve(), source / 'tests/windows_resource_probe.cpp',
              source / 'tests/windows_resource_handles.hpp', binary]
    provenance = dict(schema=1, binary=str(binary), modes=modes, repetitions=args.repetitions,
        child_timeout_seconds=60, cleanup_timeout_seconds=10,
        inputs=[dict(name=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for path in inputs],
        limits=['Caller owns the isolated PostgreSQL fixture.',
                'No service credentials or profiles are retained.',
                'Finite resource observations do not establish absence of leaks.'])
    (output / 'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n', encoding='utf8')
    results = []
    status = 'FAIL'
    try:
        for mode in modes:
            for attempt in range(args.repetitions):
                record = run_one(binary, mode, output / f'{mode}-{attempt}.log')
                record['attempt'] = attempt
                results.append(record)
                print(json.dumps(record), flush=True)
                if record['status'] != 'PASS':
                    return 1
        status = 'PASS'
        return 0
    finally:
        (output / 'results.json').write_text(json.dumps(dict(status=status, results=results), indent=2)+'\n', encoding='utf8')


if __name__ == '__main__':
    raise SystemExit(main())
