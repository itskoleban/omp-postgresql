"""Reject incomplete diagnostic evidence without converting SQL uncertainty."""
import json
from pathlib import Path
import tempfile
import unittest

from network_diagnostic import fnv, read_trace, summarize


class DiagnosticEvidenceTests(unittest.TestCase):
    def trace(self, directory, status=2, error=0, completed=1):
        rows=[dict(type='header',schema=1,pid=42,query_ms=1500)]
        names=['query_enter','submit','worker_start','deadline','dispatch_enter','dispatch_exit',
               'terminal_status','terminal_push_enter','terminal_push_exit','poll_delivered']
        for index,name in enumerate(names):
            rows.append(dict(type='event',seq=index,phase=name,job_ptr=100,
                scenario=fnv('hold'),command=1,data=(completed << 32)|error if name=='terminal_status'
                    else fnv('DROP TABLE phase2_hold') if name in ('deadline','query_enter') else 0,
                job=13,connection=1,backend=5253,tid=7,lane_ptr=9,
                deadline_ns=2500000000,mono_ns=1000000000+index,
                rc=1500 if name=='deadline' else status if name=='terminal_status' else 0))
        rows.append(dict(type='footer',records=len(names),dropped=0,write_ok=True))
        path=Path(directory)/'trace.jsonl'
        path.write_text('\n'.join(json.dumps(row) for row in rows)+'\n')
        return path,rows

    def test_uncertain_timeout_remains_uncertain(self):
        with tempfile.TemporaryDirectory() as directory:
            path,_=self.trace(directory,status=4,error=6,completed=0)
            result=summarize(path)
            self.assertEqual((result['cleanup']['terminal_status'],result['cleanup']['error'],
                              result['cleanup']['completed']),(4,6,0))
            self.assertFalse(result['historical_cause_established'])
            self.assertEqual(result['cleanup']['command_start_ns'],1000000000)

    def test_overflow_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path,rows=self.trace(directory)
            rows[-1]['dropped']=1
            path.write_text('\n'.join(map(json.dumps,rows)))
            with self.assertRaisesRegex(ValueError,'overflow'):
                read_trace(path)

    def test_missing_footer_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path,rows=self.trace(directory)
            path.write_text('\n'.join(map(json.dumps,rows[:-1])))
            with self.assertRaisesRegex(ValueError,'Incomplete'):
                read_trace(path)

    def test_missing_phase_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path,rows=self.trace(directory)
            rows[2]['phase']='unrelated'
            path.write_text('\n'.join(map(json.dumps,rows)))
            with self.assertRaisesRegex(ValueError,'Missing required'):
                summarize(path)


if __name__ == '__main__':
    unittest.main()
