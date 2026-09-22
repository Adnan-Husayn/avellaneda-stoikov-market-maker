#include <iostream>
#include <cmath>
#include <vector>
#include <iomanip>
#include <random>

double reservation_price(double mid_price, double inventory, double gamma,
                         double sigma, double time_remaining)
{
  return mid_price - inventory * gamma * sigma * sigma * time_remaining;
}

double arrival_rate(double distance, double A, double k)
{
  return A * std::exp(-k * distance);
}

double spread(double gamma, double sigma, double time_remaining, double k)
{
  return gamma * sigma * sigma * time_remaining + (2.0 / gamma * std::log(1.0 + gamma / k));
}

double delta_bid(double s, double r_b, double gamma, double k)
{
  return s - r_b + (1.0 / gamma) * std::log(1.0 + gamma / k);
}

double delta_ask(double r_a, double s, double gamma, double k)
{
  return r_a - s + (1.0 / gamma) * std::log(1.0 + gamma / k);
}

double quoted_spread(double r_a, double r_b, double gamma, double k)
{
  return (r_a - r_b) + (2.0 / gamma) * std::log(1.0 + gamma / k);
}

double reservation_ask(double mid_price, double inventory, double gamma,
                       double sigma, double time_remaining)
{
  return mid_price + ((1.0 - 2.0 * inventory) / 2.0) * gamma * sigma * sigma * time_remaining;
}

double reservation_bid(double mid_price, double inventory, double gamma,
                       double sigma, double time_remaining)
{
  return mid_price + ((-1.0 - 2.0 * inventory) / 2.0) * gamma * sigma * sigma * time_remaining;
}

double lambda_ask(double delta_ask, double A, double k)
{
  return arrival_rate(delta_ask, A, k);
}
double lambda_bid(double delta_bid, double A, double k)
{
  return arrival_rate(delta_bid, A, k);
}

double probability(double lambda, double dt)
{
  return lambda * dt;
}

bool order_filled(double lambda, double dt, std::mt19937 &rng)
{
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  double u = dist(rng);

  return u < lambda * dt;
}

double update_mid_price(double mid_price, double sigma, double dt,
                        std::mt19937 &rng)
{
  std::normal_distribution<double> normal(0.0, 1.0);

  double z = normal(rng);

  return mid_price + sigma * std::sqrt(dt) * z;
}

int main()
{
  double mid_price = 100.0;
  double inventory = 5.0;
  double gamma = 0.1;
  double sigma = 2.0;
  double time_remaining = 1.0;
  double dt = 0.005;
  double cash = 0.0;
  double initial_inventory = inventory;
  double initial_cash = cash;
  double initial_wealth = initial_cash + initial_inventory * mid_price;

  double A = 140.0;
  double k = 1.5;
  std::vector<double> distances = {0.0, 0.1, 0.5, 1.0, 2.0};
  std::mt19937 rng(std::random_device{}());

  while (time_remaining > 0.0)
  {
    double s = mid_price;

    double r = reservation_price(
        mid_price,
        inventory,
        gamma,
        sigma,
        time_remaining);

    double r_b = reservation_bid(
        mid_price,
        inventory,
        gamma,
        sigma,
        time_remaining);

    double r_a = reservation_ask(
        mid_price,
        inventory,
        gamma,
        sigma,
        time_remaining);

    double delta_b = delta_bid(s, r_b, gamma, k);
    double delta_a = delta_ask(r_a, s, gamma, k);

    double p_b = s - delta_b;
    double p_a = s + delta_a;

    double bid_lambda = lambda_bid(delta_b, A, k);
    double ask_lambda = lambda_ask(delta_a, A, k);

    bool bid_filled = order_filled(bid_lambda, dt, rng);
    bool ask_filled = order_filled(ask_lambda, dt, rng);

    if (bid_filled)
    {
      inventory += 1;
      cash -= p_b;
    }

    if (ask_filled)
    {
      inventory -= 1;
      cash += p_a;
    }

    mid_price = update_mid_price(mid_price, sigma, dt, rng);

    std::cout
        << "t_remaining: " << time_remaining
        << " | inventory: " << inventory
        << " | r: " << r
        << " | bid: " << p_b
        << " | ask: " << p_a
        << '\n';

    time_remaining -= dt;
  }

  double pnl = cash + inventory * mid_price - initial_wealth;
  std::cout << "Initial wealth: " << initial_wealth << "\n";
  std::cout << "Final cash: " << cash << "\n";
  std::cout << "Final inventory value: " << inventory * mid_price << "\n";
  std::cout << "Mark-to-market P&L: " << pnl << "\n";

  return 0;
}
