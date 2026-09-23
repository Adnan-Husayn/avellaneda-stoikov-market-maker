#include <iomanip>
#include <iostream>
#include <string>

#include "../engine/market_maker.hpp"

namespace
{
void print_usage(const char *prog)
{
  std::cout << "Usage: " << prog << " [--source synthetic|historical] [--csv path/to/prices.csv]\n"
            << "  --source synthetic   Simulate the mid-price as a random walk (default)\n"
            << "  --source historical  Drive the mid-price from a CSV produced by data/fetch_data.py\n"
            << "  --csv <path>         CSV to use when --source historical is set\n";
}
} // namespace

int main(int argc, char **argv)
{
  SimConfig config;

  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--source" && i + 1 < argc)
    {
      std::string value = argv[++i];
      if (value == "historical")
      {
        config.source = PriceSource::Historical;
      }
      else if (value == "synthetic")
      {
        config.source = PriceSource::Synthetic;
      }
      else
      {
        std::cerr << "Unknown --source value: " << value << "\n";
        return 1;
      }
    }
    else if (arg == "--csv" && i + 1 < argc)
    {
      config.historical_csv = argv[++i];
    }
    else if (arg == "--help" || arg == "-h")
    {
      print_usage(argv[0]);
      return 0;
    }
    else
    {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      return 1;
    }
  }

  if (config.source == PriceSource::Historical && config.historical_csv.empty())
  {
    std::cerr << "--source historical requires --csv <path>\n";
    return 1;
  }

  try
  {
    MarketMakerSim sim(config);
    double initial_wealth = sim.initial_wealth();

    SimState state;
    while (true)
    {
      SimState next = sim.step();
      if (next.done)
      {
        break;
      }
      state = next;

      std::cout
          << "t_remaining: " << state.time_remaining
          << " | inventory: " << state.inventory
          << " | r: " << state.reservation_price
          << " | bid: " << state.bid
          << " | ask: " << state.ask
          << '\n';
    }

    std::cout << "Initial wealth: " << initial_wealth << "\n";
    std::cout << "Final cash: " << state.cash << "\n";
    std::cout << "Final inventory value: " << state.inventory * state.mid_price << "\n";
    std::cout << "Mark-to-market P&L: " << state.pnl << "\n";
  }
  catch (const std::exception &e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
