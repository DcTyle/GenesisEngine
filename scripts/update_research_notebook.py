from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
from typing import Any


DEFAULT_CONFIG_PATH = pathlib.Path("ProjectSettings/notebook_logging.json")
DEFAULT_NOTEBOOK_PATH = pathlib.Path("ResearchConfinement/docs_v4/ENGINE_BUILD_RUNTIME_LAB_NOTEBOOK.md")
DEFAULT_STATE_PATH = pathlib.Path("GenesisEngineState/Logs/notebook_logger_state.json")
DEFAULT_KNOWN_LOGS = [
    "GenesisEngineState/Logs/genesis_engine.log",
    "GenesisEngineState/Logs/genesis_runtime.log",
    "GenesisEngineState/Logs/genesis_viewport.log",
    "out/build/gui_log.txt",
    "out/build/smoke_log.txt",
    "build_config_log.txt",
    "build_config_log_capture.txt",
]
ERROR_RE = re.compile(r"\b(error|fatal|exception|traceback|validate_fail|structured exception)\b", re.IGNORECASE)
WARNING_RE = re.compile(r"\b(warning|warn|deprecated)\b", re.IGNORECASE)
STAGE_HINTS = {
    "configure": "configure",
    "build": "build",
    "install": "install",
    "package": "package",
    "validate": "validate",
    "runtime": "runtime",
    "editor": "editor",
    "viewport": "viewport",
    "smoke": "smoke",
    "gui": "gui",
}


def load_json(path: pathlib.Path, default: Any) -> Any:
    if not path.exists():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return default


