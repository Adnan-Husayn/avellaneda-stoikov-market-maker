# Avellaneda-Stoikov Market Maker

A C++ implementation of the Avellaneda-Stoikov optimal market-making model. It simulates a market maker quoting bid/ask prices around a mid-price, adjusting quotes based on inventory risk, volatility, and time remaining in the trading horizon — either against a synthetic random walk or against real historical price data.

## Overview

The Avellaneda-Stoikov model derives optimal bid/ask quotes for a market maker who wants to maximize expected utility of terminal wealth while managing inventory risk. The core ideas:

- **Reservation price**: the price at which the market maker is indifferent to holding their current inventory, skewed away from the mid-price based on inventory, risk aversion, and volatility.
- **Optimal spread**: derived from risk aversion, volatility, time remaining, and the market's order arrival intensity.
- **Inventory skew**: bid and ask reservation prices are shifted asymmetrically so the market maker naturally quotes to reduce excess inventory.

The simulation evolves the mid-price, computes reservation bid/ask prices and spread each timestep, and models fills against a simple queue-position approximation (not just a bare Poisson coin-flip) before tracking cash, inventory, and mark-to-market P&L.

## Project layout

```
engine/       Shared simulation core (SimConfig, SimState, MarketMakerSim) — the only place the AS math lives
cli/          Command-line runner, prints the simulation tick by tick
data/         fetch_data.py — pulls real historical prices; historical CSVs are loaded by the engine
gui_native/   Dear ImGui + ImPlot desktop app with a live chart and play/pause/restart controls
gui_web/      C++ runner that streams state to JSON + a dependency-free web dashboard that polls it
```

All three frontends (CLI, native GUI, web dashboard) call into the same `MarketMakerSim` engine, so the simulation logic isn't duplicated.

## Build

Requires CMake and a C++17 compiler. The native GUI additionally requires GLFW and OpenGL — install with `brew install glfw` (macOS) — and is skipped automatically if they aren't found.

```bash
cmake -B build
cmake --build build
```

This produces `build/mm_cli`, `build/mm_web_runner`, and (if GLFW/OpenGL were found) `build/mm_gui`.

## Run

### CLI

```bash
./build/mm_cli
```

Prints each timestep's remaining time, inventory, reservation price, and quoted bid/ask, then reports final P&L.

### Native GUI

```bash
./build/mm_gui
```

Opens a window with live price/bid/ask, inventory, and P&L charts, plus pause/resume, restart, and simulation-speed controls. Switch to historical data from inside the window: pick the "Historical CSV" radio button, enter (or edit) the CSV path, and press Restart — or start it directly with `./build/mm_gui --source historical --csv data/aapl.csv`.

### Web dashboard

```bash
./build/mm_web_runner &
python3 gui_web/server.py
```

Then open `http://localhost:8000`. `mm_web_runner` streams simulation state to `gui_web/static/state.json` and `history.json`; `server.py` is a zero-dependency static file server (Python stdlib only) that serves the dashboard, which polls those files and redraws the charts. Run the underlying simulation against real data with `./build/mm_web_runner --source historical --csv data/aapl.csv &` — the dashboard header shows which source and file are active.

## Real historical data

By default the mid-price follows a synthetic random walk. To drive the simulation from real prices instead:

```bash
python3 data/fetch_data.py AAPL
./build/mm_cli --source historical --csv data/aapl.csv
./build/mm_gui --source historical --csv data/aapl.csv
./build/mm_web_runner --source historical --csv data/aapl.csv &
```

`fetch_data.py` pulls free daily historical closes from Yahoo Finance's public chart API (no API key required), writes a `date,price` CSV, and prints the realized annualized volatility of the series. The engine upsamples the daily closes into the simulation's finer timestep via a Brownian-bridge interpolation in log-price space, so the intraday path is anchored to real historical moves rather than being fully synthetic. All three frontends — CLI, native GUI, and web runner — accept the same `--source`/`--csv` flags; the native GUI can also switch sources at runtime from its own controls.

## Parameters

Key parameters live in `SimConfig` (`engine/market_maker.hpp`):

| Parameter | Description |
|---|---|
| `mid_price` | Starting mid-price of the asset (ignored in historical mode — the series' first price is used) |
| `inventory` | Starting inventory held by the market maker |
| `gamma` | Risk aversion coefficient |
| `sigma` | Volatility of the mid-price |
| `time_remaining` | Trading horizon (in time units) |
| `dt` | Simulation timestep |
| `A`, `k` | Order arrival intensity parameters |

## Status

Real-data-anchored simulation, a native GUI, and a web dashboard are implemented, and all three can run against historical data. Not yet done, and worth doing next: a full limit-order-book matching engine (the current fill model is a lightweight queue-position approximation, not real order-book depth), and exposing the remaining `SimConfig` parameters (gamma, sigma, A, k, etc.) from the GUIs instead of only via `SimConfig` edits.
