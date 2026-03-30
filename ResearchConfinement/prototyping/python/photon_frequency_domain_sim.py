from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parent
RUN41_SUMMARY = ROOT / "Run41" / "run_041b_summary.json"
NIST_REFERENCE = ROOT / "nist_silicon_reference.json"


@dataclass
class CohortSeed:
    name: str
    freq_norm: float
    amp_norm: float
    volt_norm: float
    curr_norm: float


@dataclass
class PacketState:
    packet_id: int
    cohort: str
    cohort_index: int
    topological_charge: int
    amplitude_drive: float
    frequency_drive: float
    voltage_drive: float
    amperage_drive: float
    retro_gain: float
    spectrum: np.ndarray


@dataclass
class SimulationConfig:
    packet_count: int = 32
    bin_count: int = 128
    steps: int = 48
    recon_samples: int = 256
    equivalent_grid_linear: int = 256
    seed: int = 41
    low_bin_count: int = 12
    kappa_a: float = 0.085
    kappa_f: float = 0.055
    kappa_couple: float = 0.080
    kappa_leak: float = 0.035
    kappa_trap: float = 0.120
    kappa_retro: float = 0.085
    max_amplitude: float = 2.75


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Pure frequency-domain photon confinement simulator."
    )
    parser.add_argument("--packet-count", type=int, default=32)
    parser.add_argument("--bin-count", type=int, default=128)
    parser.add_argument("--steps", type=int, default=48)
    parser.add_argument("--recon-samples", type=int, default=256)
    parser.add_argument("--equivalent-grid-linear", type=int, default=256)
    parser.add_argument("--seed", type=int, default=41)
    parser.add_argument(
        "--output-dir",
        default=str(ROOT / "frequency_domain_runs" / "latest"),
        help="Directory for plots, summaries, and debug views.",
    )
    parser.add_argument(
        "--write-root-samples",
        action="store_true",
        help="Refresh the root photon sample JSON/CSV files used by the current archive loader.",
    )
    return parser.parse_args()


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def load_cohort_seeds() -> list[CohortSeed]:
    summary = load_json(RUN41_SUMMARY)
    best_cases = summary["best_cases"]
    order = ["D_track", "I_accum", "L_smooth"]
    out: list[CohortSeed] = []
    for name in order:
        case = best_cases[name]
        out.append(
            CohortSeed(
                name=name,
                freq_norm=float(case["freq_norm"]),
                amp_norm=float(case["amp_norm"]),
                volt_norm=float(case["volt_norm"]),
                curr_norm=float(case["curr_norm"]),
            )
        )
    return out


def load_nist_reference() -> dict[str, float]:
    raw = load_json(NIST_REFERENCE)
    return {k: float(v) for k, v in raw.items() if isinstance(v, (int, float))}


def gaussian(x: np.ndarray, center: float, width: float) -> np.ndarray:
    return np.exp(-0.5 * ((x - center) / max(width, 1.0e-6)) ** 2)


def gaussian_kernel(radius: int, sigma: float) -> np.ndarray:
    coords = np.arange(-radius, radius + 1, dtype=np.float64)
    kernel = np.exp(-0.5 * (coords / max(sigma, 1.0e-6)) ** 2)
    kernel /= np.sum(kernel)
    return kernel


def blur_along_bins(values: np.ndarray, sigma: float) -> np.ndarray:
    radius = max(1, int(math.ceil(3.0 * sigma)))
    kernel = gaussian_kernel(radius, sigma)
    padded = np.pad(values, ((0, 0), (0, 0), (radius, radius)), mode="edge")
    out = np.empty_like(values)
    for packet_idx in range(values.shape[0]):
        for axis_idx in range(values.shape[1]):
            out[packet_idx, axis_idx] = np.convolve(
                padded[packet_idx, axis_idx], kernel, mode="valid"
            )
    return out


