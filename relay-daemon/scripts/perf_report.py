#!/usr/bin/env python3
import argparse
import json
import math
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path


IDLE_STAGE_PREFIXES = (
    "telegram.poll_http",
    "telegram.poll_total",
)


def is_idle_stage(stage):
    if not stage:
        return False
    return any(stage.startswith(prefix) for prefix in IDLE_STAGE_PREFIXES)


def percentile(sorted_vals, p):
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return float(sorted_vals[0])
    rank = (len(sorted_vals) - 1) * p
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return float(sorted_vals[low])
    frac = rank - low
    return float(sorted_vals[low] + (sorted_vals[high] - sorted_vals[low]) * frac)


def safe_mean(values):
    return float(sum(values)) / len(values) if values else 0.0


def load_events(path):
    events = []
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                evt = json.loads(line)
            except json.JSONDecodeError:
                continue
            evt["line_no"] = line_no
            events.append(evt)
    return events


def summarize(events):
    stage_durations = defaultdict(list)
    stage_errors = defaultdict(int)
    stage_counts = defaultdict(int)
    stage_detail = defaultdict(lambda: defaultdict(int))
    request_totals = defaultdict(float)
    request_meta = {}
    total_duration = 0.0

    min_ts = None
    max_ts = None

    for evt in events:
        stage = evt.get("stage", "unknown")
        dur = float(evt.get("duration_ms", 0.0))
        status = evt.get("status", "ok")
        detail = evt.get("detail", "")
        request_id = evt.get("request_id", "")
        chat_id = evt.get("chat_id", "")
        provider = evt.get("provider", "")
        ts_ms = evt.get("ts_ms")

        stage_durations[stage].append(dur)
        stage_counts[stage] += 1
        stage_detail[stage][detail] += 1
        if status != "ok":
            stage_errors[stage] += 1

        if stage == "request.total" and request_id:
            request_totals[request_id] = max(request_totals[request_id], dur)
            request_meta[request_id] = {"chat_id": chat_id, "provider": provider, "status": status}

        total_duration += dur

        if isinstance(ts_ms, (int, float)):
            min_ts = ts_ms if min_ts is None else min(min_ts, ts_ms)
            max_ts = ts_ms if max_ts is None else max(max_ts, ts_ms)

    stage_stats = {}
    total_idle_duration = 0.0
    total_active_duration = 0.0

    for stage, vals in stage_durations.items():
        stage_total = float(sum(vals))
        if is_idle_stage(stage):
            total_idle_duration += stage_total
        else:
            total_active_duration += stage_total

    for stage, vals in stage_durations.items():
        ordered = sorted(vals)
        count = len(ordered)
        total_ms = float(sum(ordered))
        active_base = total_active_duration if total_active_duration else 0.0
        stage_stats[stage] = {
            "count": count,
            "errors": stage_errors[stage],
            "error_rate": (stage_errors[stage] / count) if count else 0.0,
            "total_ms": total_ms,
            "category": "idle_wait" if is_idle_stage(stage) else "active_work",
            "share_of_total": (total_ms / total_duration) if total_duration else 0.0,
            "share_of_active": (total_ms / active_base) if (active_base and not is_idle_stage(stage)) else 0.0,
            "mean_ms": safe_mean(ordered),
            "p50_ms": percentile(ordered, 0.50),
            "p95_ms": percentile(ordered, 0.95),
            "p99_ms": percentile(ordered, 0.99),
            "max_ms": float(ordered[-1]) if ordered else 0.0,
            "common_details": sorted(
                [{"detail": k, "count": v} for k, v in stage_detail[stage].items() if k],
                key=lambda x: x["count"],
                reverse=True
            )[:3],
        }

    slow_requests = sorted(
        [{"request_id": rid, "duration_ms": d, **request_meta.get(rid, {})}
         for rid, d in request_totals.items()],
        key=lambda x: x["duration_ms"],
        reverse=True
    )[:20]

    return {
        "event_count": len(events),
        "totals": {
            "duration_ms": total_duration,
            "idle_wait_ms": total_idle_duration,
            "active_work_ms": total_active_duration,
            "idle_wait_share": (total_idle_duration / total_duration) if total_duration else 0.0,
            "active_work_share": (total_active_duration / total_duration) if total_duration else 0.0,
        },
        "window": {
            "start_ts_ms": min_ts,
            "end_ts_ms": max_ts,
            "start_iso": datetime.fromtimestamp(min_ts / 1000, tz=timezone.utc).isoformat() if min_ts else None,
            "end_iso": datetime.fromtimestamp(max_ts / 1000, tz=timezone.utc).isoformat() if max_ts else None,
        },
        "stages": stage_stats,
        "slow_requests": slow_requests,
    }


