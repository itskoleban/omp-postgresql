"""Bounded observers for one hosted ASan network execution; no workload retries."""
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import subprocess
import threading
import time


def read_trace(path):
    rows = [json.loads(line) for line in Path(path).read_text().splitlines()]
    if not rows or rows[0].get('type') != 'header' or rows[-1].get('type') != 'footer':
        raise ValueError('Incomplete client trace')
    footer = rows[-1]
    events = rows[1:-1]
    if footer['dropped'] or not footer['write_ok'] or footer['records'] != len(events):
        raise ValueError('Client trace overflow or write loss')
    if [r['seq'] for r in events] != list(range(len(events))):
        raise ValueError('Client trace sequence gap')
    return rows[0], events


def fnv(text):
    value = 14695981039346656037
    for byte in text.encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def summarize(path):
    header, events = read_trace(path)
    cleanup = [e for e in events if e['phase'] == 'deadline' and
               e['scenario'] == fnv('hold') and e['data'] == fnv('DROP TABLE phase2_hold')]
    if len(cleanup) != 1:
        raise ValueError('Expected exactly one hold cleanup dispatch')
    command = cleanup[0]
    matching = [e for e in events if e['job_ptr'] == command['job_ptr'] and e['job'] == command['job'] and
                e['scenario'] == command['scenario']]
    phases = {name: [e for e in matching if e['phase'] == name] for name in
              ('submit', 'worker_start', 'dispatch_enter', 'dispatch_exit',
               'flush_exit', 'consume_enter', 'consume_exit', 'select_exit',
               'getresult_exit', 'sql_confirmed', 'receive_interrupted',
               'wait_expired', 'terminal_status', 'terminal_push_enter',
               'terminal_push_exit', 'poll_delivered')}
    required = ('submit', 'worker_start', 'dispatch_enter', 'dispatch_exit',
                'terminal_status', 'terminal_push_enter', 'terminal_push_exit', 'poll_delivered')
    if any(not phases[name] for name in required):
        raise ValueError('Missing required cleanup phase')
    terminal = phases['terminal_status'][-1]
    entries = [e for e in events if e['phase'] == 'query_enter' and
               e['scenario'] == command['scenario'] and e['data'] == fnv('DROP TABLE phase2_hold')]
    if len(entries) != 1:
        raise ValueError('Missing unambiguous query API entry')
    return dict(pid=header['pid'], query_ms=header['query_ms'], records=len(events),
        cleanup=dict(job=command['job'], connection=command['connection'],
            backend=command['backend'], tid=command['tid'], lane_ptr=command['lane_ptr'],
            deadline_ns=command['deadline_ns'],
            command_start_ns=command['deadline_ns'] - command['rc'] * 1000000,
            query_ms=command['rc'], terminal_status=terminal['rc'],
            query_entry=entries[0],
            error=terminal['data'] & 0xffffffff, completed=terminal['data'] >> 32,
            phases=phases),
        historical_cause_established=False,
        limits=['Trace calls have overhead; terminal queue insertion is bracketed by enter/exit.',
                'UTC values are correlation anchors; monotonic values govern deadlines.',
                'A passing hosted execution does not correct the historical failure.'])


