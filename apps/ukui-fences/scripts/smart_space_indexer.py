#!/usr/bin/env python3
"""Build the local Smart Space index used by ukui-fences.

The indexer is deliberately standalone: it understands TagSpaces sidecars,
extracts common document formats with system tools/the Python standard library,
and writes one atomic JSON snapshot for the Qt widget.
"""

import argparse
import ast
import datetime as dt
import gzip
import json
import os
import re
import shutil
import struct
import subprocess
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Optional, Sequence, Set, Tuple
from xml.etree import ElementTree


INDEX_VERSION = 3
DEFAULT_MAX_CONTENT = 12_000
DEFAULT_MAX_ITEMS = 25_000
DEFAULT_MAX_TOTAL_CONTENT = 16 * 1024 * 1024
MAX_EXTRACT_FILE_SIZE = 64 * 1024 * 1024
MAX_OCR_PDF_PAGES = 5
IGNORED_DIRS = {
    ".git", ".svn", ".hg", ".cache", ".local", ".config", ".Trash",
    ".ts", "node_modules", "build", "build-clean", "build-v10",
    "__pycache__",
}
TEXT_SUFFIXES = {
    ".txt", ".md", ".markdown", ".csv", ".tsv", ".json", ".xml",
    ".yaml", ".yml", ".ini", ".conf", ".log", ".html", ".htm",
    ".css", ".js", ".ts", ".cpp", ".c", ".h", ".hpp", ".py",
    ".sh", ".desktop", ".rtf",
}
IMAGE_SUFFIXES = {
    ".jpg", ".jpeg", ".png", ".webp", ".tif", ".tiff", ".bmp",
    ".gif",
}
DOCUMENT_SUFFIXES = {
    ".pdf", ".doc", ".docx", ".odt", ".wps",
}
PRESENTATION_SUFFIXES = {
    ".ppt", ".pptx", ".odp", ".dps",
}
SPREADSHEET_SUFFIXES = {
    ".xls", ".xlsx", ".ods", ".et",
}
OOXML_SUFFIXES = {".docx", ".pptx", ".xlsx", ".odt", ".odp", ".ods"}
LEGACY_OFFICE_SUFFIXES = {".doc", ".ppt", ".xls", ".wps", ".dps", ".et"}
KNOWN_INDEX_SUFFIXES = (TEXT_SUFFIXES | IMAGE_SUFFIXES | DOCUMENT_SUFFIXES |
                        PRESENTATION_SUFFIXES | SPREADSHEET_SUFFIXES)


def compact_text(value: str, limit: int) -> str:
    value = value.replace("\x00", " ")
    value = re.sub(r"[\t\r\f\v]+", " ", value)
    value = re.sub(r"\n{3,}", "\n\n", value)
    value = re.sub(r" {2,}", " ", value).strip()
    return value[:limit]


def read_json(path: Path) -> Dict:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError, UnicodeError):
        return {}


def tag_titles(value: object) -> List[str]:
    result: List[str] = []
    if not isinstance(value, list):
        return result
    for tag in value:
        title = tag.get("title") if isinstance(tag, dict) else tag
        if isinstance(title, str):
            title = title.strip()
            if title and title not in result:
                result.append(title)
    return result


def filename_tags(path: Path) -> List[str]:
    stem = path.stem
    matches = re.findall(r"\[([^\[\]]+)\]", stem)
    result: List[str] = []
    for group in matches:
        for title in re.split(r"[\s,，;；]+", group):
            title = title.strip()
            if title and title not in result:
                result.append(title)
    return result


def sidecar_tags(path: Path, is_dir: bool) -> List[str]:
    meta_path = path / ".ts" / "tsm.json" if is_dir else path.parent / ".ts" / (path.name + ".json")
    return tag_titles(read_json(meta_path).get("tags"))


def merged_tags(path: Path, is_dir: bool) -> List[str]:
    result = sidecar_tags(path, is_dir)
    if not is_dir:
        for title in filename_tags(path):
            if title not in result:
                result.append(title)
    return result


def category_for(path: Path, is_dir: bool = False) -> str:
    if is_dir:
        return "folder"
    suffix = path.suffix.lower()
    if suffix in IMAGE_SUFFIXES:
        return "image"
    if suffix == ".pdf":
        return "pdf"
    if suffix in PRESENTATION_SUFFIXES:
        return "presentation"
    if suffix in SPREADSHEET_SUFFIXES:
        return "spreadsheet"
    if suffix in DOCUMENT_SUFFIXES or suffix in TEXT_SUFFIXES:
        return "document"
    return "other"


def run_text_command(arguments: Sequence[str], timeout: int) -> str:
    try:
        completed = subprocess.run(
            list(arguments), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            timeout=timeout, check=False,
        )
        return completed.stdout.decode("utf-8", errors="ignore")
    except (OSError, subprocess.SubprocessError):
        return ""


def metadata_path(path: Path, is_dir: bool) -> Path:
    return path / ".ts" / "tsm.json" if is_dir else path.parent / ".ts" / (path.name + ".json")


def stat_ns(path: Path) -> int:
    try:
        return int(path.stat().st_mtime_ns)
    except (AttributeError, OSError):
        try:
            return int(path.stat().st_mtime * 1_000_000_000)
        except OSError:
            return 0


def extract_plain_text(path: Path, limit: int) -> str:
    try:
        raw = path.read_bytes()[: max(limit * 4, 262_144)]
    except OSError:
        return ""
    for encoding in ("utf-8", "gb18030", "utf-16"):
        try:
            return compact_text(raw.decode(encoding), limit)
        except UnicodeError:
            continue
    return compact_text(raw.decode("utf-8", errors="ignore"), limit)


def iter_zip_xml_text(path: Path) -> Iterable[str]:
    with zipfile.ZipFile(str(path)) as archive:
        candidates = []
        for name in archive.namelist():
            lower = name.lower()
            if not lower.endswith(".xml"):
                continue
            if lower.startswith((
                "word/", "ppt/slides/", "ppt/notesSlides/", "xl/worksheets/",
                "xl/sharedstrings.xml", "content.xml",
            )):
                candidates.append(name)
        for name in sorted(candidates):
            try:
                data = archive.read(name)
                root = ElementTree.fromstring(data)
            except (KeyError, OSError, ValueError, ElementTree.ParseError):
                continue
            pieces = []
            for element in root.iter():
                if element.text and element.text.strip():
                    pieces.append(element.text.strip())
            if pieces:
                yield " ".join(pieces)


def extract_zip_document(path: Path, limit: int) -> str:
    pieces: List[str] = []
    length = 0
    try:
        for value in iter_zip_xml_text(path):
            pieces.append(value)
            length += len(value)
            if length >= limit:
                break
    except (OSError, zipfile.BadZipFile, RuntimeError):
        return ""
    return compact_text("\n".join(pieces), limit)


def extract_legacy_office(path: Path, limit: int) -> str:
    # Old Office and native WPS files are compound/binary containers.  The
    # strings fallback is intentionally conservative and is replaced by a
    # platform provider when UKUI/WPS exposes richer extraction facilities.
    outputs = []
    strings = shutil.which("strings")
    if not strings:
        return ""
    for encoding_args in (["-el"], []):
        value = run_text_command([strings, *encoding_args, "-n", "4", str(path)], 12)
        if value:
            outputs.append(value)
    lines: List[str] = []
    for line in "\n".join(outputs).splitlines():
        line = line.strip()
        if len(line) < 3 or len(line) > 500:
            continue
        if re.search(r"[\u4e00-\u9fffA-Za-z]{2}", line):
            lines.append(line)
    return compact_text("\n".join(lines), limit)


