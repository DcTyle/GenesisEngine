# Path: core/state_eq.py
# Description:
#   Full-state vector lifecycle helpers for live mode.
#   - Enforces persistent, tokenized storage of state-vectors in VSD.
#   - Guards every write with confirm_live_mode(); no ephemeral or test buffers.
#   - Provides integrity hashing, partial slice rehydration, and safe replacement.
#   - Pure ASCII; deterministic roundtrip via utils.serialize_vector_with_tokens().
#
#   VSD layout (conventional):
#     <root>/state_text            : tokenized ascii string
#     <root>/state_dict            : token dictionary {token->chunk}
#     <root>/integrity             : sha256 over state_text
#     <root>/meta                  : {size_chars, len, last_write_s}
#
#   Exports:
#     save_vector(root: str, vec: List[complex]) -> dict
#     load_vector(root: str) -> dict
#     slice_vector(root: str, start_char: int, length: int) -> dict
#     replace_segment(root: str, start_char: int, text: str) -> dict
#     exists(root: str) -> bool
#     clear(root: str) -> None
#
#   Notes:
#     - All paths are VSD keys; callers decide the <root> (e.g., "vhw/vqram/tier/1/lane/A").
#     - Integrity covers only the tokenized text (dict changes require full rewrite to be safe).

from typing import Any, Dict, List
import time
from core import utils

def _keys(root: str) -> Dict[str, str]:
    base = str(root).rstrip("/")
    return {
        "text": f"{base}/state_text",
        "dict": f"{base}/state_dict",
        "meta": f"{base}/meta",
        "integrity": f"{base}/integrity",
        "oplog": f"{base}/operation_log",
        "operator_chain.json": f"{base}/operator_chain.json",
        "delta_evolution_vector.json": f"{base}/delta_evolution_vector.json",
        "temporal_trace.sha": f"{base}/temporal_trace.sha"
    }

def exists(root: str) -> bool:
    ks = _keys(root)
    return utils.get(ks["text"], None) is not None

def save_vector(root: str, vec: List[complex], operation_log: List[str] = None) -> Dict[str, Any]:
    """
    Persist a full state-vector under <root>. Always live-mode, always persistent.
    Optionally store a log of operations that produced the state vector.
    """
    utils.confirm_live_mode()
    ks = _keys(root)
    blob = utils.serialize_vector_with_tokens(vec)
    text = str(blob["text"])
    dct = blob.get("dict", {})
    h = utils.sha256_text(text)
    utils.store(ks["text"], text)
    utils.store(ks["dict"], dct)
    utils.store(ks["integrity"], h)
    meta = {"size_chars": len(text), "len": len(vec), "last_write_s": float(time.time())}
    if operation_log is not None:
        meta["operation_log"] = operation_log
        # Provenance objects
        operator_chain = operation_log
        delta_evolution_vector = utils.compute_delta_evolution_vector(vec, operation_log)
        temporal_trace_sha = utils.sha256_text(str(operator_chain))
        meta["operator_chain"] = operator_chain
        meta["delta_evolution_vector"] = delta_evolution_vector
        meta["temporal_trace_sha"] = temporal_trace_sha
        utils.store(ks["operator_chain.json"], operator_chain)
        utils.store(ks["delta_evolution_vector.json"], delta_evolution_vector)
        utils.store(ks["temporal_trace.sha"], temporal_trace_sha)
    utils.store(ks["meta"], meta)
    return {"root": root, "size_chars": len(text), "len": len(vec), "hash": h, "meta": meta}

def load_vector(root: str) -> Dict[str, Any]:
    """
    Load and integrity-check a state-vector; returns {"status": "...", "vector": List[complex], "meta": dict, "operation_log": list}
    """
    ks = _keys(root)
    text = utils.get(ks["text"], None)
    integ = utils.get(ks["integrity"], None)
    meta = utils.get(ks["meta"], {})
    operator_chain = utils.get(ks["operator_chain.json"], None)
    delta_evolution_vector = utils.get(ks["delta_evolution_vector.json"], None)
    temporal_trace_sha = utils.get(ks["temporal_trace.sha"], None)
    expected_sha = meta.get("temporal_trace_sha", None)
    operation_log = meta.get("operation_log", [])
    # Validation
    if not isinstance(text, str) or not isinstance(integ, str):
        raise RuntimeError("Missing state vector or integrity hash; cannot rehydrate.")
    if operator_chain is None or delta_evolution_vector is None or temporal_trace_sha is None:
        raise RuntimeError("Missing provenance objects; cannot rehydrate state vector.")
    if expected_sha is None or temporal_trace_sha != expected_sha:
        raise RuntimeError("Temporal trace hash mismatch; provenance validation failed.")
    if utils.sha256_text(text) != integ:
        raise RuntimeError("State vector integrity hash mismatch; cannot rehydrate.")
    # rehydrate (token dict may be present, but text is already tokenized  detokenize first)
    dct = utils.get(ks["dict"], {})
    if isinstance(dct, dict) and dct:
        from_text = utils.deserialize_vector_with_tokens({"text": text, "dict": dct})
    else:
        from_text = utils.deserialize_vector_with_tokens(text)
    return {"status": "ok", "vector": from_text, "meta": meta, "operation_log": operation_log, "operator_chain": operator_chain, "delta_evolution_vector": delta_evolution_vector, "temporal_trace_sha": temporal_trace_sha}

def slice_vector(root: str, start_char: int, length: int) -> Dict[str, Any]:
    """
    Read a slice of the tokenized text and rehydrate only that region.
    """
    ks = _keys(root)
    text = utils.get(ks["text"], "")
    s = max(0, int(start_char))
    L = max(0, int(length))
    chunk = str(text)[s:s + L]
    vec = utils.deserialize_vector_with_tokens(chunk)
    return {"status": "ok", "vector": vec, "slice": {"start_char": s, "length": L}}

def replace_segment(root: str, start_char: int, text: str) -> Dict[str, Any]:
    """
    Replace a tokenized-text segment in-place and update integrity and meta.
    Callers must ensure the replacement preserves tokenization grammar.
    """
    utils.confirm_live_mode()
    ks = _keys(root)
    cur = utils.get(ks["text"], "")
    s = max(0, int(start_char))
    new_text = str(cur[:s]) + str(text) + str(cur[s + len(text):])
    h = utils.sha256_text(new_text)
    utils.store(ks["text"], new_text)
    utils.store(ks["integrity"], h)
    utils.store(ks["meta"], {"size_chars": len(new_text), "last_write_s": float(time.time())})
    return {"root": root, "size_chars": len(new_text), "hash": h}

def clear(root: str) -> None:
    """
    Remove a state-vector from VSD (rare in live mode; typically we keep historical).
    """
    utils.confirm_live_mode()
    ks = _keys(root)
    utils.delete(ks["text"])
    utils.delete(ks["dict"])
    utils.delete(ks["integrity"])
    utils.delete(ks["meta"])
    utils.delete(ks["operator_chain.json"])
    utils.delete(ks["delta_evolution_vector.json"])
    utils.delete(ks["temporal_trace.sha"])
