# Avellaneda-Stoikov Market Maker

A C++ implementation of the Avellaneda-Stoikov optimal market-making model. It simulates a market maker quoting bid/ask prices around a mid-price, adjusting quotes based on inventory risk, volatility, and time remaining in the trading horizon — either against a synthetic random walk or against real historical price data.

## Overview

The Avellaneda-Stoikov model derives optimal bid/ask quotes for a market maker who wants to maximize expected utility of terminal wealth while managing inventory risk. The core ideas:

- **Reservation price**: the price at which the market maker is indifferent to holding their current inventory, skewed away from the mid-price based on inventory, risk aversion, and volatility.
- **Optimal spread**: derived from risk aversion, volatility, time remaining, and the market's order arrival intensity.
- **Inventory skew**: bid and ask reservation prices are shifted asymmetrically so the market maker naturally quotes to reduce excess inventory.

The simulation evolves the mid-price, computes reservation bid/ask prices and spread each timestep, and simulates a multi-level order book on each side of the mid: resting volume at each level depletes from simulated aggressive flow and replenishes toward an equilibrium size. Our own quote joins the back of whichever level its distance from mid falls into, and only fills once that level's simulated volume ahead of it clears — not a full matching engine (no individual order IDs), but fills are driven by the same visible depth rather than an independent hidden draw. Cash, inventory, and mark-to-market P&L are tracked throughout.

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

Opens a window with live price/bid/ask, order-book depth, inventory, and P&L charts, plus pause/resume, restart, and simulation-speed controls. Switch to historical data from inside the window: pick the "Historical CSV" radio button, enter (or edit) the CSV path, and press Restart — or start it directly with `./build/mm_gui --source historical --csv data/aapl.csv`. Model parameters (gamma, sigma, A, k) and order-book parameters (book levels, tick size, base level volume, replenish rate) are also adjustable via sliders and take effect on Restart.

### Web dashboard

```bash
./build/mm_web_runner &
python3 gui_web/server.py
```

Then open `http://localhost:8000`. `mm_web_runner` streams simulation state (including a live order-book depth snapshot) to `gui_web/static/state.json` and `history.json`; `server.py` is a zero-dependency static file server (Python stdlib only) that serves the dashboard, which polls those files and redraws the charts. Run the underlying simulation against real data with `./build/mm_web_runner --source historical --csv data/aapl.csv &` — the dashboard header shows which source and file are active.

The dashboard also has a "Model parameters" panel — source, CSV path, gamma, sigma, A, k, plus book levels, tick size, base level volume, and replenish rate — with an Apply button. Apply `POST`s the new values to `server.py`'s `/control` endpoint, which writes them to `gui_web/control.json`; `mm_web_runner` polls that file and restarts the simulation with the new config once it sees a new value there. No relaunch needed.

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
| `book_levels` | Number of simulated price levels per side of the book |
| `tick_size` | Spacing between book levels. `book_levels * tick_size` should comfortably exceed typical quote distance from mid, or quotes pile into the outermost level |
| `base_level_volume` | Equilibrium resting volume each level reverts toward |
| `replenish_rate` | How fast a depleted level's volume reverts toward `base_level_volume` |

## Status

Real-data-anchored simulation, a native GUI, and a web dashboard are implemented; all three can run against historical data, both GUIs expose gamma/sigma/A/k and the order-book parameters (book levels, tick size, base level volume, replenish rate) as live-adjustable controls, and fills are driven by a simulated multi-level order book (visualized live in both GUIs) instead of an independent queue draw. Not yet done, and worth doing next: a true matching engine with individual resting orders (the current book is still a lightweight multi-level liquidity approximation, not a full limit-order book).
