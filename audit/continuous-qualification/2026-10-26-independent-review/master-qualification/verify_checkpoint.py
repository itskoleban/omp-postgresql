"""Verify preserved CI observations without a network connection or test rerun."""
from pathlib import Path
import hashlib
import json
import zipfile
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parent
def digest(data):
    return hashlib.sha256(data).hexdigest()
def read_json(path):
    return json.loads((ROOT/path).read_bytes())

ledger=read_json('sha256.json')
for row in ledger:
    data=(ROOT/row['path']).read_bytes()
    assert len(data)==row['bytes'] and digest(data)==row['sha256'], row['path']
index=read_json('raw-index.json')
report=read_json('verification.json')
run=read_json('run-final.json')
jobs=read_json('jobs-final.json')['jobs']
artifacts={row['id']:row for row in read_json('artifacts-final.json')['artifacts']}
source=index['source_sha']
assert run['id']==index['run_id']==report['run_id']
assert run['head_sha']==source==report['source_sha']
assert run['run_attempt']==index['run_attempt']==1
assert run['status']=='completed' and run['conclusion']=='success'
assert run['head_commit']['tree_id']==index['source_tree']==report['source_tree']
assert len(jobs)==7 and all(job['conclusion']=='success' for job in jobs)
raw=(ROOT/index['archive']['path']).read_bytes()
assert len(raw)==index['archive']['bytes'] and digest(raw)==index['archive']['sha256']
with zipfile.ZipFile(ROOT/index['archive']['path']) as archive:
    assert len(index['members'])==len(archive.namelist())
    assert set(archive.namelist())=={row['path'] for row in index['members']}
    selected={}
    for row in index['members']:
        data=archive.read(row['path'])
        assert len(data)==row['bytes'] and digest(data)==row['sha256'],row['path']
        art=artifacts[row['source_artifact_id']]
        assert art['workflow_run']['head_sha']==source
        assert art['name']==row['source_artifact_name']
        assert art['digest']=='sha256:'+row['source_artifact_archive_sha256']
        assert art['size_in_bytes']==row['source_artifact_archive_bytes']
        selected[row['path']]=data

    junit=[row for row in index['members'] if row['role']=='ctest-junit']
    counts={}
    for row in junit:
        root=ET.fromstring(selected[row['path']])
        cases=root.findall('.//testcase')
        assert len(cases)==int(root.get('tests'))
        assert all(int(root.get(key,'0'))==0 for key in ('failures','errors','disabled','skipped'))
        assert all(case.find('failure') is None and case.find('error') is None and case.find('skipped') is None for case in cases)
        allocation=next(case for case in cases if case.get('name')=='confirmed_result_allocation_integration')
        assert 'confirmed_result_allocation PASS scenarios=7' in allocation.findtext('system-out','')
        counts[row['source_artifact_name']]=len(cases)
    assert counts=={'linux-x86-test-logs':35,'linux-x64-test-logs-ON':33,'linux-x64-test-logs-OFF':35,'windows-x64-test-logs':28,'windows-x86-test-logs':30,'linux-x64-test-logs-THREAD':39}
    assert sum(counts.values())==200

    tsan_row=next(row for row in index['members'] if row['original_member_path'].endswith('tsan-evidence/manifest.json'))
    manifest=json.loads(selected[tsan_row['path']])
    assert manifest['status']=='PASS' and manifest['source_sha']==source and not manifest['tracked_source_modified']
    prefix=tsan_row['path'].removesuffix('manifest.json')
    for path,record in manifest['public_files'].items():
        data=selected[prefix+path]
        assert len(data)==record['bytes'] and digest(data)==record['sha256']
    assert selected[prefix+'commands/source-sha.stdout.txt'].decode().strip()==source
    assert selected[prefix+'commands/tracked-status.stdout.txt']==b''
    for target in manifest['targets']:
        assert '-fsanitize=thread' in target['compile_argv'] and '-fsanitize=thread' in target['link_argv']
        assert target['required_undefined_symbols']

    sessions=[json.loads(selected[row['path']]) for row in index['members'] if row['role']=='sanitized-session-results']
    assert len(sessions)==4
    for session in sessions:
        assert session['source_commit']==source
        provenance=session['source_provenance']
        assert not provenance['tracked_source_modified'] and provenance['tracked_changes']==[]
        assert provenance['head_tree']==index['source_tree']
        results=session['results']
        assert len(results)==23 and len({row['id'] for row in results})==23
        assert all(row['status']=='PASS' for row in results)
    package_manifests={}
    for row in index['members']:
        if row['role']=='package-original-manifest':
            pkg=json.loads(selected[row['path']])
            assert pkg['commit']==source
            package_manifests[pkg['platform']]=(pkg, digest(selected[row['path']]))
    assert set(package_manifests)=={'linux','windows'}
    for platform,(pkg,manifest_hash) in package_manifests.items():
        session_platform={'linux':'linux','windows':'win32'}[platform]
        pair=[session for session in sessions if session['platform']==session_platform]
        assert len(pair)==2
        built=next(session for session in pair if 'package' not in session)
        installed=next(session for session in pair if 'package' in session)
        assert [row['id'] for row in built['results']]==[row['id'] for row in installed['results']]
        identity=installed['package']
        assert identity['archive']==pkg['archive'] and identity['manifest_sha256']==manifest_hash
        component='components/omp-postgresql.'+('dll' if platform=='windows' else 'so')
        assert identity['component_sha256']==pkg['files'][component]==installed['inputs']['component']['sha256']
        runtimes={Path(path).name:pkg['files'][path] for path in pkg['dependencies'] if path!=component}
        assert identity['runtime_sha256']==runtimes
        assert {row['file']:row['sha256'] for row in installed['runtime']}==runtimes

print('PASS: archived evidence verifies 7 jobs, 200 CTest cases without failures or skips, TSan provenance, and four 23-case real-session suites with installed-package identities.')
print('This verifies preserved observations; it does not rerun tests, reconstruct binaries, resolve historical failures, or authorize prolonged stability or publication.')