def compare_with_baseline(current, baseline):
    regressions = []
    base_stages = baseline.get("stages", {})
    cur_stages = current.get("stages", {})

    for stage, cur in cur_stages.items():
        prev = base_stages.get(stage)
        if not prev:
            continue
        prev_p95 = float(prev.get("p95_ms", 0.0))
        cur_p95 = float(cur.get("p95_ms", 0.0))
        if prev_p95 <= 0:
            continue
        delta = (cur_p95 - prev_p95) / prev_p95
        if delta > 0.10:
            regressions.append({
                "stage": stage,
                "baseline_p95_ms": prev_p95,
                "current_p95_ms": cur_p95,
                "increase_ratio": delta,
            })

    regressions.sort(key=lambda x: x["increase_ratio"], reverse=True)
    return regressions


def render_markdown(report, regressions):
    lines = []
    lines.append("# Performance Report")
    lines.append("")
    lines.append(f"- Events analyzed: **{report['event_count']}**")
    window = report.get("window", {})
    if window.get("start_iso") and window.get("end_iso"):
        lines.append(f"- Window (UTC): **{window['start_iso']}** to **{window['end_iso']}**")
    totals = report.get("totals", {})
    if totals:
        lines.append(
            f"- Time split: **active_work {totals.get('active_work_share', 0) * 100:.1f}%** / "
            f"**idle_wait {totals.get('idle_wait_share', 0) * 100:.1f}%**"
        )
    lines.append("")

    stages = report.get("stages", {})
    ordered = sorted(stages.items(), key=lambda kv: kv[1].get("share_of_total", 0), reverse=True)
    active_ordered = sorted(
        [(s, st) for s, st in stages.items() if st.get("category") == "active_work"],
        key=lambda kv: kv[1].get("share_of_active", 0),
        reverse=True,
    )

    lines.append("## Stage Breakdown")
    lines.append("")
    lines.append("| Stage | Category | Count | Errors | p50 (ms) | p95 (ms) | p99 (ms) | Mean (ms) | Share |")
    lines.append("|---|---|---:|---:|---:|---:|---:|---:|---:|")
    for stage, stats in ordered:
        lines.append(
            f"| `{stage}` | {stats.get('category', 'active_work')} | {stats['count']} | {stats['errors']} | "
            f"{stats['p50_ms']:.1f} | {stats['p95_ms']:.1f} | {stats['p99_ms']:.1f} | "
            f"{stats['mean_ms']:.1f} | {stats['share_of_total'] * 100:.1f}% |"
        )
    lines.append("")

    lines.append("## Active Hotspots (Excludes Idle Wait)")
    lines.append("")
    lines.append("| Stage | p95 (ms) | Mean (ms) | Active Share | Errors |")
    lines.append("|---|---:|---:|---:|---:|")
    if not active_ordered:
        lines.append("| _none_ | 0.0 | 0.0 | 0.0% | 0 |")
    else:
        for stage, stats in active_ordered[:10]:
            lines.append(
                f"| `{stage}` | {stats['p95_ms']:.1f} | {stats['mean_ms']:.1f} | "
                f"{stats.get('share_of_active', 0) * 100:.1f}% | {stats['errors']} |"
            )
    lines.append("")

    lines.append("## Top Slow Requests")
    lines.append("")
    lines.append("| Request ID | Duration (ms) | Provider | Chat | Status |")
    lines.append("|---|---:|---|---|---|")
    for req in report.get("slow_requests", []):
        lines.append(
            f"| `{req.get('request_id', '')}` | {req.get('duration_ms', 0):.1f} | "
            f"{req.get('provider', '')} | {req.get('chat_id', '')} | {req.get('status', '')} |"
        )
    lines.append("")

    lines.append("## Regression Alerts (>10% p95 increase)")
    lines.append("")
    if not regressions:
        lines.append("- None")
    else:
        for r in regressions:
            lines.append(
                f"- `{r['stage']}`: p95 {r['baseline_p95_ms']:.1f}ms -> "
                f"{r['current_p95_ms']:.1f}ms ({r['increase_ratio'] * 100:.1f}% increase)"
            )
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate relay profiler reports from JSONL events.")
    parser.add_argument("--input", required=True, help="Path to profiler JSONL file.")
    parser.add_argument("--output-dir", required=True, help="Directory for report outputs.")
    parser.add_argument("--baseline", default="", help="Optional baseline report.json path.")
    args = parser.parse_args()

    input_path = Path(args.input)
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    if not input_path.exists():
        raise SystemExit(f"Input file not found: {input_path}")

    events = load_events(input_path)
    report = summarize(events)

    baseline_data = None
    baseline_path = Path(args.baseline) if args.baseline else None
    if baseline_path and baseline_path.exists():
        try:
            baseline_data = json.loads(baseline_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            baseline_data = None

    regressions = compare_with_baseline(report, baseline_data) if baseline_data else []
    report["regressions"] = regressions
    report["generated_at_utc"] = datetime.now(timezone.utc).isoformat()

    report_json_path = out_dir / "report.json"
    report_md_path = out_dir / "report.md"

    report_json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    report_md_path.write_text(render_markdown(report, regressions), encoding="utf-8")

    print(f"Wrote {report_json_path}")
    print(f"Wrote {report_md_path}")


if __name__ == "__main__":
    main()
