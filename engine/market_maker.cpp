#include "market_maker.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>

double reservation_price(double mid_price, double inventory, double gamma,
                          double sigma, double time_remaining)
{
  return mid_price - inventory * gamma * sigma * sigma * time_remaining;
}

double reservation_bid(double mid_price, double inventory, double gamma,
                        double sigma, double time_remaining)
{
  return mid_price + ((-1.0 - 2.0 * inventory) / 2.0) * gamma * sigma * sigma * time_remaining;
}

double reservation_ask(double mid_price, double inventory, double gamma,
                        double sigma, double time_remaining)
{
  return mid_price + ((1.0 - 2.0 * inventory) / 2.0) * gamma * sigma * sigma * time_remaining;
}

double arrival_rate(double distance, double A, double k)
{
  return A * std::exp(-k * distance);
}

double optimal_spread(double gamma, double sigma, double time_remaining, double k)
{
  return gamma * sigma * sigma * time_remaining + (2.0 / gamma) * std::log(1.0 + gamma / k);
}

double delta_bid(double s, double r_b, double gamma, double k)
{
  return s - r_b + (1.0 / gamma) * std::log(1.0 + gamma / k);
}

double delta_ask(double r_a, double s, double gamma, double k)
{
  return r_a - s + (1.0 / gamma) * std::log(1.0 + gamma / k);
}

PriceFeed::PriceFeed(const std::string &csv_path, double dt, std::mt19937 &rng)
{
  std::ifstream file(csv_path);
  if (!file.is_open())
  {
    throw std::runtime_error("could not open price CSV: " + csv_path);
  }

  std::vector<double> closes;
  std::string line;
  bool first_line = true;
  while (std::getline(file, line))
  {
    if (line.empty())
    {
      continue;
    }
    std::stringstream ss(line);
    std::string date_field, price_field;
    if (!std::getline(ss, date_field, ','))
    {
      continue;
    }
    if (!std::getline(ss, price_field, ','))
    {
      continue;
    }
    if (first_line)
    {
      first_line = false;
      // Skip a header row such as "date,price".
      char c = price_field.empty() ? '\0' : price_field[0];
      if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-'))
      {
        continue;
      }
    }
    try
    {
      closes.push_back(std::stod(price_field));
    }
    catch (const std::exception &)
    {
      continue;
    }
  }

  if (closes.size() < 2)
  {
    throw std::runtime_error("price CSV needs at least two rows: " + csv_path);
  }

  std::vector<double> log_returns;
  log_returns.reserve(closes.size() - 1);
  for (size_t i = 1; i < closes.size(); ++i)
  {
    log_returns.push_back(std::log(closes[i] / closes[i - 1]));
  }
  double mean = std::accumulate(log_returns.begin(), log_returns.end(), 0.0) / log_returns.size();
  double sq_sum = 0.0;
  for (double r : log_returns)
  {
    sq_sum += (r - mean) * (r - mean);
  }
  double sigma_daily = log_returns.size() > 1
                            ? std::sqrt(sq_sum / (log_returns.size() - 1))
                            : 0.0;
  realized_sigma_ = sigma_daily * std::sqrt(252.0);

  // Upsample the real closes into a finer intraday path via a Brownian
  // bridge in log-price space, anchored exactly to each real close.
  std::normal_distribution<double> normal(0.0, 1.0);
  const int steps_per_bar = std::max(1, static_cast<int>(std::round(1.0 / (closes.size() * dt))));

  path_.push_back(closes[0]);
  for (size_t i = 0; i + 1 < closes.size(); ++i)
  {
    double log_p0 = std::log(closes[i]);
    double log_p1 = std::log(closes[i + 1]);
    for (int j = 1; j <= steps_per_bar; ++j)
    {
      double u = static_cast<double>(j) / steps_per_bar;
      double mean_log = log_p0 + u * (log_p1 - log_p0);
      if (j == steps_per_bar)
      {
        path_.push_back(closes[i + 1]);
        continue;
      }
      double bridge_var = sigma_daily * sigma_daily * u * (1.0 - u);
      double noise = std::sqrt(std::max(bridge_var, 0.0)) * normal(rng);
      path_.push_back(std::exp(mean_log + noise));
    }
  }
}

double PriceFeed::next()
{
  if (path_.empty())
  {
    return 0.0;
  }
  double value = path_[std::min(cursor_, path_.size() - 1)];
  if (cursor_ < path_.size() - 1)
  {
    ++cursor_;
  }
  return value;
}

