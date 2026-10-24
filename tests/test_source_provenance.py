"""Real disposable Git trees, including generated files and tracked edits."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).parent))
from source_provenance import capture, parse_status


class SourceProvenanceTests(unittest.TestCase):
    def make_repo(self, root):
        def git(*args):
            return subprocess.check_output(["git",*args],cwd=root,stderr=subprocess.DEVNULL)
        git("init","-q")
        git("config","user.name","Fixture")
        git("config","user.email","fixture@example.invalid")
        (root/"tracked.txt").write_text("original\n")
        (root/".gitignore").write_text("/build*/\n")
        git("add",".");git("commit","-qm","fixture")
        return git

    def test_generated_files_keep_source_dirty_true_without_tracked_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.make_repo(root)
            clean=capture(root)
            self.assertFalse(clean["source_dirty"])
            (root/"libpq32").mkdir();(root/"libpq32/dummy").write_text("fixture")
            (root/"postgres.zip").write_bytes(b"fixture")
            (root/"build-ignored").mkdir();(root/"build-ignored/x").write_text("ignored")
            observed=capture(root)
            self.assertEqual(observed["source_commit"],clean["source_commit"])
            self.assertTrue(observed["source_dirty"])
            self.assertFalse(observed["tracked_source_modified"])
            self.assertEqual(observed["source_state"],"generated-files-only")
            self.assertEqual({v["path"] for v in observed["untracked_entries"]},{"libpq32/","postgres.zip"})
            self.assertIsNone(observed["tracked_patch_sha256"])

    def test_staged_unstaged_deleted_and_renamed_files_are_not_hidden(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);git=self.make_repo(root)
            (root/"tracked.txt").write_text("edited\n")
            unstaged=capture(root)
            self.assertTrue(unstaged["tracked_source_modified"])
            self.assertEqual(unstaged["source_state"],"tracked-modified")
            self.assertIsNotNone(unstaged["tracked_patch_sha256"])
            git("add","tracked.txt")
            self.assertTrue(capture(root)["tracked_source_modified"])
            git("commit","-qm","edit")
            git("mv","tracked.txt","renamed file.txt")
            row=capture(root)["tracked_changes"][0]
            self.assertEqual(row["path"],"renamed file.txt")
            self.assertEqual(row["original_path"],"tracked.txt")
            git("commit","-qm","rename")
            (root/"renamed file.txt").unlink()
            self.assertTrue(capture(root)["tracked_source_modified"])

    def test_unknown_untracked_file_stays_visible_and_unclassified(self):
        tracked,untracked=parse_status(b"?? unknown.txt\0")
        self.assertEqual(tracked,[])
        self.assertEqual(untracked,[{"path":"unknown.txt","classification":"unclassified"}])


if __name__ == "__main__":
    unittest.main()