class Collector:
    def __init__(self, build, env, server_log):
        self.directory = Path(build).resolve() / 'network-diagnostic'
        self.directory.mkdir(parents=True, exist_ok=False)
        self.env = dict(env)
        self.server_log = Path(server_log)
        self.stop = threading.Event()
        self.errors = []
        self.threads = []
        self.pid = None

    def provenance(self):
        source = Path(__file__).resolve().parents[1]
        binary = self.directory.parent / 'pg_network_recovery'
        tracked = subprocess.check_output(['git', '-C', str(source), 'ls-files', '-z']).decode().split('\0')
        identities = {name: hashlib.sha256((source / name).read_bytes()).hexdigest()
                      for name in tracked if name and (source / name).is_file()}
        commands = {}
        for name, argv in {
            'compiler': ['c++', '--version'], 'kernel': ['uname', '-a'],
            'libraries': ['ldd', str(binary)],
            'commands': ['ninja', '-C', str(self.directory.parent), '-t', 'commands', 'pg_network_recovery'],
            'symbols': ['nm', '-u', str(binary)],
            'tracked_status': ['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no']
        }.items():
            result = subprocess.run(argv, capture_output=True, timeout=15)
            (self.directory / (name + '.log')).write_bytes(result.stdout + result.stderr)
            commands[name] = dict(argv=argv, exit_code=result.returncode)
            if result.returncode:
                raise RuntimeError('Diagnostic provenance command failed: ' + name)
        if (self.directory / 'tracked_status.log').read_bytes().strip():
            raise RuntimeError('Diagnostic source has tracked modifications')
        symbols = (self.directory / 'symbols.log').read_text()
        if '__asan_' not in symbols or '__ubsan_' not in symbols:
            raise RuntimeError('Diagnostic binary lacks required sanitizer instrumentation')
        runtime = {}
        for line in (self.directory / 'libraries.log').read_text().splitlines():
            fields = line.split()
            if '=>' in fields and len(fields) > 2 and fields[2].startswith('/'):
                path = Path(fields[2]).resolve()
                runtime[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        record = dict(source_sha=subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'],text=True).strip(),
            binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
            source_sha256=identities, runtime_sha256=runtime, commands=commands,
            attempts=1, query_ms=1500,
            ASAN_OPTIONS=self.env.get('ASAN_OPTIONS'), UBSAN_OPTIONS=self.env.get('UBSAN_OPTIONS'),
            CTEST_PARALLEL_LEVEL=self.env.get('CTEST_PARALLEL_LEVEL'),
            run_id=os.getenv('GITHUB_RUN_ID'), run_attempt=os.getenv('GITHUB_RUN_ATTEMPT'))
        (self.directory / 'provenance.json').write_text(json.dumps(record, indent=2)+'\n')
        # Preserve the actual diagnostic executable, not a substitute local build.
        (self.directory / 'pg_network_recovery').write_bytes(binary.read_bytes())

    def guarded(self, function):
        try:
            function()
        except Exception as error:
            self.errors.append(type(error).__name__ + ': ' + str(error))

    def wait_target(self):
        deadline = time.monotonic() + 120
        path = self.directory / 'client.jsonl'
        while not self.stop.is_set() and time.monotonic() < deadline:
            if path.exists():
                try:
                    line = path.open().readline()
                    self.pid = json.loads(line)['pid']
                    return True
                except (ValueError, KeyError):
                    pass
            self.stop.wait(.01)
        return False

    def resources(self):
        if not self.wait_target():
            raise RuntimeError('Network process did not publish its diagnostic identity')
        target = Path('/proc') / str(self.pid)
        deadline = time.monotonic() + 95
        with (self.directory / 'resources.jsonl').open('w') as output:
            while not self.stop.is_set() and target.exists() and time.monotonic() < deadline:
                row = dict(mono_ns=time.monotonic_ns(), utc_ns=time.time_ns(), pid=self.pid, threads={})
                for name in ('/proc/pressure/cpu','/proc/pressure/io','/proc/pressure/memory',
                             '/proc/loadavg','/proc/meminfo','/sys/fs/cgroup/cpu.stat',
                             '/sys/fs/cgroup/memory.events',str(target/'io')):
                    try:
                        row[name] = Path(name).read_text()
                    except OSError as error:
                        row[name] = type(error).__name__
                for task in (target/'task').glob('*'):
                    row['threads'][task.name] = {}
                    for name in ('stat','schedstat','status'):
                        try:
                            row['threads'][task.name][name] = (task/name).read_text()
                        except OSError:
                            pass
                output.write(json.dumps(row)+'\n')
                output.flush()
                self.stop.wait(.02)

    def activity(self):
        if not self.wait_target():
            raise RuntimeError('Network diagnostic activity observer missed process start')
        lib = c.CDLL('libpq.so.5')
        signatures = {'PQconnectdb':([c.c_char_p],c.c_void_p), 'PQstatus':([c.c_void_p],c.c_int),
            'PQexec':([c.c_void_p,c.c_char_p],c.c_void_p), 'PQresultStatus':([c.c_void_p],c.c_int),
            'PQgetvalue':([c.c_void_p,c.c_int,c.c_int],c.c_char_p),
            'PQclear':([c.c_void_p],None), 'PQfinish':([c.c_void_p],None)}
        for name,(args,result) in signatures.items():
            getattr(lib,name).argtypes=args
            getattr(lib,name).restype=result
        # Explicit loopback fields avoid changing the supervising environment.
        service = self.env['PGSERVICEFILE']
        import configparser
        config = configparser.ConfigParser()
        config.read(service)
        fields = dict(config['omp_test'])
        fields['application_name'] = 'network-diagnostic'
        fields['connect_timeout'] = '2'
        text = ' '.join(key+"='"+value.replace('\\','\\\\').replace("'","\\'")+"'" for key,value in fields.items())
        conn = lib.PQconnectdb(text.encode())
        if not conn or lib.PQstatus(conn) != 0:
            if conn: lib.PQfinish(conn)
            raise RuntimeError('Independent diagnostic database connection failed')
        def execute(sql):
            result = lib.PQexec(conn, sql.encode())
            try:
                status = lib.PQresultStatus(result)
                return status, lib.PQgetvalue(result,0,0).decode() if status == 2 else None
            finally:
                lib.PQclear(result)
        execute("SET statement_timeout='100ms'")
        query = """SELECT json_build_object('server_utc',clock_timestamp(),
          'activity',(SELECT coalesce(json_agg(row_to_json(a)),'[]'::json) FROM
            (SELECT pid,backend_start,xact_start,query_start,state_change,state,
             wait_event_type,wait_event,pg_blocking_pids(pid) blocking_pids,query
             FROM pg_stat_activity WHERE application_name='omp-postgresql' ORDER BY pid) a),
          'locks',(SELECT coalesce(json_agg(row_to_json(l)),'[]'::json) FROM
            (SELECT pid,locktype,relation::text,mode,granted,waitstart FROM pg_locks
             WHERE pid IN (SELECT pid FROM pg_stat_activity WHERE application_name='omp-postgresql')
             ORDER BY pid,locktype,relation,mode) l))"""
        try:
            deadline = time.monotonic() + 95
            with (self.directory / 'activity.jsonl').open('w') as output:
                while not self.stop.is_set() and (Path('/proc')/str(self.pid)).exists() and time.monotonic() < deadline:
                    start = time.monotonic_ns()
                    status, value = execute(query)
                    output.write(json.dumps(dict(mono_ns=start,utc_ns=time.time_ns(),
                        elapsed_ns=time.monotonic_ns()-start,status=status,
                        snapshot=json.loads(value) if value else None))+'\n')
                    output.flush()
                    self.stop.wait(.05)
        finally:
            lib.PQfinish(conn)

    def start(self):
        self.provenance()
        for function in (self.resources,self.activity):
            thread = threading.Thread(target=self.guarded,args=(function,),daemon=True)
            thread.start()
            self.threads.append(thread)

    def finish(self):
        self.stop.set()
        for thread in self.threads:
            thread.join(3)
            if thread.is_alive(): self.errors.append('Diagnostic observer did not stop')
        if self.server_log.exists():
            (self.directory/'server.log').write_bytes(self.server_log.read_bytes())
        try:
            summary = summarize(self.directory/'client.jsonl')
            (self.directory/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        except Exception as error:
            self.errors.append(str(error))
        for name in ('resources.jsonl','activity.jsonl'):
            path=self.directory/name
            if not path.exists() or not path.stat().st_size:
                self.errors.append('Missing observer capture: '+name)
        (self.directory/'collection.json').write_text(json.dumps(dict(errors=self.errors,
            status='FAIL' if self.errors else 'PASS', attempts=1),indent=2)+'\n')
        manifest = {p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                    for p in self.directory.iterdir() if p.is_file() and p.name != 'manifest.json'}
        (self.directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        return not self.errors
