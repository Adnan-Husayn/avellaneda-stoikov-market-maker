#pragma once

#include <memory>
#include <random>
#include <string>
#include <vector>

enum class PriceSource
{
  Synthetic,
  Historical
};

struct SimConfig
{
  double mid_price = 100.0;
  double inventory = 5.0;
  double cash = 0.0;
  double gamma = 0.1;
  double sigma = 2.0;
  double time_remaining = 1.0;
  double dt = 0.005;
  double A = 140.0;
  double k = 1.5;
  PriceSource source = PriceSource::Synthetic;
  std::string historical_csv;
};

struct SimState
{
  double time_remaining = 0.0;
  double mid_price = 0.0;
  double inventory = 0.0;
  double cash = 0.0;
  double reservation_price = 0.0;
  double bid = 0.0;
  double ask = 0.0;
  double bid_queue_ahead = 0.0;
  double ask_queue_ahead = 0.0;
  bool bid_filled = false;
  bool ask_filled = false;
  double pnl = 0.0;
  bool done = false;
};

double reservation_price(double mid_price, double inventory, double gamma,
                          double sigma, double time_remaining);
double reservation_bid(double mid_price, double inventory, double gamma,
                        double sigma, double time_remaining);
double reservation_ask(double mid_price, double inventory, double gamma,
                        double sigma, double time_remaining);
double arrival_rate(double distance, double A, double k);
double optimal_spread(double gamma, double sigma, double time_remaining, double k);
double delta_bid(double s, double r_b, double gamma, double k);
double delta_ask(double r_a, double s, double gamma, double k);

// Historical price series loaded from CSV, upsampled to simulation dt via
// Brownian-bridge interpolation between real closes so the path is anchored
// to actual historical moves rather than being fully synthetic.
class PriceFeed
{
public:
  explicit PriceFeed(const std::string &csv_path, double dt, std::mt19937 &rng);

  bool empty() const { return path_.empty(); }
  double realized_sigma() const { return realized_sigma_; }

  // Returns the next price in the upsampled path, or the last price once
  // the series is exhausted.
  double next();

private:
  std::vector<double> path_;
  size_t cursor_ = 0;
  double realized_sigma_ = 0.0;
};

class MarketMakerSim
{
public:
  explicit MarketMakerSim(const SimConfig &config);

  // Advances the simulation by one dt and returns the resulting state.
  SimState step();

  const SimConfig &config() const { return config_; }
  double initial_wealth() const { return initial_wealth_; }

private:
  SimConfig config_;
  std::mt19937 rng_;
  std::unique_ptr<PriceFeed> feed_;

  double mid_price_;
  double inventory_;
  double cash_;
  double time_remaining_;
  double initial_wealth_;

  // Simulated resting volume ahead of our quote at the current price level;
  // a fill only triggers once this clears to zero, approximating queue
  // position instead of an instant coin-flip fill.
  double bid_queue_ahead_ = 0.0;
  double ask_queue_ahead_ = 0.0;

  double next_mid_price();
};
