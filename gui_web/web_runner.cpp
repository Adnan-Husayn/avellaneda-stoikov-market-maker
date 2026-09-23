#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../engine/market_maker.hpp"

namespace
{
// Minimal reader for the flat, known-shape JSON objects this program deals
// with (its own state, and the dashboard's control payload) — not a
// general JSON parser.
size_t skip_ws(const std::string &s, size_t pos)
{
  while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
  {
    ++pos;
  }
  return pos;
}

std::optional<double> json_number(const std::string &json, const std::string &key)
{
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos)
  {
    return std::nullopt;
  }
  pos = skip_ws(json, pos + needle.size());
  try
  {
    return std::stod(json.substr(pos));
  }
  catch (const std::exception &)
  {
    return std::nullopt;
  }
}

std::optional<std::string> json_string(const std::string &json, const std::string &key)
{
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos)
  {
    return std::nullopt;
  }
  pos = skip_ws(json, pos + needle.size());
  if (pos >= json.size() || json[pos] != '"')
  {
    return std::nullopt;
  }
  ++pos;
  size_t end = json.find('"', pos);
  if (end == std::string::npos)
  {
    return std::nullopt;
  }
  return json.substr(pos, end - pos);
}

std::string read_file(const std::string &path)
{
  std::ifstream in(path);
  if (!in.is_open())
  {
    return "";
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Applies a control.json payload to `config` in place. Returns false if the
// payload has no new seq (or is missing/invalid), true if it was applied.
bool apply_control(const std::string &control_path, SimConfig &config, long &last_seq)
{
  std::string raw = read_file(control_path);
  if (raw.empty())
  {
    return false;
  }

  auto seq = json_number(raw, "seq");
  if (!seq || static_cast<long>(*seq) == last_seq)
  {
    return false;
  }
  last_seq = static_cast<long>(*seq);

  if (auto v = json_number(raw, "gamma"))
    config.gamma = *v;
  if (auto v = json_number(raw, "sigma"))
    config.sigma = *v;
  if (auto v = json_number(raw, "A"))
    config.A = *v;
  if (auto v = json_number(raw, "k"))
    config.k = *v;
  if (auto v = json_string(raw, "source"))
    config.source = (*v == "historical") ? PriceSource::Historical : PriceSource::Synthetic;
  if (auto v = json_string(raw, "csv"))
    config.historical_csv = *v;
  if (auto v = json_number(raw, "book_levels"))
    config.book_levels = static_cast<int>(*v);
  if (auto v = json_number(raw, "tick_size"))
    config.tick_size = *v;
  if (auto v = json_number(raw, "base_level_volume"))
    config.base_level_volume = *v;
  if (auto v = json_number(raw, "replenish_rate"))
    config.replenish_rate = *v;

  return true;
}

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

std::string json_array(const std::vector<double> &values)
{
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < values.size(); ++i)
  {
    out << values[i];
    if (i + 1 < values.size())
    {
      out << ",";
    }
  }
  out << "]";
  return out.str();
}

std::string state_json(const SimState &state, double initial_wealth, bool running,
                        const SimConfig &config, const std::string &error)
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
      << "\"csv\":\"" << json_escape(config.historical_csv) << "\","
      << "\"gamma\":" << config.gamma << ","
      << "\"sigma\":" << config.sigma << ","
      << "\"A\":" << config.A << ","
      << "\"k\":" << config.k << ","
      << "\"book_levels\":" << config.book_levels << ","
      << "\"tick_size\":" << config.tick_size << ","
      << "\"base_level_volume\":" << config.base_level_volume << ","
      << "\"replenish_rate\":" << config.replenish_rate << ","
      << "\"bid_book_prices\":" << json_array(state.bid_book_prices) << ","
      << "\"bid_book_volumes\":" << json_array(state.bid_book_volumes) << ","
      << "\"ask_book_prices\":" << json_array(state.ask_book_prices) << ","
      << "\"ask_book_volumes\":" << json_array(state.ask_book_volumes) << ","
      << "\"error\":\"" << json_escape(error) << "\""
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
  std::string control_path = "gui_web/control.json";
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
    else if (arg == "--control-path" && i + 1 < argc)
    {
      control_path = argv[++i];
    }
  }

  long last_seq = -1;

  // Outer loop: (re)builds the simulation whenever the dashboard posts new
  // parameters via control.json. Runs forever so the dashboard can keep
  // restarting it without relaunching the process.
  while (true)
  {
    std::string build_error;
    std::unique_ptr<MarketMakerSim> sim;
    try
    {
      sim = std::make_unique<MarketMakerSim>(config);
    }
    catch (const std::exception &e)
    {
      build_error = e.what();
    }

    if (!sim)
    {
      std::cerr << "Error: " << build_error << "\n";
      SimState empty;
      write_atomic(out_dir + "/state.json", state_json(empty, 0.0, false, config, build_error));
      while (!apply_control(control_path, config, last_seq))
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      continue;
    }

    double initial_wealth = sim->initial_wealth();
    std::vector<std::string> history_frames;
    SimState last_state;

    while (true)
    {
      if (apply_control(control_path, config, last_seq))
      {
        break; // rebuild with the new config in the outer loop
      }

      SimState state = sim->step();
      bool running = !state.done;
      if (running)
      {
        last_state = state;
      }

      std::string frame = state_json(running ? state : last_state, initial_wealth, running, config, "");
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

      std::this_thread::sleep_for(std::chrono::milliseconds(running ? 20 : 200));
    }
  }

  return 0;
}