def extract_xlrd_spreadsheet(path: Path, limit: int) -> str:
    """Read classic XLS/WPS ET workbooks without launching an office suite.

    WPS ET commonly stores workbooks in the same BIFF compound-document form
    as classic XLS.  xlrd opens that format read-only and on demand, which is
    substantially lighter than starting the WPS GUI and does not take an
    application-level lock on the user's workbook.  It remains optional so a
    minimal installation still falls back to bounded binary string recovery.
    """
    try:
        import xlrd  # type: ignore
    except (ImportError, OSError):
        return ""

    workbook = None
    try:
        workbook = xlrd.open_workbook(str(path), on_demand=True)
        pieces: List[str] = []
        kept_chars = 0
        visited_cells = 0
        max_cells = 250_000

        for sheet_name in workbook.sheet_names():
            sheet = workbook.sheet_by_name(sheet_name)
            heading = "[%s]" % sheet_name.strip()
            if heading != "[]":
                pieces.append(heading)
                kept_chars += len(heading) + 1

            for row_index in range(sheet.nrows):
                values: List[str] = []
                for cell in sheet.row(row_index):
                    visited_cells += 1
                    if visited_cells > max_cells:
                        break
                    cell_type = getattr(cell, "ctype", None)
                    if cell_type in {
                            getattr(xlrd, "XL_CELL_EMPTY", -100),
                            getattr(xlrd, "XL_CELL_BLANK", -101),
                            getattr(xlrd, "XL_CELL_ERROR", -102)}:
                        continue
                    value = getattr(cell, "value", "")
                    if cell_type == getattr(xlrd, "XL_CELL_DATE", -103):
                        try:
                            value = xlrd.xldate_as_datetime(
                                value, workbook.datemode).isoformat(sep=" ")
                        except (TypeError, ValueError, OverflowError):
                            pass
                    if isinstance(value, float) and value.is_integer():
                        value = int(value)
                    value = str(value).strip()
                    if value:
                        values.append(value)
                if values:
                    line = "\t".join(values)
                    pieces.append(line)
                    kept_chars += len(line) + 1
                if kept_chars >= limit or visited_cells > max_cells:
                    break
            if kept_chars >= limit or visited_cells > max_cells:
                break
        return compact_text("\n".join(pieces), limit)
    except Exception:
        # Optional third-party parsers expose several format-specific error
        # classes.  A malformed workbook must never abort the whole index run.
        return ""
    finally:
        if workbook is not None:
            try:
                workbook.release_resources()
            except Exception:
                pass


def ocr_image(path: Path, limit: int, timeout: int = 45) -> str:
    tesseract = shutil.which("tesseract")
    if not tesseract:
        return ""
    value = run_text_command(
        [tesseract, str(path), "stdout", "-l", "chi_sim+eng"], timeout,
    )
    return compact_text(value, limit)


