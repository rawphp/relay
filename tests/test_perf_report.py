import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT / "relay-daemon" / "scripts" / "perf_report.py"

spec = importlib.util.spec_from_file_location("perf_report", SCRIPT_PATH)
perf_report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(perf_report)


class TestPerfReport(unittest.TestCase):
    def test_percentile_basic(self):
        vals = [10, 20, 30, 40]
        self.assertEqual(perf_report.percentile(vals, 0.50), 25.0)
        self.assertGreaterEqual(perf_report.percentile(vals, 0.95), 30.0)

    def test_summarize_and_slow_requests(self):
        events = [
            {
                "ts_ms": 1000,
                "request_id": "r1",
                "chat_id": "c1",
                "provider": "claude",
                "stage": "request.total",
                "duration_ms": 120,
                "status": "ok",
                "detail": "telegram",
            },
            {
                "ts_ms": 1010,
                "request_id": "r1",
                "chat_id": "c1",
                "provider": "claude",
                "stage": "llm.request",
                "duration_ms": 90,
                "status": "ok",
                "detail": "send_streaming",
            },
            {
                "ts_ms": 2000,
                "request_id": "r2",
                "chat_id": "c2",
                "provider": "claude",
                "stage": "request.total",
                "duration_ms": 240,
                "status": "error",
                "detail": "llm_failure",
            },
        ]

        summary = perf_report.summarize(events)
        self.assertEqual(summary["event_count"], 3)
        self.assertIn("request.total", summary["stages"])
        self.assertEqual(summary["stages"]["request.total"]["count"], 2)
        self.assertEqual(summary["stages"]["request.total"]["errors"], 1)
        self.assertEqual(summary["slow_requests"][0]["request_id"], "r2")
        self.assertIn("totals", summary)

    def test_compare_with_baseline_regression_detection(self):
        baseline = {
            "stages": {
                "request.total": {"p95_ms": 100.0},
                "llm.request": {"p95_ms": 200.0},
            }
        }
        current = {
            "stages": {
                "request.total": {"p95_ms": 130.0},  # +30%
                "llm.request": {"p95_ms": 210.0},    # +5%
            }
        }
        regressions = perf_report.compare_with_baseline(current, baseline)
        self.assertEqual(len(regressions), 1)
        self.assertEqual(regressions[0]["stage"], "request.total")

    def test_idle_vs_active_split_and_markdown(self):
        events = [
            {
                "ts_ms": 1000,
                "request_id": "sys-1",
                "chat_id": "",
                "provider": "system",
                "stage": "telegram.poll_total",
                "duration_ms": 30000,
                "status": "ok",
                "detail": "complete",
            },
            {
                "ts_ms": 1010,
                "request_id": "r1",
                "chat_id": "c1",
                "provider": "claude",
                "stage": "llm.request",
                "duration_ms": 1000,
                "status": "ok",
                "detail": "streaming",
            },
            {
                "ts_ms": 1020,
                "request_id": "r1",
                "chat_id": "c1",
                "provider": "claude",
                "stage": "request.total",
                "duration_ms": 1200,
                "status": "ok",
                "detail": "telegram",
            },
        ]
        summary = perf_report.summarize(events)
        self.assertEqual(summary["stages"]["telegram.poll_total"]["category"], "idle_wait")
        self.assertEqual(summary["stages"]["llm.request"]["category"], "active_work")
        self.assertGreater(summary["totals"]["idle_wait_ms"], summary["totals"]["active_work_ms"])
        self.assertGreater(summary["stages"]["llm.request"]["share_of_active"], 0.0)

        md = perf_report.render_markdown(summary, [])
        self.assertIn("Active Hotspots (Excludes Idle Wait)", md)
        self.assertIn("Time split:", md)


if __name__ == "__main__":
    unittest.main()