def ensure_parent(path: pathlib.Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def resolve_repo_root(explicit: str | None) -> pathlib.Path:
    if explicit:
        return pathlib.Path(explicit).resolve()
    return pathlib.Path(__file__).resolve().parent.parent


def sample_bytes(path: pathlib.Path, chunk_size: int = 65536) -> bytes:
    size = path.stat().st_size
    with path.open("rb") as handle:
        if size <= chunk_size * 2:
            return handle.read()
        head = handle.read(chunk_size)
        handle.seek(max(size - chunk_size, 0))
        tail = handle.read(chunk_size)
    return head + b"\n...\n" + tail


def fingerprint(path: pathlib.Path) -> str:
    stat = path.stat()
    digest = hashlib.sha1(sample_bytes(path)).hexdigest()
    return f"{stat.st_size}:{stat.st_mtime_ns}:{digest}"


def trim_line(line: str, max_len: int = 240) -> str:
    line = line.strip()
    if len(line) <= max_len:
        return line
    return line[: max_len - 12] + "... (clipped)"


def classify_stage(path: pathlib.Path) -> str:
    lower = path.name.lower()
    for token, label in STAGE_HINTS.items():
        if token in lower:
            return label
    return "log"


def collect_record(path: pathlib.Path, max_key_lines: int, tail_line_count: int) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    errors = [trim_line(line) for line in lines if ERROR_RE.search(line)]
    warnings = [trim_line(line) for line in lines if WARNING_RE.search(line)]
    interesting: list[str] = []
    for line in errors + warnings:
        if line and line not in interesting:
            interesting.append(line)
    if not interesting:
        for line in lines[-tail_line_count:]:
            clipped = trim_line(line)
            if clipped and clipped not in interesting:
                interesting.append(clipped)
    interesting = interesting[-max_key_lines:]
    return {
        "path": path,
        "stage": classify_stage(path),
        "line_count": len(lines),
        "error_count": len(errors),
        "warning_count": len(warnings),
        "first_error": errors[0] if errors else "",
        "first_warning": warnings[0] if warnings else "",
        "tail": trim_line(lines[-1]) if lines else "",
        "head": trim_line(lines[0]) if lines else "",
        "key_lines": interesting,
        "mtime_utc": dt.datetime.fromtimestamp(path.stat().st_mtime, tz=dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ"),
    }


def build_aethen_bullets(event_name: str, status: str, records: list[dict[str, Any]]) -> list[str]:
    total_errors = sum(record["error_count"] for record in records)
    total_warnings = sum(record["warning_count"] for record in records)
    runtime_records = [record for record in records if record["stage"] in {"runtime", "editor", "viewport", "gui", "smoke"}]
    build_records = [record for record in records if record["stage"] in {"configure", "build", "install", "package", "validate"}]
    changed_count = len(records)

    bullets: list[str] = []
    failed = "fail" in status.lower() or total_errors > 0
    if failed:
        bullets.append(
            f"{event_name} ended with status {status}; {total_errors} error-like lines were detected across {changed_count} changed log artifacts."
        )
        primary = next((record for record in records if record["first_error"]), None)
        if primary:
            bullets.append(
                f"Primary failure signal came from {primary['path'].as_posix()}: {primary['first_error']}"
            )
    else:
        bullets.append(
            f"{event_name} ended with status {status}; {changed_count} changed log artifacts were appended without error-like lines."
        )

    if total_warnings > 0:
        warning_record = next((record for record in records if record["first_warning"]), None)
        if warning_record:
            bullets.append(
                f"{total_warnings} warning-like lines were observed; the first warning came from {warning_record['path'].as_posix()}: {warning_record['first_warning']}"
            )
        else:
            bullets.append(f"{total_warnings} warning-like lines were observed in the changed logs.")

    if build_records:
        seen_stages = []
        for record in build_records:
            stage = record["stage"]
            if stage not in seen_stages:
                seen_stages.append(stage)
        bullets.append(f"Build stages observed in this capture: {', '.join(seen_stages)}.")

    if runtime_records:
        newest_runtime = max(runtime_records, key=lambda record: record["mtime_utc"])
        bullets.append(
            f"Runtime/editor traces also moved during this capture; the newest runtime marker is from {newest_runtime['path'].as_posix()}: {newest_runtime['tail'] or newest_runtime['head']}"
        )
    else:
        bullets.append("No runtime/editor log delta was detected during this capture window.")

    if not bullets:
        bullets.append(f"{event_name} produced no analyzable log lines; raw artifacts are still linked below.")
    return bullets[:5]


def build_copilot_prompt(event_name: str, status: str, records: list[dict[str, Any]]) -> str:
    payload = {
        "task": "Summarize Genesis Engine build/runtime logs for an append-only research notebook.",
        "requirements": [
            "Return 3-5 concise factual bullets.",
            "Do not invent repo or runtime facts.",
            "Mention failures first if present, then warnings, then what changed.",
        ],
        "event": event_name,
        "status": status,
        "logs": [
            {
                "path": record["path"].as_posix(),
                "stage": record["stage"],
                "line_count": record["line_count"],
                "error_count": record["error_count"],
                "warning_count": record["warning_count"],
                "key_lines": record["key_lines"],
            }
            for record in records
        ],
    }
    return json.dumps(payload, indent=2)


def run_copilot_bridge(command: str, timeout_seconds: int, prompt: str) -> str:
    completed = subprocess.run(
        command,
        input=prompt,
        capture_output=True,
        text=True,
        timeout=timeout_seconds,
        shell=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or f"bridge returned {completed.returncode}")
    output = completed.stdout.strip()
    if not output:
        raise RuntimeError("bridge returned empty output")
    return output


def build_ai_note(provider: str, config: dict[str, Any], event_name: str, status: str, records: list[dict[str, Any]]) -> tuple[list[str], str]:
    provider = (provider or "aethen").lower()
    aethen_bullets = build_aethen_bullets(event_name, status, records)
    copilot_cfg = config.get("providers", {}).get("copilot", {})
    bridge_command = os.environ.get("GENESIS_NOTEBOOK_COPILOT_BRIDGE", "").strip() or str(copilot_cfg.get("bridge_command", "")).strip()
    timeout_seconds = int(copilot_cfg.get("timeout_seconds", 45))
    fallback_allowed = bool(copilot_cfg.get("fallback_to_aethen", True))

    if provider == "aethen":
        return aethen_bullets, "aethen"

    if provider == "copilot":
        if bridge_command:
            try:
                output = run_copilot_bridge(bridge_command, timeout_seconds, build_copilot_prompt(event_name, status, records))
                bullets = [line.strip("- ").strip() for line in output.splitlines() if line.strip()]
                bullets = [bullet for bullet in bullets if bullet]
                if bullets:
                    return bullets[:5], "copilot"
            except Exception as exc:
                if not fallback_allowed:
                    return [f"Copilot bridge failed: {exc}"], "copilot_failed"
                aethen_bullets.insert(0, f"Copilot bridge was requested but unavailable; Aethen fallback was used instead ({exc}).")
                return aethen_bullets[:5], "aethen_fallback"
        if fallback_allowed:
            aethen_bullets.insert(0, "Copilot bridge was requested but no bridge command is configured; Aethen fallback was used instead.")
            return aethen_bullets[:5], "aethen_fallback"
        return ["Copilot bridge was requested but no bridge command is configured."], "copilot_unconfigured"

    if provider == "hybrid":
        if bridge_command:
            try:
                output = run_copilot_bridge(bridge_command, timeout_seconds, build_copilot_prompt(event_name, status, records))
                copilot_bullets = [line.strip("- ").strip() for line in output.splitlines() if line.strip()]
                merged: list[str] = []
                if copilot_bullets:
                    merged.extend(copilot_bullets[:3])
                merged.extend(aethen_bullets[:2])
                deduped: list[str] = []
                for bullet in merged:
                    if bullet and bullet not in deduped:
                        deduped.append(bullet)
                return deduped[:5], "hybrid"
            except Exception as exc:
                aethen_bullets.insert(0, f"Hybrid mode fell back to Aethen because the Copilot bridge failed ({exc}).")
                return aethen_bullets[:5], "aethen_fallback"
        aethen_bullets.insert(0, "Hybrid mode fell back to Aethen because no Copilot bridge command is configured.")
        return aethen_bullets[:5], "aethen_fallback"

    return aethen_bullets, "aethen"


def relpath_for_markdown(repo_root: pathlib.Path, notebook_path: pathlib.Path, target: pathlib.Path) -> tuple[str, str]:
    rel_from_root = target.resolve().relative_to(repo_root.resolve()).as_posix()
    link = pathlib.Path(os.path.relpath(target.resolve(), notebook_path.parent.resolve())).as_posix()
    return rel_from_root, link


def notebook_header() -> str:
    return """# Engine Build And Runtime Lab Notebook

This append-only notebook is generated by the Genesis Engine build/install/package scripts and by the editor/runtime shutdown hook.

Provider routing:
- `aethen`: deterministic native summarizer built into the repo logging pipeline
- `copilot`: external bridge command intended for VS Code GitHub Copilot API integration
- `hybrid`: use the Copilot bridge when available and merge with Aethen notes

The provider selection and bridge command live in `ProjectSettings/notebook_logging.json`.
Each entry below is appended in sequence order; prior notebook text is never rewritten.
"""


def append_entry(
    notebook_path: pathlib.Path,
    repo_root: pathlib.Path,
    entry_index: int,
    timestamp_utc: str,
    event_name: str,
    status: str,
    provider_requested: str,
    provider_used: str,
    script_name: str,
    records: list[dict[str, Any]],
    ai_bullets: list[str],
) -> None:
    ensure_parent(notebook_path)
    if not notebook_path.exists():
        notebook_path.write_text(notebook_header(), encoding="utf-8")

    chunks: list[str] = []
    chunks.append(f"\n## Entry {entry_index:04d} - {timestamp_utc} - {event_name}\n")
    chunks.append("Metadata:\n")
    chunks.append(f"- event: {event_name}\n")
    chunks.append(f"- status: {status}\n")
    chunks.append(f"- provider requested: {provider_requested}\n")
    chunks.append(f"- provider used: {provider_used}\n")
    chunks.append(f"- script: {script_name}\n")
    chunks.append("\nArtifacts:\n")
    for record in records:
        display_path, link_path = relpath_for_markdown(repo_root, notebook_path, record["path"])
        chunks.append(f"- [{display_path}]({link_path})\n")

    chunks.append("\nAI lab note:\n")
    for bullet in ai_bullets:
        chunks.append(f"- {bullet}\n")

    chunks.append("\nPer-log observations:\n")
    for record in records:
        display_path, link_path = relpath_for_markdown(repo_root, notebook_path, record["path"])
        chunks.append(f"\n### [{display_path}]({link_path})\n")
        chunks.append(f"- stage: {record['stage']}\n")
        chunks.append(f"- lines: {record['line_count']}\n")
        chunks.append(f"- warning-like lines: {record['warning_count']}\n")
        chunks.append(f"- error-like lines: {record['error_count']}\n")
        chunks.append(f"- modified utc: {record['mtime_utc']}\n")
        if record["head"]:
            chunks.append(f"- first line: {record['head']}\n")
        if record["tail"]:
            chunks.append(f"- last line: {record['tail']}\n")
        chunks.append("Key lines:\n")
        chunks.append("```text\n")
        for line in record["key_lines"]:
            chunks.append(line + "\n")
        chunks.append("```\n")

    with notebook_path.open("a", encoding="utf-8") as handle:
        handle.write("".join(chunks))


def unique_existing_paths(repo_root: pathlib.Path, candidates: list[str]) -> list[pathlib.Path]:
    out: list[pathlib.Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        path = pathlib.Path(candidate)
        if not path.is_absolute():
            path = repo_root / path
        path = path.resolve()
        key = str(path).lower()
        if key in seen or not path.exists() or not path.is_file():
            continue
        seen.add(key)
        out.append(path)
    return out


def command_capture(args: argparse.Namespace) -> int:
    repo_root = resolve_repo_root(args.repo_root)
    config_path = repo_root / DEFAULT_CONFIG_PATH
    config = load_json(config_path, {})
    notebook_path = repo_root / pathlib.Path(config.get("notebook_path", str(DEFAULT_NOTEBOOK_PATH)))
    state_path = repo_root / pathlib.Path(config.get("state_path", str(DEFAULT_STATE_PATH)))
    ensure_parent(state_path)
    state = load_json(state_path, {"entry_counter": 0, "processed_logs": {}})

    provider = args.provider or os.environ.get("GENESIS_NOTEBOOK_PROVIDER", "") or str(config.get("provider", "aethen"))
    known_logs = list(config.get("known_log_paths", DEFAULT_KNOWN_LOGS))
    explicit_logs = list(args.log or [])
    candidate_logs = explicit_logs + (known_logs if args.also_known_logs or config.get("append_runtime_logs", False) else [])
    paths = unique_existing_paths(repo_root, candidate_logs)

    aethen_cfg = config.get("providers", {}).get("aethen", {})
    max_key_lines = int(aethen_cfg.get("max_key_lines", 10))
    tail_line_count = int(aethen_cfg.get("tail_line_count", 16))

    changed_records: list[dict[str, Any]] = []
    processed_logs = state.get("processed_logs", {})
    for path in paths:
        fp = fingerprint(path)
        previous = processed_logs.get(str(path), {})
        if previous.get("fingerprint") == fp:
            continue
        record = collect_record(path, max_key_lines=max_key_lines, tail_line_count=tail_line_count)
        record["fingerprint"] = fp
        changed_records.append(record)

    if not changed_records:
        print("No changed log files detected; notebook unchanged.")
        return 0

    event_name = args.event or "notebook_capture"
    status = args.status or "observed"
    ai_bullets, provider_used = build_ai_note(provider, config, event_name, status, changed_records)

    entry_index = int(state.get("entry_counter", 0)) + 1
    timestamp_utc = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    append_entry(
        notebook_path=notebook_path,
        repo_root=repo_root,
        entry_index=entry_index,
        timestamp_utc=timestamp_utc,
        event_name=event_name,
        status=status,
        provider_requested=provider.lower(),
        provider_used=provider_used,
        script_name=args.script or "scripts/update_research_notebook.py",
        records=changed_records,
        ai_bullets=ai_bullets,
    )

    for record in changed_records:
        processed_logs[str(record["path"])] = {
            "fingerprint": record["fingerprint"],
            "mtime_utc": record["mtime_utc"],
            "entry_index": entry_index,
        }
    state["entry_counter"] = entry_index
    state["processed_logs"] = processed_logs
    state_path.write_text(json.dumps(state, indent=2, sort_keys=True), encoding="utf-8")

    print(f"Appended entry {entry_index:04d} to {notebook_path}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Append Genesis Engine build/runtime logs to the research notebook.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture = subparsers.add_parser("capture", help="Append one notebook entry for changed log artifacts.")
    capture.add_argument("--repo-root", default="", help="Repository root. Defaults to the script's parent repo.")
    capture.add_argument("--event", default="", help="Logical event name, such as build_game_win64 or genesis_editor_session.")
    capture.add_argument("--status", default="observed", help="Status label written into the notebook entry.")
    capture.add_argument("--provider", default="", choices=["aethen", "copilot", "hybrid"], help="AI note provider.")
    capture.add_argument("--script", default="scripts/update_research_notebook.py", help="Originating script or subsystem.")
    capture.add_argument("--log", action="append", default=[], help="Explicit log file to include; repeat as needed.")
    capture.add_argument("--also-known-logs", action="store_true", help="Also scan configured runtime/build log locations.")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.command == "capture":
        return command_capture(args)
    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())