def ocr_pdf(path: Path, limit: int, max_pages: int,
            progress_callback: Optional[Callable[[int, int], None]] = None) -> str:
    pdftoppm = shutil.which("pdftoppm")
    if not pdftoppm or not shutil.which("tesseract"):
        return ""
    pieces: List[str] = []
    with tempfile.TemporaryDirectory(prefix="ukui-fences-pdf-") as temporary:
        page_count = max_pages
        if max_pages == 0:
            pdfinfo = shutil.which("pdfinfo")
            if not pdfinfo:
                return ""
            info = run_text_command([pdfinfo, str(path)], 30)
            match = re.search(r"^Pages:\s*(\d+)\s*$", info,
                              flags=re.IGNORECASE | re.MULTILINE)
            if not match:
                return ""
            page_count = int(match.group(1))
        total = 0
        full_page_quota = max(1, limit // max(1, page_count))
        # Render only one page at a time.  A long PDF can otherwise create
        # gigabytes of PNG files before OCR starts, which is inappropriate for
        # a low-priority background task.
        for page_number in range(1, page_count + 1):
            prefix = Path(temporary) / ("page-%06d" % page_number)
            image = Path(str(prefix) + ".png")
            try:
                subprocess.run(
                    [pdftoppm, "-f", str(page_number), "-l", str(page_number),
                     "-singlefile", "-r", "150", "-png", str(path), str(prefix)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    timeout=180, check=False,
                )
            except (OSError, subprocess.SubprocessError):
                continue
            if not image.exists():
                # Fixed-page incremental mode may ask for more pages than the
                # document contains.  Stop at the first absent rendered page.
                if max_pages > 0:
                    break
                continue
            remaining = max(0, limit - total)
            # Full-idle mode intentionally OCRs every rendered page.  Once the
            # snapshot budget is full, later text is discarded rather than
            # growing the in-memory JSON without a bound.
            page_limit = (max(2_000, full_page_quota)
                          if max_pages == 0 else remaining)
            value = ocr_image(image, page_limit, timeout=90 if max_pages == 0 else 50)
            try:
                image.unlink()
            except OSError:
                pass
            if value:
                kept_limit = (min(remaining, full_page_quota)
                              if max_pages == 0 else remaining)
                kept = value[:kept_limit]
                if kept:
                    pieces.append(kept)
                    total += len(kept)
            if total >= limit and max_pages != 0:
                break
            if progress_callback:
                progress_callback(page_number, page_count)
    return compact_text("\n".join(pieces), limit)


def extract_content(path: Path, ocr_images: bool, limit: int,
                    ocr_pdf_pages: int) -> Tuple[str, str]:
    suffix = path.suffix.lower()
    if suffix in TEXT_SUFFIXES:
        return extract_plain_text(path, limit), "text"
    if suffix == ".pdf":
        pdftotext = shutil.which("pdftotext")
        text_value = ""
        if pdftotext:
            text_value = run_text_command([pdftotext, "-q", str(path), "-"], 35)
            # Normal incremental indexing prefers the cheap text layer.  An
            # explicit all-page run still OCRs every page, including hybrid
            # PDFs whose image-only pages would otherwise be missed.
            if text_value.strip() and ocr_pdf_pages != 0:
                return compact_text(text_value, limit), "pdftotext"
        if ocr_images:
            # In all-page hybrid PDFs reserve half the bounded snapshot for OCR
            # so image-only content is searchable even when the text layer is
            # already longer than the per-file cap.
            text_snapshot = text_value
            if text_value and ocr_pdf_pages == 0:
                text_snapshot = compact_text(text_value, limit // 2)
            ocr_limit = max(2_000, limit - len(text_snapshot))
            ocr_value = ocr_pdf(path, ocr_limit, ocr_pdf_pages)
            combined = compact_text("\n".join(
                value for value in (text_snapshot, ocr_value) if value.strip()), limit)
            if combined:
                extractor = ("pdftotext+pdf-tesseract" if text_value.strip()
                             and ocr_value.strip() else
                             "pdf-tesseract" if ocr_value.strip() else "pdftotext")
                return combined, extractor
        if text_value.strip():
            return compact_text(text_value, limit), "pdftotext"
        return "", "pdf-no-text"
    if suffix in OOXML_SUFFIXES:
        return extract_zip_document(path, limit), "ooxml"
    if suffix in LEGACY_OFFICE_SUFFIXES:
        if zipfile.is_zipfile(str(path)):
            return extract_zip_document(path, limit), "wps-zip"
        if suffix in {".xls", ".et"}:
            spreadsheet = extract_xlrd_spreadsheet(path, limit)
            if spreadsheet:
                return spreadsheet, "xlrd-wps-et" if suffix == ".et" else "xlrd-xls"
        legacy = extract_legacy_office(path, limit)
        extractor = ("wps-binary-text" if suffix in {".wps", ".dps", ".et"}
                     else "binary-strings")
        return legacy, extractor
    if suffix in IMAGE_SUFFIXES and ocr_images:
        value = ocr_image(path, limit)
        return value, "tesseract" if value else "tesseract-empty"
    return "", "metadata"


def ocr_state(path: Path, is_dir: bool, content: str, extractor: str,
              ocr_pdf_pages: int) -> Tuple[str, str]:
    """Return the durable OCR queue state for a filesystem item.

    Fast indexing deliberately never starts Tesseract.  Images and PDFs with
    no usable text layer are therefore marked for the separate, resumable OCR
    pass instead of making an ordinary full/incremental refresh unexpectedly
    expensive.
    """
    if is_dir:
        return "not-needed", "folder"
    suffix = path.suffix.lower()
    if suffix in IMAGE_SUFFIXES:
        if extractor == "tesseract":
            return "complete", "image-ocr"
        if extractor == "tesseract-empty":
            return "complete", "image-no-text"
        return "pending", "image"
    if suffix != ".pdf":
        return "not-needed", "format"
    if "pdf-tesseract" in extractor and ocr_pdf_pages == 0:
        return "complete", "pdf-all-pages"
    if extractor.startswith("pdftotext") and content.strip():
        return "not-needed", "pdf-text-layer"
    # A legacy five-page OCR snapshot is useful as searchable text, but is
    # still pending because the dedicated OCR task promises the entire PDF.
    return "pending", "pdf-no-text-layer"


def iso_time(timestamp: float) -> str:
    try:
        return dt.datetime.fromtimestamp(timestamp, dt.timezone.utc).isoformat()
    except (OSError, OverflowError, ValueError):
        return ""


def entry_for(path: Path, root: Path, is_dir: bool, ocr_images: bool,
              max_content: int, previous: Dict, stats: Dict[str, int],
              content_budget: List[int], ocr_pdf_pages: int,
              read_tags: bool) -> Dict:
    try:
        stat = path.stat()
    except OSError:
        stat = None
    meta_mtime_ns = stat_ns(metadata_path(path, is_dir)) if read_tags else 0
    mtime_ns = stat_ns(path)
    content, extractor = ("", "metadata")
    reusable = (
        isinstance(previous, dict)
        and previous.get("mtimeNs") == mtime_ns
        and (not read_tags or previous.get("metaMtimeNs", 0) == meta_mtime_ns)
        and previous.get("size", 0) == (0 if is_dir or not stat else int(stat.st_size))
        # A fast non-OCR pass may safely retain a richer OCR result produced
        # earlier.  An OCR pass still requires an exact extraction policy.
        and ((not ocr_images) or (
            bool(previous.get("ocrEnabled", False)) == bool(ocr_images)
            and previous.get("ocrPdfPages", MAX_OCR_PDF_PAGES) == ocr_pdf_pages))
    )
    if reusable:
        content = previous.get("content", "") if isinstance(previous.get("content", ""), str) else ""
        # Preserve the complete text/OCR of unchanged files.  The budget only
        # limits newly extracted content; it must not erase published results.
        extractor = previous.get("extractor", "metadata")
        stats["reused"] += 1
    full_pdf_ocr = (not is_dir and path.suffix.lower() == ".pdf"
                    and ocr_images and ocr_pdf_pages == 0)
    should_extract = (content_budget[0] > 0 or full_pdf_ocr)
    if (not reusable and not is_dir and stat
            and stat.st_size <= MAX_EXTRACT_FILE_SIZE and should_extract):
        extraction_limit = min(max_content, content_budget[0])
        if full_pdf_ocr:
            # Even after the global text snapshot is full, an explicit idle
            # run still walks and OCRs every page of every PDF.  Later text is
            # discarded to keep the JSON/RSS budget bounded.
            extraction_limit = max(2_000, extraction_limit)
        content, extractor = extract_content(
            path, ocr_images, extraction_limit,
            ocr_pdf_pages)
        content = content[:max(0, content_budget[0])]
        stats["extracted"] += 1
    if content:
        content_budget[0] = max(0, content_budget[0] - len(content))
        stats["contentChars"] += len(content)
    try:
        relative = str(path.relative_to(root))
    except ValueError:
        relative = path.name
    previous_ocr_state = (str(previous.get("ocrStatus", ""))
                          if reusable else "")
    if previous_ocr_state in {"complete", "not-needed", "unavailable"}:
        # An unchanged file's durable OCR result is authoritative.  Recomputing
        # it from compacted metadata can turn an all-page PDF result back into
        # "pending" (older snapshots legitimately omit ocrPdfPages).  That
        # causes needless repeat OCR even though its text is still present.
        current_ocr_state = previous_ocr_state
        current_ocr_reason = str(previous.get("ocrReason", ""))
    else:
        current_ocr_state, current_ocr_reason = ocr_state(
            path, is_dir, content, extractor,
            (previous.get("ocrPdfPages", ocr_pdf_pages)
             if reusable else ocr_pdf_pages))
    return {
        "path": str(path),
        "root": str(root),
        "relativePath": relative,
        "parentPath": str(path.parent),
        "name": path.name,
        "isDir": is_dir,
        "suffix": "" if is_dir else path.suffix.lower().lstrip("."),
        "category": category_for(path, is_dir),
        "tags": merged_tags(path, is_dir) if read_tags else [],
        "modified": iso_time(stat.st_mtime) if stat else "",
        "size": 0 if is_dir or not stat else int(stat.st_size),
        "content": content,
        "extractor": extractor,
        "mtimeNs": mtime_ns,
        "metaMtimeNs": meta_mtime_ns,
        "ocrEnabled": bool(ocr_images),
        "ocrPdfPages": ocr_pdf_pages,
        "ocrStatus": current_ocr_state,
        "ocrReason": current_ocr_reason,
        "tagsEnabled": read_tags,
        "source": "filesystem",
    }


def scan_root(root: Path, ocr_images: bool, max_content: int,
              errors: List[Dict], progress: List[int], previous: Dict[str, Dict],
              stats: Dict[str, int], state: Dict[str, int], max_items: int,
              content_budget: List[int], ocr_pdf_pages: int,
              read_tags: bool, total_items: int,
              journal: "ResumeJournal" = None,
              excluded: List[str] = None,
              included_extensions: List[str] = None) -> List[Dict]:
    excluded = excluded or []
    included_extensions = included_extensions or []
    if path_is_excluded(root, excluded):
        return []
    if max_items > 0 and state["items"] >= max_items:
        return []
    root_item = entry_for(root, root, True, ocr_images, max_content,
                          previous.get(str(root), {}), stats,
                          content_budget, ocr_pdf_pages, read_tags)
    items: List[Dict] = [root_item]
    if journal:
        journal.record(root_item)
    state["items"] += 1
    progress[0] += 1
    for current, directories, files in os.walk(str(root), followlinks=False):
        if max_items > 0 and state["items"] >= max_items:
            directories[:] = []
            state["truncated"] = 1
            break
        current_path = Path(current)
        directories[:] = sorted(
            name for name in directories
            if name not in IGNORED_DIRS and not name.startswith(".")
            and not path_is_excluded(current_path / name, excluded)
        )
        for name in directories:
            if max_items > 0 and state["items"] >= max_items:
                state["truncated"] = 1
                break
            path = current_path / name
            try:
                item = entry_for(path, root, True, ocr_images, max_content,
                                 previous.get(str(path), {}), stats,
                                 content_budget, ocr_pdf_pages, read_tags)
                items.append(item)
                if journal:
                    journal.record(item)
                state["items"] += 1
                progress[0] += 1
            except Exception as error:  # isolate unreadable or malformed entries
                errors.append({"path": str(path), "error": str(error)[:300]})
        for name in sorted(files):
            if max_items > 0 and state["items"] >= max_items:
                state["truncated"] = 1
                directories[:] = []
                break
            if name.startswith("."):
                continue
            path = current_path / name
            if (path_is_excluded(path, excluded)
                    or not extension_is_included(path, included_extensions)):
                continue
            try:
                item = entry_for(path, root, False, ocr_images, max_content,
                                 previous.get(str(path), {}), stats,
                                 content_budget, ocr_pdf_pages, read_tags)
                items.append(item)
                if journal:
                    journal.record(item)
                state["items"] += 1
                progress[0] += 1
            except Exception as error:
                errors.append({"path": str(path), "error": str(error)[:300]})
            if progress[0] % 25 == 0:
                print(json.dumps({"progress": progress[0], "total": total_items,
                                  "path": str(path)}, ensure_ascii=False), flush=True)
    return items


def compact_item_for_storage(item: Dict) -> Dict:
    """Remove filesystem fields that can be derived when the index is loaded.

    A 90k-file snapshot otherwise repeats path components and default values
    often enough to approach Qt 5's JSON document-size ceiling even though the
    actual searchable content is bounded.  Provider records keep their schema;
    local records use backwards-compatible omissions with loader fallbacks.
    """
    if item.get("source", "filesystem") != "filesystem":
        return item
    compact = dict(item)
    for key in ("relativePath", "parentPath", "suffix", "tagsEnabled"):
        compact.pop(key, None)
    if not compact.get("metaMtimeNs"):
        compact.pop("metaMtimeNs", None)
    if not compact.get("ocrEnabled"):
        compact.pop("ocrEnabled", None)
    if compact.get("ocrPdfPages", MAX_OCR_PDF_PAGES) == MAX_OCR_PDF_PAGES:
        compact.pop("ocrPdfPages", None)
    if not compact.get("isDir"):
        compact.pop("isDir", None)
    if not compact.get("size"):
        compact.pop("size", None)
    if not compact.get("content"):
        compact.pop("content", None)
    if compact.get("extractor") == "metadata":
        compact.pop("extractor", None)
    if compact.get("ocrStatus") == "not-needed":
        compact.pop("ocrReason", None)
    return compact


def compact_payload_for_storage(payload: Dict) -> Dict:
    stored = dict(payload)
    items = payload.get("items", [])
    if isinstance(items, list):
        stored["items"] = [compact_item_for_storage(item)
                           if isinstance(item, dict) else item
                           for item in items]
    return stored


def atomic_json_write(output: Path, payload: Dict) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=output.name + ".", dir=str(output.parent))
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            json.dump(compact_payload_for_storage(payload), handle,
                      ensure_ascii=False, separators=(",", ":"))
            handle.flush()
            os.fsync(handle.fileno())
        if output.is_file():
            # Retain one exact generation before replacing the canonical
            # snapshot. The hard link is atomic and avoids a 100+ MiB copy.
            backup = Path(str(output) + ".previous")
            backup_temporary = Path(str(backup) + ".new")
            try:
                backup_temporary.unlink()
            except OSError:
                pass
            try:
                os.link(str(output), str(backup_temporary))
                os.replace(str(backup_temporary), str(backup))
            finally:
                try:
                    backup_temporary.unlink()
                except OSError:
                    pass
        os.replace(temporary, str(output))
        atomic_ui_stream_write(output, payload)
    finally:
        try:
            os.unlink(temporary)
        except OSError:
            pass


def atomic_ui_stream_write(output: Path, payload: Dict) -> Path:
    """Write a compressed length-prefixed view for Qt's streaming loader.

    Qt5 cannot represent a single QJsonDocument once its internal data crosses
    roughly 128 MiB.  A small binary record per item avoids that limit and the
    cost of parsing tens of thousands of individual JSON documents, while the
    canonical JSON snapshot remains available to Python and integrations.
    """
    stream_output = Path(str(output) + ".ui.bin.gz")
    stream_output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=stream_output.name + ".", dir=str(stream_output.parent))
    try:
        with os.fdopen(descriptor, "wb") as raw:
            with gzip.GzipFile(fileobj=raw, mode="wb", compresslevel=5,
                               mtime=0) as handle:
                metadata = dict(payload)
                items = metadata.pop("items", [])
                metadata["uiStreamVersion"] = 1
                metadata["uiStreamItems"] = (
                    len(items) if isinstance(items, list) else 0)
                encoded_meta = json.dumps(
                    metadata, ensure_ascii=False,
                    separators=(",", ":")).encode("utf-8")
                handle.write(b"UKFIDX1\n")
                handle.write(struct.pack("<II", 1, len(encoded_meta)))
                handle.write(encoded_meta)
                item_count = len(items) if isinstance(items, list) else 0
                handle.write(struct.pack("<I", item_count))
                if isinstance(items, list):
                    for item in items:
                        stored = (compact_item_for_storage(item)
                                  if isinstance(item, dict) else item)
                        if not isinstance(stored, dict):
                            stored = {}
                        flags = 1 if stored.get("isDir") else 0
                        size = int(stored.get("size", 0) or 0)
                        modified_ms = -1
                        modified = str(stored.get("modified", ""))
                        try:
                            parsed = dt.datetime.fromisoformat(
                                modified.replace("Z", "+00:00"))
                            modified_ms = int(parsed.timestamp() * 1000)
                        except (TypeError, ValueError, OverflowError):
                            mtime_ns = int(stored.get("mtimeNs", 0) or 0)
                            if mtime_ns:
                                modified_ms = mtime_ns // 1_000_000
                        handle.write(struct.pack(
                            "<Bqq", flags, size, modified_ms))
                        for key in ("path", "root", "name", "category",
                                    "content", "extractor", "ocrStatus",
                                    "ocrReason"):
                            encoded = str(stored.get(key, "") or "").encode("utf-8")
                            handle.write(struct.pack("<I", len(encoded)))
                            handle.write(encoded)
            raw.flush()
            os.fsync(raw.fileno())
        os.replace(temporary, str(stream_output))
        return stream_output
    finally:
        try:
            os.unlink(temporary)
        except OSError:
            pass


def parse_roots(value: str) -> List[Path]:
    try:
        raw = json.loads(value)
    except ValueError:
        raw = []
    result: List[Path] = []
    seen: Set[str] = set()
    for item in raw if isinstance(raw, list) else []:
        if not isinstance(item, str):
            continue
        path = Path(item).expanduser().resolve()
        key = str(path)
        if path.is_dir() and key not in seen:
            result.append(path)
            seen.add(key)
    return result


def parse_path_list(value: str) -> List[str]:
    """Parse normalized paths without requiring that they still exist."""
    try:
        raw = json.loads(value)
    except ValueError:
        raw = []
    result: List[str] = []
    for item in raw if isinstance(raw, list) else []:
        if not isinstance(item, str) or not item.strip():
            continue
        path = os.path.abspath(os.path.expanduser(item))
        if path not in result:
            result.append(path)
    return result


def parse_extensions(value: str) -> List[str]:
    try:
        raw = json.loads(value)
    except ValueError:
        raw = []
    result: List[str] = []
    for item in raw if isinstance(raw, list) else []:
        if not isinstance(item, str):
            continue
        extension = item.strip().lower().lstrip(".")
        if extension and extension not in result:
            result.append(extension)
    return result


def path_is_excluded(path: Path, excluded: List[str]) -> bool:
    candidate = os.path.abspath(str(path))
    return any(candidate == parent or candidate.startswith(parent + os.sep)
               for parent in excluded)


def extension_is_included(path: Path, included: List[str]) -> bool:
    if not included or "*" in included:
        return True
    suffix = path.suffix.lower()
    extension = suffix.lstrip(".")
    return (extension in included or
            ("__other__" in included and suffix not in KNOWN_INDEX_SUFFIXES))


def count_candidates(roots: List[Path], max_items: int,
                     excluded: List[str] = None,
                     included_extensions: List[str] = None) -> int:
    """Count visible filesystem entries without opening file contents.

    This inexpensive metadata pass gives the UI a real denominator.  It uses
    the same hidden/ignored-directory policy as scan_root, so the percentage
    remains meaningful even for a large idle/full run.
    """
    count = 0
    excluded = excluded or []
    included_extensions = included_extensions or []
    for root in roots:
        if path_is_excluded(root, excluded):
            continue
        if max_items > 0 and count >= max_items:
            break
        count += 1  # configured root itself
        for current, directories, files in os.walk(str(root), followlinks=False):
            current_path = Path(current)
            directories[:] = sorted(
                name for name in directories
                if name not in IGNORED_DIRS and not name.startswith(".")
                and not path_is_excluded(current_path / name, excluded)
            )
            count += len(directories)
            count += sum(1 for name in files if not name.startswith(".")
                         and not path_is_excluded(current_path / name, excluded)
                         and extension_is_included(current_path / name,
                                                   included_extensions))
            if max_items > 0 and count >= max_items:
                return max_items
    return count


class ResumeJournal:
    """Append-only full-index checkpoint.

    The completed JSON index is still written atomically and is never replaced
    by partial work.  This journal merely caches completed extraction/OCR
    records so a later run can reuse them after a user interruption.
    """

    def __init__(self, path: Path, signature: Dict):
        self.path = path
        self.signature = signature
        self.entries: Dict[str, Dict] = {}
        self.handle = None
        valid = False
        try:
            with path.open("r", encoding="utf-8") as source:
                header = json.loads(source.readline())
                valid = (isinstance(header, dict)
                         and header.get("kind") == "header"
                         and header.get("signature") == signature)
                if valid:
                    for line in source:
                        try:
                            record = json.loads(line)
                        except (ValueError, UnicodeError):
                            continue
                        item = record.get("item") if isinstance(record, dict) else None
                        if (isinstance(record, dict)
                                and record.get("kind") == "entry"
                                and isinstance(item, dict)
                                and isinstance(item.get("path"), str)):
                            self.entries[item["path"]] = item
        except (OSError, ValueError, UnicodeError):
            valid = False
        path.parent.mkdir(parents=True, exist_ok=True)
        mode = "a" if valid else "w"
        self.handle = path.open(mode, encoding="utf-8", buffering=1)
        try:
            os.chmod(str(path), 0o600)
        except OSError:
            pass
        if not valid:
            self.entries.clear()
            self.handle.write(json.dumps(
                {"kind": "header", "signature": signature},
                ensure_ascii=False, separators=(",", ":")) + "\n")
            self.handle.flush()

    def record(self, item: Dict) -> None:
        path = str(item.get("path", ""))
        if not path:
            return
        # Drop consumed checkpoint entries as the deterministic scan advances.
        # This avoids holding both a full previous dictionary and a full new
        # item list in RAM during a large resumed run.
        previous = self.entries.pop(path, None)
        if previous == item:
            return
        self.handle.write(json.dumps(
            {"kind": "entry", "item": item}, ensure_ascii=False,
            separators=(",", ":")) + "\n")
        # Line buffering makes every completed document immediately resumable
        # without the cost of fsync for every item.
        self.handle.flush()

    def close(self, completed: bool = False) -> None:
        if self.handle:
            self.handle.flush()
            self.handle.close()
            self.handle = None
        if completed:
            try:
                self.path.unlink()
            except OSError:
                pass


def dotted_value(value: object, key: str) -> object:
    current = value
    for part in key.split(".") if key else []:
        if not isinstance(current, dict):
            return None
        current = current.get(part)
    return current


def inherited_provider(provider: Dict, base_dir: Path) -> Dict:
    path_value = provider.get("inheritFrom")
    if not isinstance(path_value, str) or not path_value.strip():
        return dict(provider)
    path = Path(os.path.expandvars(path_value)).expanduser()
    if not path.is_absolute():
        path = base_dir / path
    inherited = read_json(path)
    selected = dotted_value(inherited, str(provider.get("inheritKey", "")))
    merged = dict(selected) if isinstance(selected, dict) else {}
    merged.update({key: value for key, value in provider.items()
                   if key not in {"inheritFrom", "inheritKey"}})
    return merged


def provider_request(provider: Dict, roots: List[Path]) -> Dict:
    return {
        "query": provider.get("query", ""),
        "roots": [str(root) for root in roots],
        "limit": int(provider.get("limit", 5000)),
        "options": provider.get("options", {}),
    }


def run_provider(provider: Dict, roots: List[Path]) -> object:
    kind = str(provider.get("type", "command"))
    request = provider_request(provider, roots)
    timeout = max(1, min(int(provider.get("timeout", 30)), 300))
    if kind == "command":
        program = os.path.expandvars(str(provider.get("program", "")))
        arguments = provider.get("arguments", [])
        if not program or not isinstance(arguments, list):
            raise ValueError("command provider requires program and arguments")
        completed = subprocess.run(
            [program, *[os.path.expandvars(str(value)) for value in arguments]],
            input=json.dumps(request, ensure_ascii=False).encode("utf-8"),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout, check=False,
        )
        if completed.returncode != 0:
            raise RuntimeError(completed.stderr.decode("utf-8", errors="ignore")[:300]
                               or "provider command failed")
        return json.loads(completed.stdout.decode("utf-8"))
    if kind == "http":
        url = str(provider.get("url", ""))
        if not url.startswith(("http://", "https://")):
            raise ValueError("http provider requires an http(s) url")
        headers = {str(key): str(value) for key, value in
                   (provider.get("headers", {}) if isinstance(provider.get("headers"), dict) else {}).items()}
        token_env = str(provider.get("tokenEnv", ""))
        if token_env and os.environ.get(token_env):
            headers.setdefault("Authorization", "Bearer " + os.environ[token_env])
        headers.setdefault("Content-Type", "application/json; charset=utf-8")
        http_request = urllib.request.Request(
            url, data=json.dumps(request, ensure_ascii=False).encode("utf-8"),
            headers=headers, method="POST",
        )
        with urllib.request.urlopen(http_request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    if kind == "dbus":
        gdbus = shutil.which("gdbus")
        if not gdbus:
            raise RuntimeError("gdbus is unavailable")
        service = str(provider.get("service", ""))
        object_path = str(provider.get("objectPath", ""))
        method = str(provider.get("method", ""))
        if not service or not object_path or "." not in method:
            raise ValueError("dbus provider requires service, objectPath and method")
        completed = subprocess.run(
            [gdbus, "call", "--session", "--dest", service,
             "--object-path", object_path, "--method", method,
             json.dumps(request, ensure_ascii=False)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout, check=False,
        )
        if completed.returncode != 0:
            raise RuntimeError(completed.stderr.decode("utf-8", errors="ignore")[:300])
        tuple_value = ast.literal_eval(completed.stdout.decode("utf-8").strip())
        value = tuple_value[0] if isinstance(tuple_value, tuple) and tuple_value else tuple_value
        return json.loads(value) if isinstance(value, str) else value
    raise ValueError("unknown provider type: " + kind)


def normalize_provider_item(raw: object, provider_name: str) -> Dict:
    if not isinstance(raw, dict):
        raise ValueError("provider item is not an object")
    path = str(raw.get("path", "")).strip()
    if not path:
        raise ValueError("provider item has no path")
    name = str(raw.get("name", "")).strip() or Path(path).name or path
    tags = tag_titles(raw.get("tags", []))
    # Provider APIs often return tags as strings; tag_titles already accepts both.
    is_dir = bool(raw.get("isDir", False))
    suffix = str(raw.get("suffix", "")).lower().lstrip(".")
    category = str(raw.get("category", "")).strip()
    if not category:
        category = category_for(Path(path), is_dir)
    return {
        "path": path,
        "root": str(raw.get("root", "")),
        "relativePath": str(raw.get("relativePath", name)),
        "parentPath": str(raw.get("parentPath", str(Path(path).parent))),
        "name": name,
        "isDir": is_dir,
        "suffix": suffix,
        "category": category,
        "tags": tags,
        "modified": str(raw.get("modified", "")),
        "size": int(raw.get("size", 0) or 0),
        "content": compact_text(str(raw.get("content", "")), DEFAULT_MAX_CONTENT),
        "extractor": str(raw.get("extractor", "provider")),
        "mtimeNs": int(raw.get("mtimeNs", 0) or 0),
        "metaMtimeNs": int(raw.get("metaMtimeNs", 0) or 0),
        "ocrEnabled": False,
        "source": "provider:" + provider_name,
    }


def collect_provider_items(config_path: Path, roots: List[Path],
                           errors: List[Dict]) -> List[Dict]:
    if not config_path.is_file():
        return []
    config = read_json(config_path)
    providers = config.get("providers", [])
    if not isinstance(providers, list):
        errors.append({"path": str(config_path), "error": "providers must be an array"})
        return []
    result: List[Dict] = []
    for original in providers:
        if not isinstance(original, dict) or original.get("enabled", True) is False:
            continue
        provider = inherited_provider(original, config_path.parent)
        name = str(provider.get("name", provider.get("type", "provider")))
        try:
            payload = run_provider(provider, roots)
            raw_items = payload.get("items", []) if isinstance(payload, dict) else payload
            if not isinstance(raw_items, list):
                raise ValueError("provider response must be an array or {items: []}")
            for raw in raw_items[:max(1, min(int(provider.get("limit", 5000)), 100000))]:
                try:
                    result.append(normalize_provider_item(raw, name))
                except (TypeError, ValueError) as error:
                    errors.append({"path": name, "error": str(error)[:300]})
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError,
                urllib.error.URLError, json.JSONDecodeError) as error:
            errors.append({"path": name, "error": str(error)[:300]})
    return result


def previous_items(output: Path) -> Dict[str, Dict]:
    value = read_json(output)
    if value.get("version") != INDEX_VERSION:
        return {}
    items = value.get("items", [])
    return {str(item.get("path")): item for item in items
            if isinstance(item, dict)
            and item.get("source", "filesystem") == "filesystem"
            and isinstance(item.get("path"), str)}


def checkpoint_items(path: Path) -> Dict[str, Dict]:
    """Read completed entries from an older append-only checkpoint.

    This intentionally ignores the old command signature: every candidate is
    still validated by path, size and mtime before reuse.  It lets the new fast
    pipeline retain OCR work completed by releases that coupled OCR to full
    rebuilds, without mutating or deleting the legacy checkpoint.
    """
    result: Dict[str, Dict] = {}
    try:
        with path.open("r", encoding="utf-8") as source:
            for line in source:
                try:
                    record = json.loads(line)
                except (ValueError, UnicodeError):
                    continue
                item = record.get("item") if isinstance(record, dict) else None
                if (isinstance(item, dict)
                        and isinstance(item.get("path"), str)):
                    result[item["path"]] = item
    except OSError:
        pass
    return result


def item_ocr_state(item: Dict) -> Tuple[str, str]:
    state = str(item.get("ocrStatus", ""))
    if state in {"complete", "not-needed", "unavailable"}:
        return state, str(item.get("ocrReason", ""))
    return ocr_state(
        Path(str(item.get("path", ""))), bool(item.get("isDir")),
        str(item.get("content", "")), str(item.get("extractor", "metadata")),
        int(item.get("ocrPdfPages", MAX_OCR_PDF_PAGES)))


def extract_pdf_text_layer(path: Path, limit: int) -> Tuple[str, str]:
    """Read a PDF text layer without ever starting image OCR."""
    if path.suffix.lower() == ".pdf":
        text, extractor = extract_content(path, False, limit, MAX_OCR_PDF_PAGES)
        if text.strip() and extractor.startswith("pdftotext"):
            return text, extractor
    return "", "pdf-no-text"


def run_pending_ocr(output: Path, resume_state: str, max_content: int,
                    max_total_content: int, process_nice: object,
                    io_priority: str) -> int:
    """OCR only items tagged pending in an existing fast snapshot.

    The published index is replaced atomically only after a pass completes.
    Completed files are appended to a separate journal so Pause never damages
    the searchable fast index and a later OCR click resumes completed work.
    """
    payload = read_json(output)
    raw_items = payload.get("items", [])
    if payload.get("version") != INDEX_VERSION or not isinstance(raw_items, list):
        print(json.dumps({"error": "missing-fast-index"}), flush=True)
        return 2
    items = [item for item in raw_items if isinstance(item, dict)]
    pending = [item for item in items if item_ocr_state(item)[0] == "pending"]
    print(json.dumps({"phase": "counting", "task": "ocr"},
                     ensure_ascii=False), flush=True)
    print(json.dumps({"total": len(pending), "progress": 0,
                      "task": "ocr"}, ensure_ascii=False), flush=True)

    signature = {
        "indexVersion": INDEX_VERSION,
        "mode": "ocr-pending",
        "snapshotGeneratedAt": str(payload.get("generatedAt", "")),
        "maxContent": max_content,
        "maxTotalContent": max_total_content,
    }
    journal = (ResumeJournal(Path(resume_state).expanduser(), signature)
               if resume_state else None)
    saved = journal.entries if journal else {}

    existing_chars = sum(len(str(item.get("content", ""))) for item in items)
    content_budget = max(0, max_total_content - existing_chars)
    updates: Dict[str, Dict] = {}
    errors = payload.get("errors", [])
    if not isinstance(errors, list):
        errors = []
    completed = 0
    preflight_completed = 0
    text_layer_pdfs = 0
    reused = 0
    extracted = 0
    content_chars = 0
    budget_exhausted = False
    pdf_ready = bool(shutil.which("pdftoppm") and shutil.which("tesseract"))
    image_ready = bool(shutil.which("tesseract"))

    def cached_matches(cached: object, original: Dict) -> bool:
        return (isinstance(cached, dict)
                and cached.get("mtimeNs") == original.get("mtimeNs")
                and cached.get("size", 0) == original.get("size", 0))

    resume_preflight = sum(
        1 for original in pending
        if cached_matches(saved.get(str(original.get("path", ""))), original)
        and (saved[str(original.get("path", ""))].get("ocrPreflight")
             or item_ocr_state(saved[str(original.get("path", ""))])[0]
             != "pending"))
    resume_ocr_completed = sum(
        1 for original in pending
        if cached_matches(saved.get(str(original.get("path", ""))), original)
        and item_ocr_state(saved[str(original.get("path", ""))])[0]
        == "complete")
    if saved:
        print(json.dumps({"resuming": len(saved),
                          "resumePreflight": resume_preflight,
                          "resumeOcrCompleted": resume_ocr_completed,
                          "task": "ocr"}, ensure_ascii=False), flush=True)

    # Phase 1 is deliberately Tesseract-free.  Fast-full snapshots created
    # after their global text budget is filled conservatively mark later PDFs
    # as pending.  Inspect every PDF text layer first so Word/WPS-exported PDFs
    # never enter the slow image-OCR queue.
    for original in pending:
        path_value = str(original.get("path", ""))
        path = Path(path_value)
        cached = saved.get(path_value)
        reused_this_item = cached_matches(cached, original) and (
                cached.get("ocrPreflight") or
                item_ocr_state(cached)[0] != "pending")
        if reused_this_item:
            updated = cached
            reused += 1
        else:
            suffix = path.suffix.lower()
            updated = dict(original)
            if not path.is_file():
                updated["ocrStatus"] = "unavailable"
                updated["ocrReason"] = "file-missing"
            elif suffix in IMAGE_SUFFIXES:
                updated["ocrStatus"] = "pending"
                updated["ocrReason"] = "image"
                updated["ocrPreflight"] = True
            elif suffix == ".pdf":
                text_limit = max(2_000, min(max_content, content_budget))
                content, extractor = extract_pdf_text_layer(path, text_limit)
                if content.strip():
                    updated["content"] = content
                    updated["extractor"] = extractor
                    updated["ocrStatus"] = "not-needed"
                    updated["ocrReason"] = "pdf-text-layer"
                    content_budget -= len(content)
                    content_chars += len(content)
                    text_layer_pdfs += 1
                else:
                    updated["ocrStatus"] = "pending"
                    updated["ocrReason"] = "pdf-image-only"
                updated["ocrPreflight"] = True
            else:
                updated["ocrStatus"] = "unavailable"
                updated["ocrReason"] = "unsupported-format"
            if journal:
                journal.record(updated)
        updates[path_value] = updated
        preflight_completed += 1
        # Replayed checkpoints can cover tens of thousands of files.  Emit
        # occasional aggregate progress for those cached entries instead of
        # flooding the Qt event loop with one JSON line per file.
        if (not reused_this_item or preflight_completed % 250 == 0
                or preflight_completed == len(pending)):
            print(json.dumps({"phase": "ocr-preflight",
                              "progress": preflight_completed,
                              "total": len(pending), "path": path_value,
                              "reused": reused, "task": "ocr"},
                             ensure_ascii=False), flush=True)

    ocr_candidates = [item for item in updates.values()
                      if item_ocr_state(item)[0] == "pending"]
    # Small files first: users see useful progress immediately and one
    # several-hundred-page scan cannot pin the counter at 1 for an hour.
    ocr_candidates.sort(key=lambda item: (
        int(item.get("size", 0)), str(item.get("path", "")).casefold()))
    ocr_total = resume_ocr_completed + len(ocr_candidates)
    print(json.dumps({"phase": "ocr-running",
                      "progress": resume_ocr_completed,
                      "total": ocr_total, "remaining": len(ocr_candidates),
                      "resumeOcrCompleted": resume_ocr_completed,
                      "task": "ocr",
                      "textLayerPdfs": text_layer_pdfs},
                     ensure_ascii=False), flush=True)

    for original in ocr_candidates:
        path_value = str(original.get("path", ""))
        path = Path(path_value)
        suffix = path.suffix.lower()
        ready = image_ready if suffix in IMAGE_SUFFIXES else pdf_ready
        updated = dict(original)
        if not path.is_file():
            updated["ocrStatus"] = "unavailable"
            updated["ocrReason"] = "file-missing"
        elif not ready:
            updated["ocrStatus"] = "unavailable"
            updated["ocrReason"] = "ocr-tools-missing"
        elif content_budget <= 0:
            budget_exhausted = True
            break
        else:
            extraction_limit = min(max_content, content_budget)
            if suffix == ".pdf":
                def page_progress(page: int, pages: int) -> None:
                    print(json.dumps({"phase": "ocr-pages",
                                      "progress": resume_ocr_completed + completed,
                                      "total": ocr_total,
                                      "page": page, "pages": pages,
                                      "path": path_value, "task": "ocr"},
                                     ensure_ascii=False), flush=True)
                content = ocr_pdf(path, extraction_limit, 0, page_progress)
                extractor = "pdf-tesseract" if content else "pdf-tesseract-empty"
            else:
                content = ocr_image(path, extraction_limit)
                extractor = "tesseract" if content else "tesseract-empty"
            updated["content"] = content
            updated["extractor"] = extractor
            updated["ocrEnabled"] = True
            updated["ocrPdfPages"] = 0
            updated["ocrPreflight"] = True
            state, reason = ocr_state(path, False, content, extractor, 0)
            if suffix == ".pdf" and not content.strip():
                state, reason = "complete", "ocr-no-text"
            updated["ocrStatus"] = state
            updated["ocrReason"] = reason
            content_budget -= len(content)
            content_chars += len(content)
            extracted += 1
        if journal:
            journal.record(updated)
        updates[path_value] = updated
        completed += 1
        print(json.dumps({"phase": "ocr-running",
                          "progress": resume_ocr_completed + completed,
                          "total": ocr_total, "path": path_value,
                          "task": "ocr"}, ensure_ascii=False), flush=True)

    for index, item in enumerate(items):
        replacement = updates.get(str(item.get("path", "")))
        if replacement is not None:
            items[index] = replacement
        elif "ocrStatus" not in item:
            state, reason = item_ocr_state(item)
            item["ocrStatus"] = state
            item["ocrReason"] = reason

    remaining = sum(1 for item in items if item_ocr_state(item)[0] == "pending")
    payload["items"] = items
    payload["generatedAt"] = dt.datetime.now(dt.timezone.utc).isoformat()
    payload["ocrBackfill"] = True
    stats = payload.get("stats", {})
    if not isinstance(stats, dict):
        stats = {}
    stats.update({"ocrCompleted": resume_ocr_completed + completed,
                  "ocrCompletedThisRun": completed,
                  "ocrPreflight": preflight_completed,
                  "ocrTextLayerPdfs": text_layer_pdfs, "ocrReused": reused,
                  "ocrExtracted": extracted, "ocrContentChars": content_chars,
                  "ocrPending": remaining,
                  "ocrBudgetExhausted": int(budget_exhausted)})
    payload["stats"] = stats
    capabilities = payload.get("capabilities", {})
    if not isinstance(capabilities, dict):
        capabilities = {}
    capabilities.update({"pdfOcr": pdf_ready, "imageOcr": image_ready,
                         "ocrQueue": True, "ocrMaxContentPerFile": max_content,
                         "ocrMaxTotalContent": max_total_content,
                         "processNice": process_nice,
                         "ioPriority": io_priority})
    payload["capabilities"] = capabilities
    payload["errors"] = errors[:200]
    atomic_json_write(output, payload)
    if journal:
        journal.close(completed=True)
    print(json.dumps({"done": True, "task": "ocr", "items": len(items),
                      "ocrCompleted": resume_ocr_completed + completed,
                      "ocrCompletedThisRun": completed,
                      "ocrPending": remaining,
                      "budgetExhausted": budget_exhausted},
                     ensure_ascii=False), flush=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--roots-json", required=True)
    parser.add_argument("--exclude-roots-json", default="[]")
    parser.add_argument("--include-extensions-json", default="[]")
    parser.add_argument("--output", required=True)
    parser.add_argument("--ocr-images", action="store_true")
    parser.add_argument("--ocr-pdf-pages", type=int,
                        default=MAX_OCR_PDF_PAGES,
                        help="0 means every PDF page")
    parser.add_argument("--full-rebuild", action="store_true")
    parser.add_argument("--fast-full", action="store_true",
                        help="unlimited full snapshot without OCR")
    parser.add_argument("--ocr-pending", action="store_true",
                        help="OCR only pending items from the current snapshot")
    parser.add_argument("--no-tags", action="store_true")
    parser.add_argument("--max-content", type=int, default=DEFAULT_MAX_CONTENT)
    parser.add_argument("--max-items", type=int, default=DEFAULT_MAX_ITEMS)
    parser.add_argument("--max-total-content", type=int,
                        default=DEFAULT_MAX_TOTAL_CONTENT)
    parser.add_argument("--provider-config", default="")
    parser.add_argument("--resume-state", default="",
                        help="append-only checkpoint used by full/OCR tasks")
    args = parser.parse_args()
    try:
        os.nice(19 if (args.full_rebuild or args.fast_full or args.ocr_pending) else 10)
    except OSError:
        pass
    try:
        process_nice = os.getpriority(os.PRIO_PROCESS, 0)
    except (AttributeError, OSError):
        process_nice = None
    io_priority = ""
    ionice = shutil.which("ionice")
    if ionice:
        io_priority = run_text_command(
            [ionice, "-p", str(os.getpid())], 5).strip()

    roots = parse_roots(args.roots_json)
    excluded_roots = parse_path_list(args.exclude_roots_json)
    included_extensions = parse_extensions(args.include_extensions_json)
    output = Path(args.output).expanduser()
    max_content = max(2_000, min(args.max_content, 100_000))
    max_total_cap = 256 * 1024 * 1024 if args.ocr_pending else 64 * 1024 * 1024
    max_total_content = max(
        1_000_000, min(args.max_total_content, max_total_cap))
    if args.ocr_pending:
        return run_pending_ocr(output, args.resume_state, max_content,
                               max_total_content, process_nice, io_priority)
    errors: List[Dict] = []
    items: List[Dict] = []
    progress = [0]
    stats = {"reused": 0, "extracted": 0, "deleted": 0,
             "contentChars": 0, "truncated": 0}
    # A full rebuild is unconditionally unlimited by item count.  Enforce the
    # policy inside the indexer as well as in the widget so an older caller or
    # a resumed command cannot accidentally reintroduce the daily 25k guard.
    # A zero limit is also honoured for incremental updates.  Truncating an
    # existing full snapshot is data loss, not resource control.
    rebuild_mode = bool(args.full_rebuild or args.fast_full)
    max_items = (0 if rebuild_mode or args.max_items <= 0
                 else max(1_000, min(args.max_items, 100_000)))
    content_budget = [max_total_content]
    ocr_pdf_pages = max(0, min(args.ocr_pdf_pages, 10_000))
    read_tags = not args.no_tags
    journal = None
    previous = previous_items(output)
    if rebuild_mode:
        # A legacy explicit full rebuild starts clean.  Fast full keeps rich,
        # unchanged text/OCR from the current snapshot while still walking an
        # unlimited set of configured files.
        if args.full_rebuild:
            previous = {}
        elif args.fast_full:
            # Preserve already completed files from the former slow all-page
            # full-index journal.  The journal remains untouched as a safety
            # fallback until the user chooses to remove it.
            previous.update(checkpoint_items(
                Path(str(output) + ".resume.jsonl")))
        if args.resume_state:
            signature = {
                "indexVersion": INDEX_VERSION,
                "roots": [str(root) for root in roots],
                "ocrImages": bool(args.ocr_images),
                "ocrPdfPages": ocr_pdf_pages,
                "readTags": read_tags,
                "maxContent": max_content,
                "maxTotalContent": max_total_content,
                "mode": "fast-full" if args.fast_full else "full-rebuild",
            }
            if excluded_roots:
                signature["excludedRoots"] = excluded_roots
            if included_extensions and "*" not in included_extensions:
                signature["includedExtensions"] = included_extensions
            journal = ResumeJournal(Path(args.resume_state).expanduser(), signature)
            checkpoint = journal.entries
            if checkpoint:
                previous.update(checkpoint)
                print(json.dumps({"resuming": len(checkpoint)},
                                 ensure_ascii=False), flush=True)
    print(json.dumps({"phase": "counting"}, ensure_ascii=False), flush=True)
    total_items = count_candidates(roots, max_items, excluded_roots,
                                   included_extensions)
    print(json.dumps({"total": total_items, "progress": 0},
                     ensure_ascii=False), flush=True)
    state = {"items": 0, "truncated": 0}
    for root in roots:
        items.extend(scan_root(root, args.ocr_images,
                               max_content, errors, progress, previous, stats,
                               state, max_items, content_budget,
                               ocr_pdf_pages, read_tags, total_items, journal,
                               excluded_roots, included_extensions))
    print(json.dumps({"progress": state["items"], "total": total_items,
                      "path": str(items[-1].get("path", "")) if items else ""},
                     ensure_ascii=False), flush=True)
    stats["truncated"] = state["truncated"]
    if state["truncated"]:
        errors.append({
            "path": "<limits>",
            "error": "item limit reached; narrow the indexed roots",
        })

    if args.provider_config:
        items.extend(collect_provider_items(
            Path(args.provider_config).expanduser(), roots, errors))
    if not read_tags:
        # The widget's tag feature is intentionally removed.  Also discard
        # legacy/provider tag fields so a no-tag snapshot cannot reintroduce
        # them indirectly.
        for item in items:
            item["tags"] = []
    stats["ocrPending"] = sum(
        1 for item in items if isinstance(item, dict)
        and item_ocr_state(item)[0] == "pending")
    # A local filesystem record is authoritative when a provider returns the
    # same path.  Providers still contribute virtual/external paths.
    deduplicated: Dict[str, Dict] = {}
    for item in items:
        key = str(item.get("path", ""))
        if key and (key not in deduplicated or item.get("source") == "filesystem"):
            deduplicated[key] = item
    items = list(deduplicated.values())
    if max_items > 0 and len(items) > max_items:
        items = items[:max_items]
        stats["truncated"] = 1
    current_paths = {str(item.get("path")) for item in items
                     if item.get("source") == "filesystem"}
    stats["deleted"] = len(set(previous) - current_paths)

    tags = (sorted({tag for item in items for tag in item.get("tags", [])},
                   key=str.casefold) if read_tags else [])
    payload = {
        "version": INDEX_VERSION,
        "generatedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
        "roots": [str(root) for root in roots],
        "excludedRoots": excluded_roots,
        "includedExtensions": included_extensions,
        "ocrImages": bool(args.ocr_images),
        "fullRebuild": rebuild_mode,
        "indexMode": ("fast-full" if args.fast_full else
                      "full-ocr" if args.full_rebuild else "incremental"),
        "ocrPdfPages": ocr_pdf_pages,
        "tagsEnabled": read_tags,
        "items": items,
        "tags": tags,
        "errors": errors[:200],
        "stats": stats,
        "capabilities": {
            "pdfText": bool(shutil.which("pdftotext")),
            "pdfOcr": bool(shutil.which("pdftoppm") and shutil.which("tesseract")),
            "imageOcr": bool(shutil.which("tesseract")),
            "ooxml": True,
            "legacyOffice": bool(shutil.which("strings")),
            "providers": True,
            "maxItems": max_items,
            "maxContentPerFile": max_content,
            "maxTotalContent": max_total_content,
            "ocrPdfPages": ocr_pdf_pages,
            "tags": read_tags,
            "excludedRoots": len(excluded_roots),
            "includedExtensions": included_extensions,
            "processNice": process_nice,
            "ioPriority": io_priority,
        },
    }
    atomic_json_write(output, payload)
    if journal:
        journal.close(completed=True)
    print(json.dumps({"done": True, "items": len(items), "errors": len(errors),
                      "stats": stats}, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
