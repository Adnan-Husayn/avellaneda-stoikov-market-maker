#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../engine/market_maker.hpp"

namespace
{
std::string json_escape(const std::string &s)
{
  std::string out;
  out.reserve(s.size());
  for (char c : s)
  {
    if (c == '"' || c == '\\')
    {
      out += '\\';
    }
    out += c;
  }
  return out;
}

std::string state_json(const SimState &state, double initial_wealth, bool running,
                        const SimConfig &config)
{
  std::ostringstream out;
  out << std::boolalpha
      << "{"
      << "\"time_remaining\":" << state.time_remaining << ","
      << "\"mid_price\":" << state.mid_price << ","
      << "\"inventory\":" << state.inventory << ","
      << "\"cash\":" << state.cash << ","
      << "\"reservation_price\":" << state.reservation_price << ","
      << "\"bid\":" << state.bid << ","
      << "\"ask\":" << state.ask << ","
      << "\"bid_filled\":" << state.bid_filled << ","
      << "\"ask_filled\":" << state.ask_filled << ","
      << "\"pnl\":" << state.pnl << ","
      << "\"initial_wealth\":" << initial_wealth << ","
      << "\"running\":" << running << ","
      << "\"source\":\"" << (config.source == PriceSource::Historical ? "historical" : "synthetic") << "\","
      << "\"csv\":\"" << json_escape(config.historical_csv) << "\""
      << "}";
  return out.str();
}

void write_atomic(const std::string &path, const std::string &content)
{
  std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::trunc);
    out << content;
  }
  std::rename(tmp_path.c_str(), path.c_str());
}
} // namespace

int main(int argc, char **argv)
{
  SimConfig config;
  std::string out_dir = "gui_web/static";
  int history_limit = 500;

  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--source" && i + 1 < argc)
    {
      std::string value = argv[++i];
      config.source = (value == "historical") ? PriceSource::Historical : PriceSource::Synthetic;
    }
    else if (arg == "--csv" && i + 1 < argc)
    {
      config.historical_csv = argv[++i];
    }
    else if (arg == "--out-dir" && i + 1 < argc)
    {
      out_dir = argv[++i];
    }
  }

  try
  {
    MarketMakerSim sim(config);
    double initial_wealth = sim.initial_wealth();

    std::vector<std::string> history_frames;
    SimState last_state;

    while (true)
    {
      SimState state = sim.step();
      bool running = !state.done;
      if (running)
      {
        last_state = state;
      }

      std::string frame = state_json(running ? state : last_state, initial_wealth, running, config);
      write_atomic(out_dir + "/state.json", frame);

      if (running)
      {
        history_frames.push_back(frame);
        if (static_cast<int>(history_frames.size()) > history_limit)
        {
          history_frames.erase(history_frames.begin());
        }

        std::ostringstream history;
        history << "[";
        for (size_t i = 0; i < history_frames.size(); ++i)
        {
          history << history_frames[i];
          if (i + 1 < history_frames.size())
          {
            history << ",";
          }
        }
        history << "]";
        write_atomic(out_dir + "/history.json", history.str());
      }
      else
      {
        std::cout << "Simulation complete. Final P&L: " << last_state.pnl << "\n";
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