def write_json_with_comment(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        handle.write(f"// {path.name}\n")
        json.dump(payload, handle, indent=2)
        handle.write("\n")


def write_csv_with_comment(path: Path, header: list[str], rows: list[list[Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        handle.write(f"// {path.name}\n")
        writer = csv.writer(handle)
        writer.writerow(header)
        writer.writerows(rows)


def make_packet_spectrum(
    packet_id: int,
    packet_count: int,
    bin_count: int,
    cohort: CohortSeed,
    cohort_index: int,
    nist: dict[str, float],
) -> PacketState:
    bin_idx = np.arange(bin_count, dtype=np.float64)
    bin_norm = bin_idx / max(bin_count - 1, 1)
    packet_theta = (2.0 * math.pi * packet_id) / max(packet_count, 1)
    charge_cycle = (-2, -1, 1, 2)
    topological_charge = charge_cycle[packet_id % len(charge_cycle)]

    base_spacing = 1.0 + 0.35 * cohort.freq_norm + 0.04 * cohort_index
    packet_shift = (packet_id / max(packet_count - 1, 1)) * (bin_count * 0.18)
    center_base = bin_count * (0.12 + 0.58 * cohort.freq_norm) + packet_shift

    centers = np.array(
        [
            center_base,
            center_base + bin_count * 0.06 * math.sin(packet_theta),
            center_base + bin_count * 0.05 * math.cos(packet_theta),
        ],
        dtype=np.float64,
    )
    widths = np.array(
        [
            2.0 + 4.5 * (1.0 - cohort.curr_norm),
            2.4 + 3.8 * (1.0 - cohort.curr_norm),
            2.8 + 3.5 * (1.0 - cohort.curr_norm),
        ],
        dtype=np.float64,
    )

    amplitude_drive = 0.55 + 1.35 * cohort.amp_norm
    frequency_drive = base_spacing + 0.08 * topological_charge
    voltage_drive = 0.30 + 1.50 * cohort.volt_norm
    amperage_drive = 0.25 + 1.30 * cohort.curr_norm
    retro_gain = 0.04 + 0.03 * (packet_id % 5)

    amplitudes = np.zeros((3, bin_count), dtype=np.float64)
    phases = np.zeros((3, bin_count), dtype=np.float64)
    harmonic_offsets = (0.0, 7.0, 13.0)
    harmonic_scales = (1.0, 0.42, 0.24)
    excitation_scale = 0.004 * nist.get("mean_excitation_energy_ev", 173.0)

    for axis_idx in range(3):
        for harmonic_idx, harmonic_scale in enumerate(harmonic_scales):
            harmonic_center = min(
                bin_count - 1,
                centers[axis_idx]
                + harmonic_offsets[harmonic_idx] * (1.0 + 0.25 * cohort.freq_norm),
            )
            amplitudes[axis_idx] += (
                amplitude_drive
                * harmonic_scale
                * gaussian(bin_idx, harmonic_center, widths[axis_idx] + harmonic_idx)
            )
        amplitudes[axis_idx] += excitation_scale * np.exp(-bin_norm * (2.5 + axis_idx))

        helical_phase = topological_charge * packet_theta
        voltage_tilt = voltage_drive * bin_norm * math.pi
        harmonic_twist = frequency_drive * (axis_idx + 1) * bin_norm * 2.0 * math.pi
        axis_offset = axis_idx * (math.pi / 2.0)
        phases[axis_idx] = helical_phase + axis_offset + harmonic_twist + voltage_tilt

    compressed = blur_along_bins(amplitudes[None, :, :], sigma=1.0 + 1.25 * amperage_drive)[0]
    high_bin_falloff = np.exp(-bin_norm * (0.60 + 0.45 * amperage_drive))
    compressed *= high_bin_falloff[None, :]

    spectrum = compressed * np.exp(1j * phases)
    return PacketState(
        packet_id=packet_id,
        cohort=cohort.name,
        cohort_index=cohort_index,
        topological_charge=topological_charge,
        amplitude_drive=amplitude_drive,
        frequency_drive=frequency_drive,
        voltage_drive=voltage_drive,
        amperage_drive=amperage_drive,
        retro_gain=retro_gain,
        spectrum=spectrum.astype(np.complex128),
    )


def build_packet_bank(config: SimulationConfig, nist: dict[str, float]) -> list[PacketState]:
    cohorts = load_cohort_seeds()
    out: list[PacketState] = []
    for packet_id in range(config.packet_count):
        cohort_index = packet_id % len(cohorts)
        out.append(
            make_packet_spectrum(
                packet_id=packet_id,
                packet_count=config.packet_count,
                bin_count=config.bin_count,
                cohort=cohorts[cohort_index],
                cohort_index=cohort_index,
                nist=nist,
            )
        )
    return out


def dominant_axis_frequencies(
    spectrum: np.ndarray, freq_axis: np.ndarray
) -> tuple[np.ndarray, np.ndarray]:
    magnitudes = np.abs(spectrum)
    dominant_bins = np.argmax(magnitudes, axis=-1)
    freqs = np.take_along_axis(freq_axis[None, None, :], dominant_bins[..., None], axis=-1)[
        ..., 0
    ]
    return dominant_bins, freqs


def reconstruct_packet_path(
    spectrum: np.ndarray, recon_samples: int
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    signals = np.fft.ifft(spectrum, n=recon_samples, axis=-1)
    pos = np.real(signals).T
    vel = np.gradient(pos, axis=0)
    acc = np.gradient(vel, axis=0)
    return pos, vel, acc


def mean_phase_lock(lhs: np.ndarray, rhs: np.ndarray) -> float:
    numerator = np.vdot(lhs.reshape(-1), rhs.reshape(-1))
    denom = np.linalg.norm(lhs) * np.linalg.norm(rhs) + 1.0e-9
    return float(abs(numerator) / denom)


def packet_color(packet: PacketState) -> tuple[float, float, float]:
    charge_norm = min(abs(packet.topological_charge) / 2.0, 1.0)
    cohort_bias = packet.cohort_index / 2.0
    return (
        0.25 + 0.60 * charge_norm,
        0.20 + 0.65 * (1.0 - cohort_bias * 0.5),
        0.30 + 0.55 * cohort_bias,
    )


def update_packet_bank(
    packets: list[PacketState],
    config: SimulationConfig,
    freq_axis: np.ndarray,
    step_index: int,
) -> dict[str, Any]:
    packet_count = len(packets)
    spectrum = np.stack([p.spectrum for p in packets], axis=0)
    bin_norm = np.linspace(0.0, 1.0, config.bin_count, dtype=np.float64)

    amplitudes = np.abs(spectrum)
    phases = np.unwrap(np.angle(spectrum), axis=-1)
    dln_a = np.gradient(np.log(amplitudes + 1.0e-9), axis=-1)

    voltage_ramp = np.array([p.voltage_drive for p in packets], dtype=np.float64)[:, None, None]
    frequency_drive = np.array([p.frequency_drive for p in packets], dtype=np.float64)[:, None, None]
    dln_f = np.gradient(
        np.log(freq_axis[None, None, :] * (1.0 + 0.12 * frequency_drive) + 1.0e-6),
        axis=-1,
    )
    voltage_phase = voltage_ramp * bin_norm[None, None, :] * (
        0.50 + 0.35 * math.sin(step_index * 0.15)
    )

    updated = spectrum * np.exp(
        config.kappa_a * dln_a + 1j * (config.kappa_f * dln_f + voltage_phase)
    )

    dominant_bins, dominant_freqs = dominant_axis_frequencies(updated, freq_axis)
    packet_center_freq = np.mean(dominant_freqs, axis=1)
    beat_delta = np.abs(packet_center_freq[:, None] - packet_center_freq[None, :])
    beat_weight = np.exp(-beat_delta / max(freq_axis[-1] * 0.18, 1.0e-6))

    phase_lock = np.zeros((packet_count, packet_count), dtype=np.float64)
    for lhs_idx in range(packet_count):
        for rhs_idx in range(packet_count):
            if lhs_idx == rhs_idx:
                continue
            phase_lock[lhs_idx, rhs_idx] = mean_phase_lock(updated[lhs_idx], updated[rhs_idx])

    coupling_term = np.zeros_like(updated)
    leakage_term = np.zeros_like(updated)
    shared_scores = np.zeros(packet_count, dtype=np.float64)
    for lhs_idx in range(packet_count):
        accumulator = np.zeros_like(updated[lhs_idx])
        leak_accumulator = np.zeros_like(updated[lhs_idx])
        weight_accumulator = 1.0e-9
        for rhs_idx in range(packet_count):
            if lhs_idx == rhs_idx:
                continue
            weight = beat_weight[lhs_idx, rhs_idx] * phase_lock[lhs_idx, rhs_idx]
            weight_accumulator += weight
            accumulator += weight * (updated[rhs_idx] - updated[lhs_idx])
            leak_accumulator += weight * (np.abs(updated[rhs_idx]) - np.abs(updated[lhs_idx]))
        coupling_term[lhs_idx] = accumulator / weight_accumulator
        leakage_term[lhs_idx] = leak_accumulator / weight_accumulator
        shared_scores[lhs_idx] = float(np.max(phase_lock[lhs_idx]))

    low_envelope = np.mean(np.abs(updated[:, :, : config.low_bin_count]), axis=-1, keepdims=True)
    trap_profile = np.exp(-bin_norm * 5.0)[None, None, :]
    high_profile = np.clip(bin_norm - 0.25, 0.0, 1.0)[None, None, :]

    retro_future = np.mean(updated[:, :, -4:], axis=-1, keepdims=True)
    retro_gain = np.array([p.retro_gain for p in packets], dtype=np.float64)[:, None, None]
    retro_term = retro_gain * retro_future * np.exp(-bin_norm * 8.0)[None, None, :]

    updated += config.kappa_couple * coupling_term
    updated *= 1.0 + config.kappa_leak * leakage_term
    updated += config.kappa_retro * retro_term
    updated += (
        config.kappa_trap
        * low_envelope
        * trap_profile
        * np.exp(1j * np.angle(updated))
    )
    updated[:, :, config.low_bin_count :] *= np.clip(
        1.0 - 0.11 * low_envelope * high_profile[:, :, config.low_bin_count :],
        0.65,
        1.0,
    )

    blur_sigma = 0.85 + np.mean([p.amperage_drive for p in packets]) * 0.90
    blurred = blur_along_bins(np.abs(updated), sigma=blur_sigma)
    updated = np.clip(blurred, 0.0, config.max_amplitude) * np.exp(1j * np.angle(updated))

    for packet_idx, packet in enumerate(packets):
        packet.spectrum = updated[packet_idx]

    return {
        "shared_scores": shared_scores,
        "beat_weight": beat_weight,
        "phase_lock": phase_lock,
        "dominant_bins": dominant_bins,
        "dominant_freqs": dominant_freqs,
    }


def build_debug_html(
    path: Path,
    final_paths: dict[int, np.ndarray],
    inspector_rows: list[dict[str, Any]],
    packet_classes: list[dict[str, Any]],
) -> None:
    packet_index = {row["packet_id"]: row for row in inspector_rows}
    class_index = {row["packet_id"]: row for row in packet_classes}

    final_points = []
    for packet_id, points in final_paths.items():
        final_points.append((packet_id, float(points[-1, 0]), float(points[-1, 1])))
    if not final_points:
        return

    xs = [p[1] for p in final_points]
    ys = [p[2] for p in final_points]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    span_x = max(max_x - min_x, 1.0e-6)
    span_y = max(max_y - min_y, 1.0e-6)

    def project_x(value: float) -> float:
        return 80.0 + 840.0 * ((value - min_x) / span_x)

    def project_y(value: float) -> float:
        return 620.0 - 500.0 * ((value - min_y) / span_y)

    parts: list[str] = []
    parts.append("<!doctype html><html><head><meta charset='utf-8'><title>Photon Frequency Domain Debug</title>")
    parts.append(
        "<style>body{font-family:Segoe UI,Arial,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:24px;}"
        "svg{background:#111827;border:1px solid #30363d;border-radius:12px;}"
        "table{border-collapse:collapse;margin-top:20px;width:100%;}"
        "th,td{border:1px solid #30363d;padding:8px;text-align:left;font-size:13px;}"
        "th{background:#161b22;} .note{color:#8b949e;}</style></head><body>"
    )
    parts.append("<h1>Photon Frequency-Domain Debug View</h1>")
    parts.append("<p class='note'>Hover a packet marker to inspect representative bin state and emergent vector curl.</p>")
    parts.append("<svg width='1000' height='700' viewBox='0 0 1000 700'>")
    parts.append("<line x1='80' y1='620' x2='920' y2='620' stroke='#30363d' stroke-width='1' />")
    parts.append("<line x1='80' y1='620' x2='80' y2='120' stroke='#30363d' stroke-width='1' />")

    for packet_id, x, y in final_points:
        debug = packet_index[packet_id]
        packet_class = class_index[packet_id]
        title = (
            f"Packet {packet_id} ({debug['cohort']})\n"
            f"Classification: {packet_class['classification']}\n"
            f"Dominant bin: {debug['dominant_bin']}\n"
            f"Bin 10: mag {debug['bin10_mag']:.3f}, phase {debug['bin10_phase_rad']:.3f} rad "
            f"-> vector curl {debug['vector_curl_deg']:.2f} deg\n"
            f"Shared score: {packet_class['phase_lock_score']:.3f}"
        )
        parts.append(
            f"<circle cx='{project_x(x):.2f}' cy='{project_y(y):.2f}' r='7' fill='{debug['color_hex']}' stroke='#f8fafc' stroke-width='1.0'>"
            f"<title>{title}</title></circle>"
        )

    parts.append("</svg>")
    parts.append("<table><thead><tr><th>Packet</th><th>Cohort</th><th>Top bins</th><th>Bin 10</th><th>Vector curl</th></tr></thead><tbody>")
    for row in inspector_rows:
        top_bins = ", ".join(
            f"{item['bin']}:{item['mag']:.2f}@{item['phase_rad']:.2f}"
            for item in row["top_bins"]
        )
        parts.append(
            "<tr>"
            f"<td>{row['packet_id']}</td>"
            f"<td>{row['cohort']}</td>"
            f"<td>{top_bins}</td>"
            f"<td>mag {row['bin10_mag']:.3f}, phase {row['bin10_phase_rad']:.3f} rad</td>"
            f"<td>{row['vector_curl_deg']:.2f} deg</td>"
            "</tr>"
        )
    parts.append("</tbody></table></body></html>")
    path.write_text("".join(parts), encoding="utf-8")


def save_plots(
    output_dir: Path,
    packets: list[PacketState],
    aggregate_amplitude: np.ndarray,
    final_paths: dict[int, np.ndarray],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    axis_names = ("f_x", "f_y", "f_z")
    colors = ("#ff6b6b", "#ffd166", "#4dabf7")
    for axis_idx, axis in enumerate(axes):
        axis.plot(aggregate_amplitude[axis_idx], color=colors[axis_idx], linewidth=1.6)
        axis.set_ylabel(f"{axis_names[axis_idx]} mag")
        axis.grid(alpha=0.25)
    axes[-1].set_xlabel("Bin index")
    fig.suptitle("Frequency-Domain Photon Packet Spectrum")
    fig.tight_layout()
    fig.savefig(output_dir / "spectrum_plot.png", dpi=180)
    plt.close(fig)

    fig = plt.figure(figsize=(12, 10))
    axis = fig.add_subplot(111, projection="3d")
    for packet in packets:
        path = final_paths[packet.packet_id]
        axis.plot(path[:, 0], path[:, 1], path[:, 2], color=packet_color(packet), linewidth=1.2, alpha=0.9)
    axis.set_title("Reconstructed Packet Vectors From Inverse FFT")
    axis.set_xlabel("x")
    axis.set_ylabel("y")
    axis.set_zlabel("z")
    fig.tight_layout()
    fig.savefig(output_dir / "reconstructed_vectors.png", dpi=180)
    plt.close(fig)


def packet_spin_from_path(vel: np.ndarray, acc: np.ndarray) -> np.ndarray:
    spin = np.mean(np.cross(vel, acc), axis=0)
    norm = np.linalg.norm(spin)
    if norm <= 1.0e-9:
        return np.array([0.0, 0.0, 1.0], dtype=np.float64)
    return spin / norm


def pack_color_hex(rgb: tuple[float, float, float]) -> str:
    clipped = [int(np.clip(channel, 0.0, 1.0) * 255.0) for channel in rgb]
    return "#{:02x}{:02x}{:02x}".format(*clipped)


def write_frequency_outputs(
    output_dir: Path,
    payloads: dict[str, Any],
) -> None:
    json_names = {
        "trajectory_json": "photon_packet_trajectory_sample.json",
        "path_classification_json": "photon_packet_path_classification_sample.json",
        "tensor6d_json": "photon_lattice_tensor6d_sample.json",
        "tensor_gradient_json": "photon_tensor_gradient_sample.json",
        "vector_excitation_json": "photon_vector_excitation_sample.json",
        "tensor_glyph_json": "photon_tensor_gradient_glyph_sample.json",
        "shader_texture_json": "photon_shader_texture_sample.json",
        "audio_waveform_json": "photon_audio_waveform_sample.json",
        "packet_debug_json": "packet_frequency_debug.json",
        "run_summary_json": "frequency_domain_run_summary.json",
        "reconstructed_paths_json": "reconstructed_packet_paths.json",
    }
    for key, filename in json_names.items():
        write_json_with_comment(output_dir / filename, payloads[key])

    write_csv_with_comment(
        output_dir / "photon_packet_trajectory_sample.csv",
        [
            "packet_id",
            "timestep",
            "x",
            "y",
            "z",
            "theta",
            "amplitude",
            "freq_x",
            "freq_y",
            "freq_z",
            "phase_coupling",
            "temporal_inertia",
            "curvature",
            "coherence",
            "flux",
        ],
        payloads["trajectory_csv_rows"],
    )
    write_csv_with_comment(
        output_dir / "photon_packet_path_classification_sample.csv",
        [
            "packet_id",
            "classification",
            "group_id",
            "phase_lock_score",
            "curvature_depth",
            "coherence_score",
        ],
        payloads["path_classification_csv_rows"],
    )


def append_u32_le(out: bytearray, value: int) -> None:
    out.extend(int(value).to_bytes(4, byteorder="little", signed=False))


def append_u64_le(out: bytearray, value: int) -> None:
    out.extend(int(value).to_bytes(8, byteorder="little", signed=False))


def append_blob(out: bytearray, data: bytes) -> None:
    append_u32_le(out, len(data))
    out.extend(data)


def write_virtual_state_drive(path: Path, records: list[dict[str, Any]]) -> None:
    blob = bytearray()
    append_u32_le(blob, 0x31545356)  # 'VST1'
    append_u32_le(blob, 1)
    append_u32_le(blob, len(records))
    for record in records:
        key_bytes = record["key"].encode("utf-8")
        type_bytes = record["type"].encode("utf-8")
        meta_bytes = record["meta"].encode("utf-8")
        value_bytes = record["value"]
        append_blob(blob, key_bytes)
        append_blob(blob, type_bytes)
        append_blob(blob, meta_bytes)
        append_blob(blob, value_bytes)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)


def pack_u16(value: int) -> bytes:
    return int(value).to_bytes(2, byteorder="little", signed=False)


def pack_i16(value: int) -> bytes:
    return int(value).to_bytes(2, byteorder="little", signed=True)


def main() -> None:
    args = parse_args()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    config = SimulationConfig(
        packet_count=args.packet_count,
        bin_count=args.bin_count,
        steps=args.steps,
        recon_samples=args.recon_samples,
        equivalent_grid_linear=args.equivalent_grid_linear,
        seed=args.seed,
    )

    np.random.seed(config.seed)
    nist = load_nist_reference()
    packets = build_packet_bank(config, nist)
    freq_axis = np.linspace(1.0, float(config.bin_count), config.bin_count, dtype=np.float64)

    trajectory_records: list[dict[str, Any]] = []
    trajectory_csv_rows: list[list[Any]] = []
    vector_excitations: list[dict[str, Any]] = []
    tensor6d_cells: list[dict[str, Any]] = []
    tensor_gradient_samples: list[dict[str, Any]] = []
    tensor_glyphs: list[dict[str, Any]] = []
    shader_texture: list[dict[str, Any]] = []
    audio_waveform: list[dict[str, Any]] = []
    inspector_rows: list[dict[str, Any]] = []
    reconstructed_paths: list[dict[str, Any]] = []
    final_paths: dict[int, np.ndarray] = {}
    phase_lock_history = np.zeros((config.packet_count, config.packet_count), dtype=np.float64)
    shared_score_history = np.zeros((config.packet_count,), dtype=np.float64)

    step_dominant_bins = np.zeros((config.steps, config.packet_count, 3), dtype=np.int32)
    step_dominant_freqs = np.zeros((config.steps, config.packet_count, 3), dtype=np.float64)
    theta_history = np.zeros((config.steps, config.packet_count), dtype=np.float64)
    amplitude_history = np.zeros((config.steps, config.packet_count), dtype=np.float64)
    shared_history = np.zeros((config.steps, config.packet_count), dtype=np.float64)
    coherence_history = np.zeros((config.steps, config.packet_count), dtype=np.float64)
    curvature_history = np.zeros((config.steps, config.packet_count), dtype=np.float64)
    flux_history = np.zeros((config.steps, config.packet_count), dtype=np.float64)

    for step_index in range(config.steps):
        update_meta = update_packet_bank(packets, config, freq_axis, step_index)
        phase_lock_history += update_meta["phase_lock"]
        shared_score_history += update_meta["shared_scores"]

        for packet_idx, packet in enumerate(packets):
            pos, vel, acc = reconstruct_packet_path(packet.spectrum, config.recon_samples)
            sample_idx = min(
                config.recon_samples - 1,
                int(round(step_index * (config.recon_samples - 1) / max(config.steps - 1, 1))),
            )
            point = pos[sample_idx]
            velocity = vel[sample_idx]
            accel = acc[sample_idx]
            theta = float(np.mean(np.angle(packet.spectrum)))
            amplitude = float(np.mean(np.abs(packet.spectrum)))
            freq_x, freq_y, freq_z = [float(value) for value in update_meta["dominant_freqs"][packet_idx]]
            phase_coupling = float(update_meta["shared_scores"][packet_idx])
            temporal_inertia = float(np.linalg.norm(accel))
            curvature = float(np.linalg.norm(np.cross(velocity, accel)) / (np.linalg.norm(velocity) ** 3 + 1.0e-9))
            coherence = float(mean_phase_lock(packet.spectrum, np.mean(np.stack([p.spectrum for p in packets], axis=0), axis=0)))
            flux = float(np.linalg.norm(velocity) * amplitude)

            step_dominant_bins[step_index, packet_idx] = update_meta["dominant_bins"][packet_idx]
            step_dominant_freqs[step_index, packet_idx] = update_meta["dominant_freqs"][packet_idx]
            theta_history[step_index, packet_idx] = theta
            amplitude_history[step_index, packet_idx] = amplitude
            shared_history[step_index, packet_idx] = phase_coupling
            coherence_history[step_index, packet_idx] = coherence
            curvature_history[step_index, packet_idx] = curvature
            flux_history[step_index, packet_idx] = flux

            record = {
                "packet_id": packet.packet_id,
                "timestep": step_index,
                "x": float(point[0]),
                "y": float(point[1]),
                "z": float(point[2]),
                "theta": theta,
                "amplitude": amplitude,
                "freq_x": freq_x,
                "freq_y": freq_y,
                "freq_z": freq_z,
                "phase_coupling": phase_coupling,
                "temporal_inertia": temporal_inertia,
                "curvature": curvature,
                "coherence": coherence,
                "flux": flux,
            }
            trajectory_records.append(record)
            trajectory_csv_rows.append(
                [
                    packet.packet_id,
                    step_index,
                    record["x"],
                    record["y"],
                    record["z"],
                    theta,
                    amplitude,
                    freq_x,
                    freq_y,
                    freq_z,
                    phase_coupling,
                    temporal_inertia,
                    curvature,
                    coherence,
                    flux,
                ]
            )

    mean_phase_lock_matrix = phase_lock_history / max(config.steps, 1)
    mean_shared_scores = shared_score_history / max(config.steps, 1)
    mean_curvature_scores = np.mean(curvature_history, axis=0)
    mean_coherence_scores = np.mean(coherence_history, axis=0)

    packet_classes: list[dict[str, Any]] = []
    path_classification_csv_rows: list[list[Any]] = []
    aggregate_amplitude = np.mean(
        np.stack([np.abs(packet.spectrum) for packet in packets], axis=0),
        axis=0,
    )

    phase_norm = (mean_shared_scores - np.min(mean_shared_scores)) / (
        np.ptp(mean_shared_scores) + 1.0e-9
    )
    coherence_norm = (mean_coherence_scores - np.min(mean_coherence_scores)) / (
        np.ptp(mean_coherence_scores) + 1.0e-9
    )
    curvature_norm = (mean_curvature_scores - np.min(mean_curvature_scores)) / (
        np.ptp(mean_curvature_scores) + 1.0e-9
    )
    shared_index = 0.40 * phase_norm + 0.40 * coherence_norm + 0.20 * (1.0 - curvature_norm)
    shared_threshold = float(np.quantile(shared_index, 0.55))

    for packet_idx, packet in enumerate(packets):
        pos, vel, acc = reconstruct_packet_path(packet.spectrum, config.recon_samples)
        final_paths[packet.packet_id] = pos
        spin = packet_spin_from_path(vel, acc)
        final_theta = theta_history[-1, packet_idx]
        final_amp = amplitude_history[-1, packet_idx]
        final_freq = step_dominant_freqs[-1, packet_idx]
        dtheta_dt = float(np.gradient(theta_history[:, packet_idx])[-1])
        d2theta_dt2 = float(np.gradient(np.gradient(theta_history[:, packet_idx]))[-1])
        inertia = float(np.mean(np.linalg.norm(acc, axis=1)))
        curvature_depth = float(np.mean(curvature_history[:, packet_idx]))
        coherence_score = float(np.mean(coherence_history[:, packet_idx]))
        flux_score = float(np.mean(flux_history[:, packet_idx]))
        oam_twist = float(np.dot(spin, np.mean(vel, axis=0)) * (packet.topological_charge / 2.0))
        higgs_inertia = float(max(0.0, inertia * coherence_score - np.std(np.linalg.norm(vel, axis=1))))

        partner_idx = int(np.argmax(mean_phase_lock_matrix[packet_idx]))
        if partner_idx == packet_idx:
            off_diag = mean_phase_lock_matrix[packet_idx].copy()
            off_diag[packet_idx] = -1.0
            partner_idx = int(np.argmax(off_diag))
        phase_lock_score = float(mean_shared_scores[packet_idx])
        classification = "shared" if shared_index[packet_idx] >= shared_threshold else "individual"
        group_id = (
            min(packet.packet_id, packets[partner_idx].packet_id)
            if classification == "shared"
            else packet.packet_id
        )

        packet_class_row = {
            "packet_id": packet.packet_id,
            "classification": classification,
            "group_id": group_id,
            "phase_lock_score": phase_lock_score,
            "curvature_depth": curvature_depth,
            "coherence_score": coherence_score,
        }
        packet_classes.append(packet_class_row)
        path_classification_csv_rows.append(
            [
                packet.packet_id,
                classification,
                group_id,
                phase_lock_score,
                curvature_depth,
                coherence_score,
            ]
        )

        dominant_bins = step_dominant_bins[-1, packet_idx]
        magnitudes = np.abs(packet.spectrum)
        top_bin_indices = np.argsort(magnitudes.mean(axis=0))[-3:][::-1]
        vector_curl_deg = float(np.degrees(np.arctan2(np.linalg.norm(np.mean(np.cross(vel, acc), axis=0)), np.linalg.norm(np.mean(vel, axis=0)) + 1.0e-9)))
        color_rgb = packet_color(packet)
        inspector_rows.append(
            {
                "packet_id": packet.packet_id,
                "cohort": packet.cohort,
                "dominant_bin": int(int(np.mean(dominant_bins))),
                "bin10_mag": float(np.mean(magnitudes[:, min(10, config.bin_count - 1)])),
                "bin10_phase_rad": float(np.mean(np.angle(packet.spectrum[:, min(10, config.bin_count - 1)]))),
                "vector_curl_deg": vector_curl_deg,
                "color_hex": pack_color_hex(color_rgb),
                "top_bins": [
                    {
                        "bin": int(bin_idx),
                        "mag": float(magnitudes.mean(axis=0)[bin_idx]),
                        "phase_rad": float(np.mean(np.angle(packet.spectrum[:, bin_idx]))),
                    }
                    for bin_idx in top_bin_indices
                ],
            }
        )

        reconstructed_paths.append(
            {
                "packet_id": packet.packet_id,
                "cohort": packet.cohort,
                "points": [
                    {"x": float(point[0]), "y": float(point[1]), "z": float(point[2])}
                    for point in pos
                ],
            }
        )

        packet_bin_center = int(np.mean(dominant_bins))
        for sample_idx in np.linspace(0, config.recon_samples - 1, 4, dtype=np.int32):
            sample_point = pos[int(sample_idx)]
            sample_vel = vel[int(sample_idx)]
            vector_excitations.append(
                {
                    "x": float(sample_point[0]),
                    "y": float(sample_point[1]),
                    "z": float(sample_point[2]),
                    "vec_x": float(sample_vel[0]),
                    "vec_y": float(sample_vel[1]),
                    "vec_z": float(sample_vel[2]),
                    "spin": [float(spin[0]), float(spin[1]), float(spin[2])],
                    "oam_twist": oam_twist,
                }
            )

        tensor_matrix = np.array(
            [
                [float(final_freq[0] / config.bin_count), float(np.mean(vel[:, 0])), float(np.mean(acc[:, 0]))],
                [float(np.mean(vel[:, 1])), float(final_freq[1] / config.bin_count), float(np.mean(acc[:, 1]))],
                [float(np.mean(acc[:, 2])), float(np.mean(vel[:, 2])), float(final_freq[2] / config.bin_count)],
            ],
            dtype=np.float64,
        )
        tensor6d_cells.append(
            {
                "x": int(packet.packet_id),
                "y": int(dominant_bins[0]),
                "z": int(dominant_bins[1]),
                "phase_coherence": coherence_score,
                "curvature": curvature_depth,
                "flux": flux_score,
                "inertia": inertia,
                "freq_x": float(final_freq[0]),
                "freq_y": float(final_freq[1]),
                "freq_z": float(final_freq[2]),
                "dtheta_dt": dtheta_dt,
                "d2theta_dt2": d2theta_dt2,
                "oam_twist": oam_twist,
                "spin_vector": [float(spin[0]), float(spin[1]), float(spin[2])],
                "higgs_inertia": higgs_inertia,
            }
        )
        tensor_gradient_samples.append(
            {
                "packet_id": packet.packet_id,
                "bin_center": packet_bin_center,
                "tensor": tensor_matrix.tolist(),
                "phase_gradient": [
                    float(np.mean(np.gradient(np.unwrap(np.angle(packet.spectrum[0]))))),
                    float(np.mean(np.gradient(np.unwrap(np.angle(packet.spectrum[1]))))),
                    float(np.mean(np.gradient(np.unwrap(np.angle(packet.spectrum[2]))))),
                ],
                "amplitude_gradient": [
                    float(np.mean(np.gradient(np.abs(packet.spectrum[0])))),
                    float(np.mean(np.gradient(np.abs(packet.spectrum[1])))),
                    float(np.mean(np.gradient(np.abs(packet.spectrum[2])))),
                ],
                "oam_twist": oam_twist,
                "temporal_inertia": inertia,
            }
        )
        tensor_glyphs.append(
            {
                "x": int(packet.packet_id),
                "y": int(dominant_bins[0]),
                "z": int(dominant_bins[2]),
                "tensor": tensor_matrix.tolist(),
                "color": [
                    float(np.clip(0.20 + 0.70 * coherence_score, 0.0, 1.0)),
                    float(np.clip(0.15 + 0.65 * abs(oam_twist), 0.0, 1.0)),
                    float(np.clip(0.20 + 0.50 * higgs_inertia, 0.0, 1.0)),
                ],
            }
        )
        shader_texture.append(
            {
                "x": int(packet.packet_id),
                "y": int(dominant_bins[0]),
                "z": int(dominant_bins[1]),
                "rgb": [
                    float(np.clip(final_amp / config.max_amplitude, 0.0, 1.0)),
                    float(np.clip((math.sin(final_theta) + 1.0) * 0.5, 0.0, 1.0)),
                    float(np.clip(abs(oam_twist) * 4.0, 0.0, 1.0)),
                ],
            }
        )

    vsd_records = [
        {
            "key": "photon/volume/edge",
            "type": "u64",
            "meta": "expanded lattice edge hint",
            "value": int(config.equivalent_grid_linear).to_bytes(8, byteorder="little", signed=False),
        },
        {
            "key": "photon/volume/scale",
            "type": "f64",
            "meta": "normalized volume scale",
            "value": np.array([1.35], dtype=np.float64).tobytes(),
        },
    ]
    for packet_idx, packet in enumerate(packets):
        freq = step_dominant_freqs[-1, packet_idx]
        coherence = float(np.mean(coherence_history[:, packet_idx]))
        shared = float(np.mean(shared_history[:, packet_idx]))
        curvature = float(np.mean(curvature_history[:, packet_idx]))
        amp = float(amplitude_history[-1, packet_idx])
        theta = float(theta_history[-1, packet_idx])
        payload = bytearray()
        payload.extend(pack_i16(int(round((freq[0] / config.bin_count) * 16384.0))))
        payload.extend(pack_i16(int(round((freq[1] / config.bin_count) * 16384.0))))
        payload.extend(pack_i16(int(round((freq[2] / config.bin_count) * 16384.0))))
        payload.extend(pack_u16(int(round(np.clip(amp / config.max_amplitude, 0.0, 1.0) * 65535.0))))
        payload.extend(pack_u16(int(round(np.clip(coherence, 0.0, 1.0) * 65535.0))))
        payload.extend(pack_u16(int(round(np.clip(shared, 0.0, 1.0) * 65535.0))))
        payload.extend(pack_u16(int(round(np.clip(curvature / 64.0, 0.0, 1.0) * 65535.0))))
        payload.extend(pack_i16(int(round(np.sin(theta) * 16384.0))))
        payload.extend(pack_i16(int(round(np.cos(theta) * 16384.0))))
        meta = (
            f"packet={packet.packet_id};cohort={packet.cohort};charge={packet.topological_charge};"
            f"amp={amp:.6f};theta={theta:.6f};coherence={coherence:.6f};shared={shared:.6f};curvature={curvature:.6f}"
        )
        vsd_records.append(
            {
                "key": f"photon/tensor/{packet.packet_id:04d}",
                "type": "freq_tensor_q16",
                "meta": meta,
                "value": bytes(payload),
            }
        )

    time_axis = np.linspace(0.0, 1.0 / 60.0, config.recon_samples, endpoint=False)
    aggregate_signal = np.mean(
        np.stack([np.fft.ifft(packet.spectrum, n=config.recon_samples, axis=-1) for packet in packets], axis=0),
        axis=0,
    )
    oam_mean = np.mean([entry["oam_twist"] for entry in vector_excitations]) if vector_excitations else 0.0
    for sample_idx, time_s in enumerate(time_axis):
        sample = aggregate_signal[:, sample_idx]
        mix = np.array(
            [
                np.real(sample[0]),
                np.real(sample[1]) + 0.25 * np.imag(sample[2]),
                np.real(sample[2]) + 0.20 * oam_mean,
                0.50 * (np.imag(sample[0]) + np.imag(sample[1])),
            ],
            dtype=np.float64,
        )
        peak = max(np.max(np.abs(mix)), 1.0e-6)
        channels = np.clip(mix / max(peak, 1.0), -1.0, 1.0)
        audio_waveform.append(
            {
                "time": float(time_s),
                "channels": [float(value) for value in channels],
            }
        )

    run_summary = {
        "mode": "frequency_domain_packets_only",
        "description": "Pure spectral photon confinement simulation with inverse-FFT emergent vectors.",
        "packet_count": config.packet_count,
        "bin_count": config.bin_count,
        "steps": config.steps,
        "recon_samples": config.recon_samples,
        "equivalent_grid_linear": config.equivalent_grid_linear,
        "voxel_grid_materialized": False,
        "fft_backend": "numpy.fft",
        "nist_reference": {
            "lattice_constant_m": nist.get("lattice_constant_m"),
            "mean_excitation_energy_ev": nist.get("mean_excitation_energy_ev"),
            "density_g_cm3": nist.get("density_g_cm3"),
        },
        "aggregate_metrics": {
            "mean_shared_score": float(np.mean(mean_shared_scores)),
            "max_phase_lock": float(np.max(mean_phase_lock_matrix)),
            "mean_amplitude": float(np.mean(aggregate_amplitude)),
            "mean_audio_abs": float(np.mean(np.abs([channel for row in audio_waveform for channel in row["channels"]]))),
            "packet_class_counts": {
                "shared": int(sum(1 for row in packet_classes if row["classification"] == "shared")),
                "individual": int(sum(1 for row in packet_classes if row["classification"] == "individual")),
            },
        },
        "artifacts": {
            "spectrum_plot": "spectrum_plot.png",
            "reconstructed_vectors": "reconstructed_vectors.png",
            "debug_view": "debug_view.html",
        },
    }

    payloads = {
        "trajectory_json": trajectory_records,
        "path_classification_json": packet_classes,
        "tensor6d_json": tensor6d_cells,
        "tensor_gradient_json": tensor_gradient_samples,
        "vector_excitation_json": vector_excitations,
        "tensor_glyph_json": tensor_glyphs,
        "shader_texture_json": shader_texture,
        "audio_waveform_json": audio_waveform,
        "packet_debug_json": inspector_rows,
        "run_summary_json": run_summary,
        "reconstructed_paths_json": reconstructed_paths,
        "trajectory_csv_rows": trajectory_csv_rows,
        "path_classification_csv_rows": path_classification_csv_rows,
    }

    write_frequency_outputs(output_dir, payloads)
    save_plots(output_dir, packets, aggregate_amplitude, final_paths)
    build_debug_html(output_dir / "debug_view.html", final_paths, inspector_rows, packet_classes)
    write_virtual_state_drive(output_dir / "photon_volume_expansion.gevsd", vsd_records)

    if args.write_root_samples:
        root_payloads = {
            "trajectory_json": trajectory_records,
            "path_classification_json": packet_classes,
            "tensor6d_json": tensor6d_cells,
            "tensor_gradient_json": tensor_gradient_samples,
            "vector_excitation_json": vector_excitations,
            "tensor_glyph_json": tensor_glyphs,
            "shader_texture_json": shader_texture,
            "audio_waveform_json": audio_waveform,
            "packet_debug_json": inspector_rows,
            "run_summary_json": run_summary,
            "reconstructed_paths_json": reconstructed_paths,
            "trajectory_csv_rows": trajectory_csv_rows,
            "path_classification_csv_rows": path_classification_csv_rows,
        }
        write_frequency_outputs(ROOT, root_payloads)
        write_virtual_state_drive(ROOT / "photon_volume_expansion.gevsd", vsd_records)

    print(f"frequency-domain run complete: {output_dir}")
    print(
        "shared packets="
        f"{sum(1 for row in packet_classes if row['classification'] == 'shared')}/"
        f"{len(packet_classes)}, mean shared score={np.mean(mean_shared_scores):.4f}"
    )
    print(
        "artifacts: spectrum_plot.png, reconstructed_vectors.png, debug_view.html, "
        "photon_packet_trajectory_sample.json"
    )


if __name__ == "__main__":
    main()
