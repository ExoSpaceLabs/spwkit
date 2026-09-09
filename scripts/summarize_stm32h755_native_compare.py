#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def fmt(value):
    if isinstance(value, int):
        return str(value)
    if abs(value - round(value)) < 1e-9:
        return str(int(round(value)))
    return f"{value:.1f}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("result_dir")
    args = parser.parse_args()

    root = Path(args.result_dir)
    case_files = sorted((root / "cases").glob("*.json"))
    if not case_files:
        raise SystemExit(f"no case JSON files found under {root / 'cases'}")

    lines = []
    lines.append("SpWKit STM32H755 native differential")
    lines.append("units: Cortex-M7 DWT cycles; delta = SpWKit - native")
    lines.append("")

    for path in case_files:
        data = json.loads(path.read_text())
        floor = data["counter_floor_statistics_cycles"]
        lines.append(
            f"{data['case']}  [{data['start_probe']} -> {data['end_probe']}]  "
            f"floor median={fmt(floor['median'])} p95={fmt(floor['p95'])}"
        )
        lines.append("payload   native_med   spwkit_med   delta_med   delta_%   delta_p95")
        for row in data["rows"]:
            native = row["native_statistics_cycles"]
            spwkit = row["spwkit_statistics_cycles"]
            delta = row["paired_delta_statistics_cycles"]
            percent = row["paired_median_delta_percent_of_native_median"]
            percent_text = "n/a" if percent is None else f"{percent:+.1f}%"
            lines.append(
                f"{row['payload_bytes']:>6}   "
                f"{fmt(native['median']):>10}   "
                f"{fmt(spwkit['median']):>10}   "
                f"{fmt(delta['median']):>9}   "
                f"{percent_text:>7}   "
                f"{fmt(delta['p95']):>9}"
            )
        lines.append("")

    text = "\n".join(lines).rstrip() + "\n"
    print(text, end="")
    (root / "summary.txt").write_text(text)


if __name__ == "__main__":
    main()