MarketMakerSim::MarketMakerSim(const SimConfig &config)
    : config_(config),
      rng_(std::random_device{}()),
      mid_price_(config.mid_price),
      inventory_(config.inventory),
      cash_(config.cash),
      time_remaining_(config.time_remaining),
      bid_volumes_(config.book_levels, config.base_level_volume),
      ask_volumes_(config.book_levels, config.base_level_volume)
{
  initial_wealth_ = cash_ + inventory_ * mid_price_;

  if (config_.source == PriceSource::Historical)
  {
    feed_ = std::make_unique<PriceFeed>(config_.historical_csv, config_.dt, rng_);
    if (!feed_->empty())
    {
      // Anchor the sim's starting mid-price to the first real price in the
      // series instead of the (likely unrelated) configured default.
      mid_price_ = feed_->next();
      initial_wealth_ = cash_ + inventory_ * mid_price_;
    }
  }
}

double MarketMakerSim::next_mid_price()
{
  if (config_.source == PriceSource::Historical && feed_ && !feed_->empty())
  {
    return feed_->next();
  }

  std::normal_distribution<double> normal(0.0, 1.0);
  double z = normal(rng_);
  return mid_price_ + config_.sigma * std::sqrt(config_.dt) * z;
}

bool MarketMakerSim::update_book_side(std::vector<double> &volumes, int &level_idx,
                                       double &queue_ahead, double distance)
{
  std::exponential_distribution<double> trade_size(1.0);
  int new_level_idx = std::clamp(
      static_cast<int>(std::round(distance / config_.tick_size)) - 1,
      0, config_.book_levels - 1);

  double consumed_at_our_level = 0.0;

  for (int i = 0; i < config_.book_levels; ++i)
  {
    double level_distance = (i + 1) * config_.tick_size;
    double lambda = arrival_rate(level_distance, config_.A, config_.k);
    std::poisson_distribution<int> arrivals(lambda * config_.dt);

    int n = arrivals(rng_);
    for (int j = 0; j < n; ++j)
    {
      double size = trade_size(rng_);
      volumes[i] = std::max(0.0, volumes[i] - size);
      if (i == new_level_idx)
      {
        consumed_at_our_level += size;
      }
    }

    // Mean-revert toward the equilibrium resting volume.
    volumes[i] += (config_.base_level_volume - volumes[i]) * config_.replenish_rate * config_.dt;
    volumes[i] = std::max(0.0, volumes[i]);
  }

  bool filled = false;
  if (new_level_idx != level_idx)
  {
    // Our quote moved to a different level: join the back of its queue.
    level_idx = new_level_idx;
    queue_ahead = volumes[level_idx];
  }
  else
  {
    queue_ahead -= consumed_at_our_level;
    if (queue_ahead <= 0.0)
    {
      filled = true;
      queue_ahead = volumes[level_idx];
    }
  }

  return filled;
}

SimState MarketMakerSim::step()
{
  SimState state;

  if (time_remaining_ <= 0.0)
  {
    state.done = true;
    return state;
  }

  double s = mid_price_;
  double r = reservation_price(mid_price_, inventory_, config_.gamma, config_.sigma, time_remaining_);
  double r_b = reservation_bid(mid_price_, inventory_, config_.gamma, config_.sigma, time_remaining_);
  double r_a = reservation_ask(mid_price_, inventory_, config_.gamma, config_.sigma, time_remaining_);

  double d_b = delta_bid(s, r_b, config_.gamma, config_.k);
  double d_a = delta_ask(r_a, s, config_.gamma, config_.k);

  double p_b = s - d_b;
  double p_a = s + d_a;

  bool bid_filled = update_book_side(bid_volumes_, bid_level_idx_, bid_queue_ahead_, d_b);
  bool ask_filled = update_book_side(ask_volumes_, ask_level_idx_, ask_queue_ahead_, d_a);

  if (bid_filled)
  {
    inventory_ += 1;
    cash_ -= p_b;
  }
  if (ask_filled)
  {
    inventory_ -= 1;
    cash_ += p_a;
  }

  mid_price_ = next_mid_price();
  time_remaining_ -= config_.dt;

  state.time_remaining = time_remaining_;
  state.mid_price = mid_price_;
  state.inventory = inventory_;
  state.cash = cash_;
  state.reservation_price = r;
  state.bid = p_b;
  state.ask = p_a;
  state.bid_queue_ahead = bid_queue_ahead_;
  state.ask_queue_ahead = ask_queue_ahead_;
  state.bid_filled = bid_filled;
  state.ask_filled = ask_filled;
  state.pnl = cash_ + inventory_ * mid_price_ - initial_wealth_;
  state.done = time_remaining_ <= 0.0;

  state.bid_book_prices.reserve(config_.book_levels);
  state.bid_book_volumes.reserve(config_.book_levels);
  state.ask_book_prices.reserve(config_.book_levels);
  state.ask_book_volumes.reserve(config_.book_levels);
  for (int i = 0; i < config_.book_levels; ++i)
  {
    state.bid_book_prices.push_back(s - (i + 1) * config_.tick_size);
    state.bid_book_volumes.push_back(bid_volumes_[i]);
    state.ask_book_prices.push_back(s + (i + 1) * config_.tick_size);
    state.ask_book_volumes.push_back(ask_volumes_[i]);
  }

  return state;
}
