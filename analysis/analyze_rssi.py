#!/usr/bin/env python3
"""RSSI post-processing for the Link16-TDMA COTS HIL experiment.

The script intentionally keeps the first analysis pass simple:
- convert RSSI dBm samples to linear power
- normalize receiver-anchor power ratios per TDMA frame
- detect RSSI drop events against configurable baselines
- produce Apollonius ratio candidates for 2-anchor fallback
- emit EKF-ready observation rows for later tracking/tuning
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict, deque
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Analyze TDMA RSSI logs.")
    parser.add_argument(
        "--input",
        default="analysis/sample_log.csv",
        help="Input CSV log path. Defaults to analysis/sample_log.csv.",
    )
    parser.add_argument(
        "--config",
        default="analysis/calibration_config.json",
        help="Calibration JSON path. Defaults to analysis/calibration_config.json.",
    )
    parser.add_argument(
        "--out-dir",
        default="analysis_out",
        help="Directory for generated CSV files. Defaults to analysis_out.",
    )
    return parser.parse_args()


def load_config(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def normalize_id(value: Any) -> str:
    text = str(value).strip()
    if not text:
        return ""
    try:
        if text.lower().startswith("0x"):
            return f"0x{int(text, 16):02x}"
        return f"0x{int(text, 10):02x}"
    except ValueError:
        return text.lower()


def parse_float(value: Any, default: float = math.nan) -> float:
    try:
        return float(str(value).strip())
    except (TypeError, ValueError):
        return default


def parse_frame(value: Any) -> int:
    try:
        return int(str(value).strip(), 0)
    except (TypeError, ValueError):
        return -1


def dbm_to_linear_power(rssi_dbm: float) -> float:
    return math.pow(10.0, rssi_dbm / 10.0)


def moving_average_by_anchor(
    samples: list[dict[str, Any]], window: int
) -> None:
    history: dict[str, deque[float]] = defaultdict(lambda: deque(maxlen=max(1, window)))
    for sample in samples:
        anchor = sample["receiver_id"]
        history[anchor].append(sample["rssi_dbm"])
        sample["rssi_dbm_ma"] = sum(history[anchor]) / len(history[anchor])
        sample["power_linear_ma"] = dbm_to_linear_power(sample["rssi_dbm_ma"])


def read_samples(csv_path: Path, config: dict[str, Any]) -> list[dict[str, Any]]:
    frame_col = config.get("frame_column", "frame_no")
    receiver_col = config.get("receiver_column", "receiver_id")
    transmitter_col = config.get("transmitter_column", "node_id")
    rssi_col = config.get("rssi_column", "rssi_dbm")
    timestamp_col = config.get("timestamp_column", "timestamp")
    default_receiver = normalize_id(config.get("default_receiver_id", "0x21"))
    decode_ok_values = {str(v) for v in config.get("decode_ok_values", ["0"])}
    use_only_decode_ok = bool(config.get("use_only_decode_ok", True))

    samples: list[dict[str, Any]] = []
    with csv_path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        if not reader.fieldnames:
            return samples
        for row_index, row in enumerate(reader, start=2):
            if use_only_decode_ok and "decode" in row:
                if str(row.get("decode", "")).strip() not in decode_ok_values:
                    continue
            frame_no = parse_frame(row.get(frame_col))
            rssi_dbm = parse_float(row.get(rssi_col))
            if frame_no < 0 or math.isnan(rssi_dbm):
                continue
            receiver_id = normalize_id(row.get(receiver_col) or default_receiver)
            transmitter_id = normalize_id(row.get(transmitter_col) or row.get("node_id", ""))
            samples.append(
                {
                    "row_index": row_index,
                    "timestamp": row.get(timestamp_col, ""),
                    "frame_no": frame_no,
                    "receiver_id": receiver_id,
                    "transmitter_id": transmitter_id,
                    "rssi_dbm": rssi_dbm,
                    "power_linear": dbm_to_linear_power(rssi_dbm),
                    "raw": row,
                }
            )
    samples.sort(key=lambda x: (x["frame_no"], x["receiver_id"], x["row_index"]))
    return samples


def frame_anchor_means(samples: list[dict[str, Any]]) -> dict[int, dict[str, dict[str, Any]]]:
    grouped: dict[int, dict[str, list[dict[str, Any]]]] = defaultdict(lambda: defaultdict(list))
    for sample in samples:
        grouped[sample["frame_no"]][sample["receiver_id"]].append(sample)

    means: dict[int, dict[str, dict[str, Any]]] = {}
    for frame_no, by_anchor in grouped.items():
        means[frame_no] = {}
        for anchor, anchor_samples in by_anchor.items():
            rssi_avg = sum(s["rssi_dbm_ma"] for s in anchor_samples) / len(anchor_samples)
            power_avg = sum(s["power_linear_ma"] for s in anchor_samples) / len(anchor_samples)
            means[frame_no][anchor] = {
                "timestamp": anchor_samples[0]["timestamp"],
                "rssi_dbm": rssi_avg,
                "power_linear": power_avg,
                "sample_count": len(anchor_samples),
            }
    return means


def write_normalized(
    out_path: Path, frame_means: dict[int, dict[str, dict[str, Any]]]
) -> None:
    with out_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "frame_no",
                "timestamp",
                "receiver_id",
                "rssi_dbm_ma",
                "power_linear",
                "normalized_power_ratio",
                "sample_count",
            ]
        )
        for frame_no in sorted(frame_means):
            anchors = frame_means[frame_no]
            total_power = sum(v["power_linear"] for v in anchors.values())
            for anchor in sorted(anchors):
                values = anchors[anchor]
                ratio = values["power_linear"] / total_power if total_power > 0 else 0.0
                writer.writerow(
                    [
                        frame_no,
                        values["timestamp"],
                        anchor,
                        f"{values['rssi_dbm']:.3f}",
                        f"{values['power_linear']:.12e}",
                        f"{ratio:.9f}",
                        values["sample_count"],
                    ]
                )


def write_drop_events(
    out_path: Path,
    frame_means: dict[int, dict[str, dict[str, Any]]],
    config: dict[str, Any],
) -> None:
    baseline = {
        normalize_id(k): float(v)
        for k, v in config.get("baseline_dbm_by_anchor", {}).items()
    }
    threshold = abs(float(config.get("rssi_drop_threshold_db", -2.4)))
    noise_floor = float(config.get("rssi_noise_floor_dbm", -104.5))
    with out_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "frame_no",
                "timestamp",
                "receiver_id",
                "baseline_dbm",
                "rssi_dbm_ma",
                "drop_db",
                "is_drop_event",
                "above_noise_floor",
            ]
        )
        for frame_no in sorted(frame_means):
            for anchor in sorted(frame_means[frame_no]):
                values = frame_means[frame_no][anchor]
                base = baseline.get(anchor)
                if base is None:
                    continue
                drop_db = values["rssi_dbm"] - base
                is_drop = drop_db <= -threshold
                above_noise = values["rssi_dbm"] > noise_floor
                writer.writerow(
                    [
                        frame_no,
                        values["timestamp"],
                        anchor,
                        f"{base:.3f}",
                        f"{values['rssi_dbm']:.3f}",
                        f"{drop_db:.3f}",
                        int(is_drop),
                        int(above_noise),
                    ]
                )


def apollonius_rows(
    frame_means: dict[int, dict[str, dict[str, Any]]],
    config: dict[str, Any],
) -> list[dict[str, Any]]:
    alpha = float(config.get("path_loss_exponent", 2.0))
    min_ratio = float(config.get("apollonius_min_distance_ratio", 0.2))
    max_ratio = float(config.get("apollonius_max_distance_ratio", 5.0))
    pairs = [
        (normalize_id(a), normalize_id(b))
        for a, b in config.get("apollonius_pairs", [])
    ]
    rows: list[dict[str, Any]] = []
    for frame_no in sorted(frame_means):
        anchors = frame_means[frame_no]
        for a, b in pairs:
            if a not in anchors or b not in anchors:
                continue
            p_a = anchors[a]["power_linear"]
            p_b = anchors[b]["power_linear"]
            if p_a <= 0 or p_b <= 0:
                continue
            power_ratio = p_a / p_b
            distance_ratio = math.pow(p_b / p_a, 1.0 / alpha)
            valid = min_ratio <= distance_ratio <= max_ratio
            rows.append(
                {
                    "frame_no": frame_no,
                    "timestamp": anchors[a]["timestamp"] or anchors[b]["timestamp"],
                    "anchor_a": a,
                    "anchor_b": b,
                    "power_ratio_a_over_b": power_ratio,
                    "distance_ratio_a_over_b": distance_ratio,
                    "valid_ratio": valid,
                }
            )
    return rows


def write_apollonius(out_path: Path, rows: list[dict[str, Any]]) -> None:
    with out_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "frame_no",
                "timestamp",
                "anchor_a",
                "anchor_b",
                "power_ratio_a_over_b",
                "distance_ratio_a_over_b",
                "valid_ratio",
            ]
        )
        for row in rows:
            writer.writerow(
                [
                    row["frame_no"],
                    row["timestamp"],
                    row["anchor_a"],
                    row["anchor_b"],
                    f"{row['power_ratio_a_over_b']:.9f}",
                    f"{row['distance_ratio_a_over_b']:.9f}",
                    int(row["valid_ratio"]),
                ]
            )


def write_ekf_observations(
    out_path: Path,
    frame_means: dict[int, dict[str, dict[str, Any]]],
    apollo: list[dict[str, Any]],
    config: dict[str, Any],
) -> None:
    anchor_ids = [normalize_id(x) for x in config.get("anchor_node_ids", [])]
    baseline = {
        normalize_id(k): float(v)
        for k, v in config.get("baseline_dbm_by_anchor", {}).items()
    }
    threshold = abs(float(config.get("rssi_drop_threshold_db", -2.4)))
    by_frame_apollo: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in apollo:
        by_frame_apollo[row["frame_no"]].append(row)

    with out_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        header = [
            "frame_no",
            "timestamp",
            "valid_anchor_count",
            "drop_event_count",
            "ekf_process_noise",
            "ekf_measurement_noise",
        ]
        header.extend(f"r_{anchor}" for anchor in anchor_ids)
        header.extend(f"delta_db_{anchor}" for anchor in anchor_ids)
        header.append("apollonius_distance_ratios")
        writer.writerow(header)

        for frame_no in sorted(frame_means):
            anchors = frame_means[frame_no]
            total_power = sum(v["power_linear"] for v in anchors.values())
            ratios: dict[str, float] = {}
            deltas: dict[str, float] = {}
            drop_count = 0
            timestamp = ""
            for anchor in anchor_ids:
                if anchor in anchors:
                    timestamp = timestamp or anchors[anchor]["timestamp"]
                    ratios[anchor] = (
                        anchors[anchor]["power_linear"] / total_power
                        if total_power > 0
                        else 0.0
                    )
                    if anchor in baseline:
                        delta = anchors[anchor]["rssi_dbm"] - baseline[anchor]
                        deltas[anchor] = delta
                        if delta <= -threshold:
                            drop_count += 1
                    else:
                        deltas[anchor] = math.nan
                else:
                    ratios[anchor] = math.nan
                    deltas[anchor] = math.nan
            ratio_text = ";".join(
                f"{r['anchor_a']}/{r['anchor_b']}={r['distance_ratio_a_over_b']:.6f}"
                for r in by_frame_apollo.get(frame_no, [])
            )
            row = [
                frame_no,
                timestamp,
                sum(1 for anchor in anchor_ids if anchor in anchors),
                drop_count,
                config.get("ekf_process_noise", 0.08),
                config.get("ekf_measurement_noise", 1.2),
            ]
            row.extend(
                "" if math.isnan(ratios[a]) else f"{ratios[a]:.9f}" for a in anchor_ids
            )
            row.extend(
                "" if math.isnan(deltas[a]) else f"{deltas[a]:.3f}" for a in anchor_ids
            )
            row.append(ratio_text)
            writer.writerow(row)


def write_summary(
    out_path: Path,
    samples: list[dict[str, Any]],
    frame_means: dict[int, dict[str, dict[str, Any]]],
    apollo: list[dict[str, Any]],
    config: dict[str, Any],
) -> None:
    summary = {
        "sample_count": len(samples),
        "frame_count": len(frame_means),
        "anchor_node_ids": [normalize_id(x) for x in config.get("anchor_node_ids", [])],
        "apollonius_pair_count": len(apollo),
        "outputs": [
            "normalized_rssi.csv",
            "drop_events.csv",
            "apollonius_candidates.csv",
            "ekf_observations.csv",
        ],
    }
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
        f.write("\n")


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    input_path = Path(args.input)
    config_path = Path(args.config)
    out_dir = Path(args.out_dir)
    if not input_path.is_absolute():
        input_path = root / input_path
    if not config_path.is_absolute():
        config_path = root / config_path
    if not out_dir.is_absolute():
        out_dir = root / out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    config = load_config(config_path)
    samples = read_samples(input_path, config)
    moving_average_by_anchor(samples, int(config.get("rssi_moving_avg_window", 5)))
    means = frame_anchor_means(samples)
    apollo = apollonius_rows(means, config)

    write_normalized(out_dir / "normalized_rssi.csv", means)
    write_drop_events(out_dir / "drop_events.csv", means, config)
    write_apollonius(out_dir / "apollonius_candidates.csv", apollo)
    write_ekf_observations(out_dir / "ekf_observations.csv", means, apollo, config)
    write_summary(out_dir / "summary.json", samples, means, apollo, config)

    print(f"input={input_path}")
    print(f"config={config_path}")
    print(f"out_dir={out_dir}")
    print(f"samples={len(samples)} frames={len(means)} apollonius_pairs={len(apollo)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
