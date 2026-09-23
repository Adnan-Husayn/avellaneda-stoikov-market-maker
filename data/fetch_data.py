#!/usr/bin/env python3
"""Download free daily historical close prices for a ticker and write a
date,price CSV that the simulator can load with --source historical.

Usage:
    python3 data/fetch_data.py AAPL
    python3 data/fetch_data.py AAPL --out data/aapl.csv --range 1y
"""

import argparse
import csv
import datetime
import math
import sys
from pathlib import Path

import requests

CHART_URL = "https://query1.finance.yahoo.com/v8/finance/chart/{ticker}"


def fetch_chart(ticker: str, price_range: str):
    response = requests.get(
        CHART_URL.format(ticker=ticker),
        params={"range": price_range, "interval": "1d"},
        headers={"User-Agent": "Mozilla/5.0"},
        timeout=15,
    )
    response.raise_for_status()
    payload = response.json()
    result = payload.get("chart", {}).get("result")
    if not result:
        error = payload.get("chart", {}).get("error")
        raise RuntimeError(f"No data for ticker '{ticker}': {error}")
    return result[0]


def parse_closes(chart_result):
    timestamps = chart_result.get("timestamp", [])
    closes = chart_result["indicators"]["quote"][0]["close"]
    rows = []
    for ts, price in zip(timestamps, closes):
        if price is None:
            continue
        date = datetime.datetime.utcfromtimestamp(ts).strftime("%Y-%m-%d")
        rows.append((date, float(price)))
    return rows


def realized_annual_sigma(rows) -> float:
    closes = [price for _, price in rows]
    if len(closes) < 2:
        return 0.0
    log_returns = [math.log(closes[i] / closes[i - 1]) for i in range(1, len(closes))]
    mean = sum(log_returns) / len(log_returns)
    variance = sum((r - mean) ** 2 for r in log_returns) / max(1, len(log_returns) - 1)
    daily_sigma = math.sqrt(variance)
    return daily_sigma * math.sqrt(252)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ticker", help="Ticker symbol, e.g. AAPL")
    parser.add_argument("--out", help="Output CSV path (default: data/<ticker>.csv)")
    parser.add_argument(
        "--range", default="6mo",
        help="Yahoo Finance range, e.g. 1mo, 6mo, 1y, 5y (default: 6mo)",
    )
    args = parser.parse_args()

    ticker = args.ticker.upper()
    out_path = Path(args.out) if args.out else Path(__file__).parent / f"{ticker.lower()}.csv"

    print(f"Fetching daily history for {ticker} ({args.range}) from Yahoo Finance...")
    chart_result = fetch_chart(ticker, args.range)
    rows = parse_closes(chart_result)

    if len(rows) < 2:
        print("Not enough data returned.", file=sys.stderr)
        sys.exit(1)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["date", "price"])
        writer.writerows(rows)

    sigma = realized_annual_sigma(rows)
    print(f"Wrote {len(rows)} rows to {out_path}")
    print(f"Realized annualized volatility (sigma): {sigma:.4f}")
    print("Run the simulator with, e.g.:")
    print(f"  ./build/mm_cli --source historical --csv {out_path}")


if __name__ == "__main__":
    main()
