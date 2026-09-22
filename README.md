# Avellaneda-Stoikov Market Maker

A C++ implementation of the Avellaneda-Stoikov optimal market-making model. It simulates a market maker quoting bid/ask prices around a stochastically evolving mid-price, adjusting quotes based on inventory risk, volatility, and time remaining in the trading horizon.

## Overview

The Avellaneda-Stoikov model derives optimal bid/ask quotes for a market maker who wants to maximize expected utility of terminal wealth while managing inventory risk. The core ideas:

- **Reservation price**: the price at which the market maker is indifferent to holding their current inventory, skewed away from the mid-price based on inventory, risk aversion, and volatility.
- **Optimal spread**: derived from risk aversion, volatility, time remaining, and the market's order arrival intensity.
- **Inventory skew**: bid and ask reservation prices are shifted asymmetrically so the market maker naturally quotes to reduce excess inventory.

## Simulation

The simulation:

1. Evolves the mid-price as a random walk (Brownian motion scaled by volatility).
2. Computes reservation bid/ask prices and the optimal spread each timestep.
3. Models order arrivals as a Poisson process whose intensity decays exponentially with distance from the mid-price.
4. Simulates probabilistic fills against the quoted bid/ask.
5. Tracks cash and inventory, reporting mark-to-market P&L at the end of the run.

## Build

```bash
g++ -O2 -std=c++17 -o marketmaker main.cpp
```

## Run

```bash
./marketmaker
```

Each timestep prints the remaining time, current inventory, reservation price, and quoted bid/ask. At the end of the run, the program reports initial wealth, final cash, final inventory value, and mark-to-market P&L.

## Parameters

Key parameters are set in `main()`:

| Parameter | Description |
|---|---|
| `mid_price` | Starting mid-price of the asset |
| `inventory` | Starting inventory held by the market maker |
| `gamma` | Risk aversion coefficient |
| `sigma` | Volatility of the mid-price |
| `time_remaining` | Trading horizon (in time units) |
| `dt` | Simulation timestep |
| `A`, `k` | Order arrival intensity parameters |

## Status

Initial implementation. More features (configurable parameters, logging/plotting, multi-asset support) may be added over time.
