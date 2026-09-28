#!/usr/bin/env python3
import gzip
import importlib.util
import json
import os
import sqlite3
import stat
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKER = os.path.join(ROOT, "scripts", "smart_space_knowledge.py")
SPEC = importlib.util.spec_from_file_location("smart_space_knowledge", WORKER)
KNOWLEDGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(KNOWLEDGE)


def text(value):
    raw = value.encode("utf-8")
    return struct.pack("<I", len(raw)) + raw


def make_stream(path):
    items = [
        ("/docs/宁波银行2025年度报告.pdf", "宁波银行2025年度报告.pdf",
         "PDF", "宁波银行在2025年年度报告中披露资产质量与经营情况。"),
        ("/docs/银河麒麟安全测评.docx", "银河麒麟安全测评.docx",
         "document", "银河麒麟操作系统通过安全可靠测评。"),
    ]
    with gzip.open(path, "wb") as stream:
        stream.write(b"UKFIDX1\n")
        stream.write(struct.pack("<I", 1))
        metadata = json.dumps({"generatedAt": "2026-07-24T12:00:00"}).encode()
        stream.write(struct.pack("<I", len(metadata)))
        stream.write(metadata)
        stream.write(struct.pack("<I", len(items)))
        for item_path, name, category, content in items:
            stream.write(b"\0")
            stream.write(struct.pack("<q", 100))
            stream.write(struct.pack("<q", 1000))
            for value in (item_path, "/docs", name, category, content,
                          "test", "done", ""):
                stream.write(text(value))


class KnowledgeWorkerTest(unittest.TestCase):
    def run_worker(self, *arguments, env=None, check=True):
        completed = subprocess.run(
            [sys.executable, WORKER, *arguments],
            check=check, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, env=env)
        lines = [json.loads(line) for line in completed.stdout.splitlines()
                 if line.strip()]
        return completed, lines

    def test_build_search_and_resume(self):
        with tempfile.TemporaryDirectory() as directory:
            stream = os.path.join(directory, "index.gz")
            db = os.path.join(directory, "knowledge.sqlite")
            make_stream(stream)
            build = subprocess.run(
                [sys.executable, WORKER, "build", "--index-stream", stream,
                 "--db", db, "--chunk-size", "300", "--overlap", "30"],
                check=True, stdout=subprocess.PIPE, text=True)
            final = json.loads(build.stdout.strip().splitlines()[-1])
            self.assertEqual(final["documents"], 2)
            self.assertGreaterEqual(final["chunks"], 2)
            self.assertEqual(
                stat.S_IMODE(os.stat(db).st_mode), 0o600)

            search = subprocess.run(
                [sys.executable, WORKER, "search", "--db", db,
                 "--query", "宁波银行 2025 报告"],
                check=True, stdout=subprocess.PIPE, text=True)
            result = json.loads(search.stdout)
            self.assertEqual(result["results"][0]["name"],
                             "宁波银行2025年度报告.pdf")
            self.assertGreaterEqual(result["results"][0]["score"], 65)

            second = subprocess.run(
                [sys.executable, WORKER, "build", "--index-stream", stream,
                 "--db", db],
                check=True, stdout=subprocess.PIPE, text=True)
            resumed = json.loads(second.stdout.strip().splitlines()[-1])
            self.assertEqual(resumed["updated"], 0)
            self.assertEqual(resumed["reused"], 2)
            with sqlite3.connect(db) as handle:
                self.assertEqual(handle.execute(
                    "PRAGMA integrity_check").fetchone()[0], "ok")
                schema_version = handle.execute(
                    "SELECT value FROM meta WHERE key='schema_version'"
                ).fetchone()[0]
                self.assertEqual(schema_version, "2")
                document_columns = {
                    row[1] for row in handle.execute(
                        "PRAGMA table_info(documents)")
                }
                self.assertNotIn("summary", document_columns)
                self.assertNotIn("metadata_json", document_columns)
                self.assertEqual(handle.execute(
                    """SELECT COUNT(*) FROM chunks_fts AS f
                       LEFT JOIN chunks AS c ON c.id=f.chunk_id
                       WHERE c.id IS NULL""").fetchone()[0], 0)

    def test_long_query_relaxes_only_after_strict_search_is_empty(self):
        with tempfile.TemporaryDirectory() as directory:
            stream = os.path.join(directory, "index.gz")
            db = os.path.join(directory, "knowledge.sqlite")
            make_stream(stream)
            self.run_worker(
                "build", "--index-stream", stream, "--db", db)
            _completed, lines = self.run_worker(
                "search", "--db", db,
                "--query", "宁波银行经营分析报告")
            self.assertEqual(lines[-1]["results"][0]["name"],
                             "宁波银行2025年度报告.pdf")
            self.assertTrue(lines[-1]["results"][0]["relaxed"])
            self.assertLess(lines[-1]["results"][0]["score"], 95)

    def test_rejects_oversized_legacy_json_and_unknown_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            legacy = os.path.join(directory, "legacy.json")
            db = os.path.join(directory, "knowledge.sqlite")
            with open(legacy, "wb") as handle:
                handle.truncate(KNOWLEDGE.MAX_LEGACY_JSON_BYTES + 1)
            completed, lines = self.run_worker(
                "build", "--index-json", legacy, "--db", db, check=False)
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("too large", lines[-1]["error"])
            with sqlite3.connect(db) as handle:
                handle.execute(
                    "UPDATE meta SET value='999' WHERE key='schema_version'")
                handle.commit()
            completed, lines = self.run_worker(
                "stats", "--db", db, check=False)
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("unsupported knowledge schema", lines[-1]["error"])


if __name__ == "__main__":
    unittest.main()
