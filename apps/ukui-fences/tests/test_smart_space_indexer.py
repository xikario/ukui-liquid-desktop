#!/usr/bin/env python3
import importlib.util
import gzip
import http.server
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import types
import unittest
import zipfile
from pathlib import Path
from unittest import mock


PROJECT = Path(__file__).resolve().parents[1]
INDEXER = PROJECT / "scripts" / "smart_space_indexer.py"
SPEC = importlib.util.spec_from_file_location("smart_space_indexer", INDEXER)
INDEXER_MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INDEXER_MODULE)


def write_zip(path, members):
    with zipfile.ZipFile(str(path), "w") as archive:
        for name, value in members.items():
            archive.writestr(name, value)


class SmartSpaceIndexerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="smart-space-test-")
        self.root = Path(self.temporary.name) / "root"
        self.root.mkdir()
        self.output = Path(self.temporary.name) / "index.json"

    def tearDown(self):
        self.temporary.cleanup()

    def run_indexer(self, *extra, env=None):
        completed = subprocess.run(
            [sys.executable, str(INDEXER), "--roots-json",
             json.dumps([str(self.root)], ensure_ascii=False),
             "--output", str(self.output), *extra],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=120, check=False, env=env,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return json.loads(self.output.read_text(encoding="utf-8")), completed

    def test_common_document_formats_and_tagspaces_metadata(self):
        (self.root / "说明[文档].md").write_text("智能空间 文件索引", encoding="utf-8")
        write_zip(self.root / "需求.docx", {
            "word/document.xml": "<w:document xmlns:w='w'><w:t>Word 中文内容</w:t></w:document>"
        })
        write_zip(self.root / "演示.pptx", {
            "ppt/slides/slide1.xml": "<p:sld xmlns:p='p'><p:t>PPTX 产品介绍</p:t></p:sld>"
        })
        write_zip(self.root / "台账.xlsx", {
            "xl/sharedStrings.xml": "<sst><si><t>XLSX 数据台账</t></si></sst>"
        })
        write_zip(self.root / "WPS文档.wps", {
            "word/document.xml": "<w:document xmlns:w='w'><w:t>WPS ZIP 中文内容</w:t></w:document>"
        })
        folder = self.root / "项目"
        folder.mkdir()
        (folder / ".ts").mkdir()
        (folder / ".ts" / "tsm.json").write_text(
            json.dumps({"id": "folder-id", "tags": [{"title": "重点"}]}, ensure_ascii=False),
            encoding="utf-8")
        sidecars = self.root / ".ts"
        sidecars.mkdir()
        (sidecars / "需求.docx.json").write_text(
            json.dumps({"id": "file-id", "tags": [{"title": "审核"}]}, ensure_ascii=False),
            encoding="utf-8")

        payload, _ = self.run_indexer()
        stream_path = Path(str(self.output) + ".ui.bin.gz")
        self.assertTrue(stream_path.is_file())
        streamed_paths = set()
        with gzip.open(stream_path, "rb") as stream:
            self.assertEqual(stream.read(8), b"UKFIDX1\n")
            version, metadata_length = struct.unpack("<II", stream.read(8))
            metadata = json.loads(stream.read(metadata_length).decode("utf-8"))
            item_count, = struct.unpack("<I", stream.read(4))
            for _ in range(item_count):
                stream.read(struct.calcsize("<Bqq"))
                values = []
                for _key in range(8):
                    length, = struct.unpack("<I", stream.read(4))
                    values.append(stream.read(length).decode("utf-8"))
                streamed_paths.add(values[0])
            self.assertEqual(stream.read(), b"")
        self.assertEqual(version, 1)
        self.assertEqual(metadata["uiStreamVersion"], 1)
        self.assertEqual(metadata["uiStreamItems"], len(payload["items"]))
        self.assertEqual(item_count, len(payload["items"]))
        self.assertEqual(
            streamed_paths,
            {item["path"] for item in payload["items"]})
        by_name = {item["name"]: item for item in payload["items"]}
        self.assertIn("智能空间", by_name["说明[文档].md"]["content"])
        self.assertIn("文档", by_name["说明[文档].md"]["tags"])
        self.assertIn("Word 中文内容", by_name["需求.docx"]["content"])
        self.assertIn("审核", by_name["需求.docx"]["tags"])
        self.assertIn("PPTX 产品介绍", by_name["演示.pptx"]["content"])
        self.assertIn("XLSX 数据台账", by_name["台账.xlsx"]["content"])
        self.assertIn("WPS ZIP 中文内容", by_name["WPS文档.wps"]["content"])
        self.assertEqual(by_name["WPS文档.wps"]["extractor"], "wps-zip")
        self.assertIn("重点", by_name["项目"]["tags"])
        self.assertEqual(by_name["演示.pptx"]["category"], "presentation")
        self.assertEqual(by_name["台账.xlsx"]["category"], "spreadsheet")

    def test_wps_et_uses_read_only_biff_extraction_when_available(self):
        class Cell:
            def __init__(self, value, cell_type=1):
                self.value = value
                self.ctype = cell_type

        class Sheet:
            name = "销售数据"
            nrows = 2

            @staticmethod
            def row(index):
                return ([Cell("客户"), Cell("金额")]
                        if index == 0 else [Cell("宁波银行"), Cell(12800.0)])

        class Workbook:
            datemode = 0

            @staticmethod
            def sheet_names():
                return ["销售数据"]

            @staticmethod
            def sheet_by_name(_name):
                return Sheet()

            @staticmethod
            def release_resources():
                pass

        fake_xlrd = types.SimpleNamespace(
            XL_CELL_EMPTY=0, XL_CELL_TEXT=1, XL_CELL_NUMBER=2,
            XL_CELL_DATE=3, XL_CELL_BOOLEAN=4, XL_CELL_ERROR=5,
            XL_CELL_BLANK=6,
            open_workbook=lambda *_args, **_kwargs: Workbook(),
            xldate_as_datetime=lambda *_args: None,
        )
        workbook_path = self.root / "宁波银行.et"
        workbook_path.write_bytes(b"mock compound document")
        with mock.patch.dict(sys.modules, {"xlrd": fake_xlrd}):
            content, extractor = INDEXER_MODULE.extract_content(
                workbook_path, False, 12_000, 5)

        self.assertEqual(extractor, "xlrd-wps-et")
        self.assertIn("[销售数据]", content)
        self.assertIn("宁波银行", content)
        self.assertIn("12800", content)

    def test_idle_full_pdf_ocr_runs_after_global_text_budget_is_full(self):
        pdf = self.root / "after-budget.pdf"
        pdf.write_bytes(b"%PDF-test")
        stats = {"reused": 0, "extracted": 0, "deleted": 0,
                 "contentChars": 0, "truncated": 0}
        with mock.patch.object(
                INDEXER_MODULE, "extract_content",
                return_value=("all page OCR evidence", "pdf-tesseract")) as extract:
            item = INDEXER_MODULE.entry_for(
                pdf, self.root, False, True, 100_000, {}, stats,
                [0], 0, False)
        extract.assert_called_once()
        self.assertEqual(extract.call_args.args[3], 0)
        self.assertEqual(item["extractor"], "pdf-tesseract")
        self.assertEqual(item["content"], "")  # JSON budget remains bounded
        self.assertEqual(stats["extracted"], 1)

    def test_incremental_reuse_change_and_delete(self):
        first = self.root / "first.txt"
        second = self.root / "second.txt"
        first.write_text("alpha", encoding="utf-8")
        second.write_text("beta", encoding="utf-8")
        initial, _ = self.run_indexer()
        self.assertGreaterEqual(initial["stats"]["extracted"], 2)

        unchanged, _ = self.run_indexer()
        self.assertGreaterEqual(unchanged["stats"]["reused"], 3)  # root + two files
        self.assertEqual(unchanged["stats"]["extracted"], 0)
        previous = json.loads(Path(str(self.output) + ".previous").read_text(
            encoding="utf-8"))
        self.assertEqual(previous["generatedAt"], initial["generatedAt"])

        time.sleep(0.02)
        first.write_text("alpha changed", encoding="utf-8")
        second.unlink()
        changed, _ = self.run_indexer()
        self.assertEqual(changed["stats"]["extracted"], 1)
        self.assertEqual(changed["stats"]["deleted"], 1)
        paths = {item["path"] for item in changed["items"]}
        self.assertNotIn(str(second), paths)

    def test_zero_item_limit_is_unlimited_and_reused_ocr_is_preserved(self):
        for index in range(1005):
            (self.root / ("file-%04d.txt" % index)).write_text(
                "value", encoding="utf-8")
        payload, _ = self.run_indexer(
            "--max-items", "0", "--no-tags",
            "--max-total-content", "1000000")
        self.assertEqual(len(payload["items"]), 1006)  # root plus all files
        self.assertEqual(payload["stats"]["truncated"], 0)

        path = self.root / "file-0000.txt"
        previous = next(item for item in payload["items"]
                        if item["path"] == str(path))
        previous["content"] = "OCR-result-" + ("x" * 120_000)
        previous["extractor"] = "tesseract"
        previous["ocrEnabled"] = True
        previous["ocrPdfPages"] = 0
        previous["ocrStatus"] = "complete"
        previous["ocrReason"] = "image-ocr"
        stats = {"reused": 0, "extracted": 0, "deleted": 0,
                 "contentChars": 0, "truncated": 0}
        reused = INDEXER_MODULE.entry_for(
            path, self.root, False, False, 2_000, previous, stats,
            [0], 5, False)
        self.assertEqual(reused["content"], previous["content"])
        self.assertEqual(reused["extractor"], "tesseract")
        self.assertEqual(reused["ocrStatus"], "complete")
        self.assertEqual(reused["ocrReason"], "image-ocr")
        self.assertEqual(stats["reused"], 1)

    def test_fast_full_marks_ocr_queue_without_running_ocr(self):
        (self.root / "scan.pdf").write_bytes(b"%PDF metadata-only fixture")
        (self.root / "photo.png").write_bytes(b"not a decoded image")
        (self.root / "notes.txt").write_text("direct searchable text",
                                               encoding="utf-8")
        payload, _ = self.run_indexer("--fast-full", "--no-tags",
                                      "--max-items", "1000")
        by_name = {item["name"]: item for item in payload["items"]}
        self.assertTrue(payload["fullRebuild"])
        self.assertEqual(payload["indexMode"], "fast-full")
        self.assertEqual(payload["capabilities"]["maxItems"], 0)
        self.assertEqual(by_name["scan.pdf"]["ocrStatus"], "pending")
        self.assertEqual(by_name["photo.png"]["ocrStatus"], "pending")
        self.assertEqual(by_name["notes.txt"]["ocrStatus"], "not-needed")
        self.assertEqual(by_name["notes.txt"]["content"],
                         "direct searchable text")

    def test_pending_ocr_updates_only_tagged_items(self):
        scan = self.root / "scan.pdf"
        scan.write_bytes(b"%PDF metadata-only fixture")
        notes = self.root / "notes.txt"
        notes.write_text("keep me", encoding="utf-8")
        payload, _ = self.run_indexer("--fast-full", "--no-tags")
        resume = Path(self.temporary.name) / "ocr.resume.jsonl"
        with mock.patch.object(INDEXER_MODULE.shutil, "which",
                               return_value="/usr/bin/mock-tool"), \
             mock.patch.object(INDEXER_MODULE, "extract_pdf_text_layer",
                               return_value=("", "pdf-no-text")) as preflight, \
             mock.patch.object(INDEXER_MODULE, "ocr_pdf",
                               return_value="OCR searchable evidence") as extract:
            result = INDEXER_MODULE.run_pending_ocr(
                self.output, str(resume), 12_000, 256 * 1024 * 1024,
                19, "idle")
        self.assertEqual(result, 0)
        preflight.assert_called_once()
        extract.assert_called_once()
        completed = json.loads(self.output.read_text(encoding="utf-8"))
        by_name = {item["name"]: item for item in completed["items"]}
        self.assertEqual(by_name["scan.pdf"]["ocrStatus"], "complete")
        self.assertEqual(by_name["scan.pdf"]["content"],
                         "OCR searchable evidence")
        self.assertEqual(by_name["notes.txt"]["content"], "keep me")
        self.assertEqual(completed["stats"]["ocrPending"], 0)
        self.assertFalse(resume.exists())

    def test_pending_ocr_skips_pdf_with_text_layer(self):
        scan = self.root / "word-export.pdf"
        scan.write_bytes(b"%PDF metadata-only fixture")
        self.run_indexer("--fast-full", "--no-tags")
        resume = Path(self.temporary.name) / "ocr.resume.jsonl"
        with mock.patch.object(INDEXER_MODULE.shutil, "which",
                               return_value="/usr/bin/mock-tool"), \
             mock.patch.object(INDEXER_MODULE, "extract_pdf_text_layer",
                               return_value=("native PDF text", "pdftotext")), \
             mock.patch.object(INDEXER_MODULE, "ocr_pdf") as ocr:
            result = INDEXER_MODULE.run_pending_ocr(
                self.output, str(resume), 12_000, 256 * 1024 * 1024,
                19, "idle")
        self.assertEqual(result, 0)
        ocr.assert_not_called()
        completed = json.loads(self.output.read_text(encoding="utf-8"))
        item = next(value for value in completed["items"]
                    if value["name"] == "word-export.pdf")
        self.assertEqual(item["ocrStatus"], "not-needed")
        self.assertEqual(item["content"], "native PDF text")

    def test_pending_ocr_reuses_completed_resume_entry(self):
        scan = self.root / "resume-scan.pdf"
        scan.write_bytes(b"%PDF metadata-only fixture")
        payload, _ = self.run_indexer("--fast-full", "--no-tags")
        original = next(item for item in payload["items"]
                        if item["name"] == "resume-scan.pdf")
        resume = Path(self.temporary.name) / "ocr.resume.jsonl"
        max_total = 256 * 1024 * 1024
        signature = {
            "indexVersion": INDEXER_MODULE.INDEX_VERSION,
            "mode": "ocr-pending",
            "snapshotGeneratedAt": str(payload.get("generatedAt", "")),
            "maxContent": 12_000,
            "maxTotalContent": max_total,
        }
        completed = dict(original)
        completed.update({
            "content": "checkpoint text",
            "extractor": "pdf-tesseract",
            "ocrStatus": "complete",
            "ocrReason": "pdf-all-pages",
            "ocrPreflight": True,
        })
        journal = INDEXER_MODULE.ResumeJournal(resume, signature)
        journal.record(completed)
        journal.close(completed=False)

        with mock.patch.object(INDEXER_MODULE.shutil, "which",
                               return_value="/usr/bin/mock-tool"), \
             mock.patch.object(INDEXER_MODULE, "extract_pdf_text_layer") as preflight, \
             mock.patch.object(INDEXER_MODULE, "ocr_pdf") as ocr:
            result = INDEXER_MODULE.run_pending_ocr(
                self.output, str(resume), 12_000, max_total, 19, "idle")
        self.assertEqual(result, 0)
        preflight.assert_not_called()
        ocr.assert_not_called()
        published = json.loads(self.output.read_text(encoding="utf-8"))
        item = next(value for value in published["items"]
                    if value["name"] == "resume-scan.pdf")
        self.assertEqual(item["content"], "checkpoint text")
        self.assertEqual(published["stats"]["ocrCompleted"], 1)
        self.assertEqual(published["stats"]["ocrCompletedThisRun"], 0)
        self.assertFalse(resume.exists())

    def test_pending_ocr_preflights_every_pdf_before_image_ocr(self):
        first = self.root / "a-scan.pdf"
        second = self.root / "b-scan.pdf"
        first.write_bytes(b"%PDF first image-only fixture")
        second.write_bytes(b"%PDF second image-only fixture")
        self.run_indexer("--fast-full", "--no-tags")
        resume = Path(self.temporary.name) / "ocr.resume.jsonl"
        events = []

        def preflight(path, _limit):
            events.append("preflight:" + path.name)
            return "", "pdf-no-text"

        def image_ocr(path, _limit, _pages, _progress):
            events.append("ocr:" + path.name)
            return "recognized"

        with mock.patch.object(INDEXER_MODULE.shutil, "which",
                               return_value="/usr/bin/mock-tool"), \
             mock.patch.object(INDEXER_MODULE, "extract_pdf_text_layer",
                               side_effect=preflight), \
             mock.patch.object(INDEXER_MODULE, "ocr_pdf",
                               side_effect=image_ocr):
            result = INDEXER_MODULE.run_pending_ocr(
                self.output, str(resume), 12_000, 256 * 1024 * 1024,
                19, "idle")
        self.assertEqual(result, 0)
        first_ocr = next(index for index, value in enumerate(events)
                         if value.startswith("ocr:"))
        self.assertEqual(events[:first_ocr], [
            "preflight:a-scan.pdf", "preflight:b-scan.pdf"])
        self.assertEqual(len(events), 4)

    def test_interrupted_full_rebuild_preserves_previous_snapshot(self):
        original = self.root / "original.txt"
        original.write_text("stable snapshot", encoding="utf-8")
        self.run_indexer("--no-tags")
        before = self.output.read_bytes()
        # Enough entries keep the rebuild alive until it can be interrupted.
        for index in range(2500):
            (self.root / ("cancel-%04d.txt" % index)).write_text(
                "cancel safety content " + ("x" * 2000), encoding="utf-8")
        resume = Path(self.temporary.name) / "resume.jsonl"
        process = subprocess.Popen(
            [sys.executable, str(INDEXER), "--roots-json",
             json.dumps([str(self.root)], ensure_ascii=False),
             "--output", str(self.output), "--full-rebuild", "--no-tags",
             "--resume-state", str(resume)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        deadline = time.time() + 5
        while time.time() < deadline and process.poll() is None:
            if resume.exists() and resume.stat().st_size > 2000:
                break
            time.sleep(0.01)
        process.terminate()
        process.communicate(timeout=10)
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(self.output.read_bytes(), before)
        self.assertTrue(resume.exists())

        resumed, completed = self.run_indexer(
            "--full-rebuild", "--no-tags", "--resume-state", str(resume))
        self.assertGreater(resumed["stats"]["reused"], 0)
        self.assertIn('"resuming"', completed.stdout)
        self.assertFalse(resume.exists())

    def test_hard_item_and_content_budgets(self):
        for index in range(1100):
            (self.root / ("item-%04d.txt" % index)).write_text(
                "bounded content " + ("x" * 3000), encoding="utf-8")
        payload, _ = self.run_indexer(
            "--max-items", "1000", "--max-content", "2000",
            "--max-total-content", "1000000")
        self.assertEqual(len(payload["items"]), 1000)
        self.assertEqual(payload["stats"]["truncated"], 1)
        self.assertLessEqual(payload["stats"]["contentChars"], 1_000_000)
        self.assertEqual(payload["capabilities"]["maxItems"], 1000)

        # Full rebuilds must ignore even a stale/non-zero caller limit.  The
        # indexer is the final policy boundary, not only the UI command line.
        unlimited, _ = self.run_indexer(
            "--full-rebuild", "--max-items", "1000", "--no-tags")
        self.assertEqual(len(unlimited["items"]), 1101)  # root + every file
        self.assertEqual(unlimited["stats"]["truncated"], 0)
        self.assertEqual(unlimited["capabilities"]["maxItems"], 0)

    def test_progress_protocol_reports_real_total(self):
        for index in range(30):
            (self.root / ("progress-%02d.txt" % index)).write_text(
                "progress payload", encoding="utf-8")
        payload, completed = self.run_indexer("--no-tags")
        messages = []
        for line in completed.stdout.splitlines():
            try:
                messages.append(json.loads(line))
            except ValueError:
                pass
        self.assertTrue(any(message.get("phase") == "counting"
                            for message in messages))
        progress = [message for message in messages
                    if int(message.get("progress", 0)) > 0]
        self.assertTrue(progress)
        self.assertEqual(progress[-1]["total"], len(payload["items"]))
        self.assertLessEqual(progress[-1]["progress"], progress[-1]["total"])

    def test_excluded_folders_and_index_format_allowlist(self):
        keep = self.root / "keep"
        excluded = self.root / "excluded"
        keep.mkdir()
        excluded.mkdir()
        (keep / "report.pdf").write_bytes(b"%PDF metadata fixture")
        (keep / "notes.txt").write_text("must not be indexed", encoding="utf-8")
        (keep / "table.et").write_text("must not be indexed", encoding="utf-8")
        (excluded / "secret.pdf").write_bytes(b"%PDF excluded fixture")
        payload, completed = self.run_indexer(
            "--exclude-roots-json", json.dumps([str(excluded)]),
            "--include-extensions-json", json.dumps(["pdf"]),
            "--no-tags")
        paths = {item["path"] for item in payload["items"]}
        self.assertIn(str(keep), paths)  # folders remain for drill-down
        self.assertIn(str(keep / "report.pdf"), paths)
        self.assertNotIn(str(keep / "notes.txt"), paths)
        self.assertNotIn(str(keep / "table.et"), paths)
        self.assertNotIn(str(excluded), paths)
        self.assertNotIn(str(excluded / "secret.pdf"), paths)
        self.assertEqual(payload["excludedRoots"], [str(excluded)])
        self.assertEqual(payload["includedExtensions"], ["pdf"])
        messages = [json.loads(line) for line in completed.stdout.splitlines()
                    if line.startswith("{")]
        total = next(message["total"] for message in messages
                     if "total" in message)
        self.assertEqual(total, len(payload["items"]))

    def test_malformed_sidecar_isolated(self):
        document = self.root / "safe.txt"
        document.write_text("safe content", encoding="utf-8")
        (self.root / ".ts").mkdir()
        (self.root / ".ts" / "safe.txt.json").write_text("{broken", encoding="utf-8")
        payload, _ = self.run_indexer()
        item = next(item for item in payload["items"] if item["name"] == "safe.txt")
        self.assertEqual(item["content"], "safe content")
        self.assertEqual(item["tags"], [])

    def test_command_provider_and_inherited_configuration(self):
        provider = Path(self.temporary.name) / "provider.py"
        provider.write_text(
            "import json,sys\n"
            "request=json.load(sys.stdin)\n"
            "json.dump({'items':[{'path':'virtual://report','name':'API report.pdf',"
            "'category':'pdf','tags':['API'],'content':'remote searchable text'}]},sys.stdout)\n",
            encoding="utf-8")
        inherited = Path(self.temporary.name) / "widget.json"
        inherited.write_text(json.dumps({"search": {
            "type": "command", "program": sys.executable,
            "arguments": [str(provider)], "limit": 10
        }}), encoding="utf-8")
        config = Path(self.temporary.name) / "providers.json"
        config.write_text(json.dumps({"providers": [{
            "name": "reused-widget", "inheritFrom": str(inherited),
            "inheritKey": "search"
        }]}), encoding="utf-8")

        payload, _ = self.run_indexer("--provider-config", str(config))
        item = next(item for item in payload["items"] if item["path"] == "virtual://report")
        self.assertEqual(item["source"], "provider:reused-widget")
        self.assertEqual(item["tags"], ["API"])
        self.assertIn("remote searchable", item["content"])

    def test_duplicate_provider_path_does_not_override_filesystem(self):
        local = self.root / "same.txt"
        local.write_text("local truth", encoding="utf-8")
        provider = Path(self.temporary.name) / "provider.py"
        provider.write_text(
            "import json,sys\njson.load(sys.stdin)\n"
            "json.dump([{'path':" + repr(str(local)) + ", 'name':'same.txt',"
            "'content':'wrong remote value'}],sys.stdout)\n", encoding="utf-8")
        config = Path(self.temporary.name) / "providers.json"
        config.write_text(json.dumps({"providers": [{
            "name": "duplicates", "type": "command",
            "program": sys.executable, "arguments": [str(provider)]
        }]}), encoding="utf-8")
        payload, _ = self.run_indexer("--provider-config", str(config))
        matches = [item for item in payload["items"] if item["path"] == str(local)]
        self.assertEqual(len(matches), 1)
        self.assertEqual(matches[0]["source"], "filesystem")
        self.assertEqual(matches[0]["content"], "local truth")

    def test_http_provider(self):
        received = {}

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(inner_self):
                length = int(inner_self.headers.get("Content-Length", "0"))
                received.update(json.loads(inner_self.rfile.read(length).decode("utf-8")))
                payload = {"items": [{
                    "path": "api://http-result", "name": "HTTP result",
                    "tags": ["Remote"], "content": "from local HTTP API"
                }]}
                body = json.dumps(payload).encode("utf-8")
                inner_self.send_response(200)
                inner_self.send_header("Content-Type", "application/json")
                inner_self.send_header("Content-Length", str(len(body)))
                inner_self.end_headers()
                inner_self.wfile.write(body)

            def log_message(self, *_args):
                pass

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            config = Path(self.temporary.name) / "http.json"
            config.write_text(json.dumps({"providers": [{
                "name": "http-test", "type": "http",
                "url": "http://127.0.0.1:%d/search" % server.server_port,
                "limit": 12
            }]}), encoding="utf-8")
            payload, _ = self.run_indexer("--provider-config", str(config))
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)
        item = next(item for item in payload["items"] if item["path"] == "api://http-result")
        self.assertEqual(item["source"], "provider:http-test")
        self.assertEqual(received["limit"], 12)
        self.assertEqual(received["roots"], [str(self.root)])

    def test_dbus_provider_protocol_with_gdbus_adapter(self):
        tools = Path(self.temporary.name) / "bin"
        tools.mkdir()
        gdbus = tools / "gdbus"
        response = json.dumps({"items": [{
            "path": "dbus://result", "name": "D-Bus result", "tags": ["Bus"]
        }]})
        gdbus.write_text(
            "#!/usr/bin/env python3\nimport sys\nprint((" + repr(response) + ",))\n",
            encoding="utf-8")
        gdbus.chmod(0o755)
        config = Path(self.temporary.name) / "dbus.json"
        config.write_text(json.dumps({"providers": [{
            "name": "dbus-test", "type": "dbus",
            "service": "org.example.Search", "objectPath": "/org/example/Search",
            "method": "org.example.Search.queryJson"
        }]}), encoding="utf-8")
        environment = dict(os.environ)
        environment["PATH"] = str(tools) + os.pathsep + environment.get("PATH", "")
        payload, _ = self.run_indexer("--provider-config", str(config), env=environment)
        item = next(item for item in payload["items"] if item["path"] == "dbus://result")
        self.assertEqual(item["source"], "provider:dbus-test")

    @unittest.skipUnless(shutil.which("tesseract"), "tesseract unavailable")
    def test_real_image_ocr(self):
        from PIL import Image, ImageDraw, ImageFont
        image = Image.new("RGB", (1500, 300), "white")
        draw = ImageDraw.Draw(image)
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 92)
        draw.text((40, 70), "SMART SPACE 7391", fill="black", font=font)
        image.save(self.root / "scan.png")
        payload, _ = self.run_indexer("--ocr-images")
        item = next(item for item in payload["items"] if item["name"] == "scan.png")
        normalized = item["content"].replace(" ", "").upper()
        self.assertIn("SMARTSPACE7391", normalized)
        self.assertEqual(item["extractor"], "tesseract")

    @unittest.skipUnless(shutil.which("tesseract") and shutil.which("pdftoppm"),
                         "PDF OCR dependencies unavailable")
    def test_real_scanned_pdf_ocr(self):
        from PIL import Image, ImageDraw, ImageFont
        from reportlab.lib.pagesizes import A4
        from reportlab.pdfgen import canvas
        image_path = Path(self.temporary.name) / "page.png"
        image = Image.new("RGB", (1600, 500), "white")
        draw = ImageDraw.Draw(image)
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 96)
        draw.text((60, 150), "SCANNED PDF 8246", fill="black", font=font)
        image.save(image_path)
        pdf_path = self.root / "scanned.pdf"
        output = canvas.Canvas(str(pdf_path), pagesize=A4)
        output.drawImage(str(image_path), 30, 430, width=535, height=167)
        output.showPage()
        output.save()
        payload, _ = self.run_indexer("--ocr-images")
        item = next(item for item in payload["items"] if item["name"] == "scanned.pdf")
        normalized = item["content"].replace(" ", "").upper()
        self.assertIn("SCANNEDPDF8246", normalized)
        self.assertEqual(item["extractor"], "pdf-tesseract")

    @unittest.skipUnless(shutil.which("tesseract") and shutil.which("pdftoppm")
                         and shutil.which("pdfinfo"),
                         "all-page PDF OCR dependencies unavailable")
    def test_idle_full_rebuild_ocrs_every_pdf_page_without_tags(self):
        from PIL import Image, ImageDraw, ImageFont
        from reportlab.lib.pagesizes import A4
        from reportlab.pdfgen import canvas

        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 92)
        page_images = []
        for page_number in range(1, 7):
            image_path = Path(self.temporary.name) / ("page-%d.png" % page_number)
            image = Image.new("RGB", (1600, 500), "white")
            draw = ImageDraw.Draw(image)
            label = ("LAST PAGE TOKEN 63827" if page_number == 6
                     else "INDEX PAGE %d" % page_number)
            draw.text((45, 150), label, fill="black", font=font)
            image.save(image_path)
            page_images.append(image_path)

        pdf_path = self.root / "six-pages.pdf"
        output = canvas.Canvas(str(pdf_path), pagesize=A4)
        for image_path in page_images:
            output.drawImage(str(image_path), 30, 430, width=535, height=167)
            output.showPage()
        output.save()
        (self.root / ".ts").mkdir()
        (self.root / ".ts" / "six-pages.pdf.json").write_text(
            json.dumps({"tags": [{"title": "must-not-load"}]}),
            encoding="utf-8")

        limited, _ = self.run_indexer(
            "--ocr-images", "--ocr-pdf-pages", "5", "--no-tags")
        limited_item = next(item for item in limited["items"]
                            if item["name"] == "six-pages.pdf")
        self.assertNotIn("LASTPAGETOKEN63827",
                         limited_item["content"].replace(" ", "").upper())

        complete, _ = self.run_indexer(
            "--full-rebuild", "--ocr-images", "--ocr-pdf-pages", "0",
            "--no-tags")
        complete_item = next(item for item in complete["items"]
                             if item["name"] == "six-pages.pdf")
        normalized = complete_item["content"].replace(" ", "").upper()
        self.assertIn("LASTPAGETOKEN63827", normalized)
        self.assertEqual(complete["stats"]["reused"], 0)
        self.assertTrue(complete["fullRebuild"])
        self.assertEqual(complete["ocrPdfPages"], 0)
        self.assertFalse(complete["tagsEnabled"])
        self.assertEqual(complete_item["tags"], [])
        self.assertGreaterEqual(complete["capabilities"]["processNice"], 19)


if __name__ == "__main__":
    unittest.main(verbosity=2)
