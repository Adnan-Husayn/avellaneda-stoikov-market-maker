#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#endif
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "../engine/market_maker.hpp"

namespace
{
constexpr size_t kHistoryLimit = 2000;

struct History
{
  std::vector<double> t, mid, bid, ask, inventory, pnl;

  void push(const SimState &s, double x)
  {
    t.push_back(x);
    mid.push_back(s.mid_price);
    bid.push_back(s.bid);
    ask.push_back(s.ask);
    inventory.push_back(s.inventory);
    pnl.push_back(s.pnl);
    if (t.size() > kHistoryLimit)
    {
      t.erase(t.begin());
      mid.erase(mid.begin());
      bid.erase(bid.begin());
      ask.erase(ask.begin());
      inventory.erase(inventory.begin());
      pnl.erase(pnl.begin());
    }
  }

  void clear()
  {
    t.clear();
    mid.clear();
    bid.clear();
    ask.clear();
    inventory.clear();
    pnl.clear();
  }
};

void glfw_error_callback(int error, const char *description)
{
  std::cerr << "GLFW error " << error << ": " << description << "\n";
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
      config.source = (value == "historical") ? PriceSource::Historical : PriceSource::Synthetic;
    }
    else if (arg == "--csv" && i + 1 < argc)
    {
      config.historical_csv = argv[++i];
    }
  }

  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit())
  {
    return 1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

  GLFWwindow *window = glfwCreateWindow(1400, 900, "Market Maker", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 150");

  MarketMakerSim sim(config);
  double initial_wealth = sim.initial_wealth();

  History history;
  double tick = 0.0;
  bool running = true;
  bool done = false;
  int steps_per_frame = 4;
  SimState last_state;

  int source_choice = config.source == PriceSource::Historical ? 1 : 0;
  char csv_path_buf[256];
  std::snprintf(csv_path_buf, sizeof(csv_path_buf), "%s",
                config.historical_csv.empty() ? "data/aapl.csv" : config.historical_csv.c_str());
  std::string load_error;

  float gamma_f = static_cast<float>(config.gamma);
  float sigma_f = static_cast<float>(config.sigma);
  float A_f = static_cast<float>(config.A);
  float k_f = static_cast<float>(config.k);

  int book_levels_i = config.book_levels;
  float tick_size_f = static_cast<float>(config.tick_size);
  float base_level_volume_f = static_cast<float>(config.base_level_volume);
  float replenish_rate_f = static_cast<float>(config.replenish_rate);

  while (!glfwWindowShouldClose(window))
  {
    glfwPollEvents();

    if (running && !done)
    {
      for (int i = 0; i < steps_per_frame; ++i)
      {
        SimState s = sim.step();
        if (s.done)
        {
          done = true;
          break;
        }
        last_state = s;
        history.push(s, tick);
        tick += config.dt;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Market Maker", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::Text("Avellaneda-Stoikov market maker simulation");
    ImGui::Separator();

    if (ImGui::Button(running ? "Pause" : "Resume"))
    {
      running = !running;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart"))
    {
      SimConfig new_config = config;
      new_config.source = source_choice == 1 ? PriceSource::Historical : PriceSource::Synthetic;
      new_config.historical_csv = csv_path_buf;
      new_config.gamma = gamma_f;
      new_config.sigma = sigma_f;
      new_config.A = A_f;
      new_config.k = k_f;
      new_config.book_levels = book_levels_i;
      new_config.tick_size = tick_size_f;
      new_config.base_level_volume = base_level_volume_f;
      new_config.replenish_rate = replenish_rate_f;

      try
      {
        MarketMakerSim new_sim(new_config);
        config = new_config;
        sim = std::move(new_sim);
        initial_wealth = sim.initial_wealth();
        history.clear();
        tick = 0.0;
        done = false;
        running = true;
        load_error.clear();
      }
      catch (const std::exception &e)
      {
        load_error = e.what();
      }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::SliderInt("Steps/frame", &steps_per_frame, 1, 40);

    ImGui::Spacing();
    ImGui::RadioButton("Synthetic", &source_choice, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Historical CSV", &source_choice, 1);
    if (source_choice == 1)
    {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(280);
      ImGui::InputText("##csv_path", csv_path_buf, sizeof(csv_path_buf));
      ImGui::SameLine();
      ImGui::TextDisabled("(from data/fetch_data.py <TICKER>)");
    }
    if (!load_error.empty())
    {
      ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "Failed to load: %s", load_error.c_str());
    }

    ImGui::Spacing();
    ImGui::Text("Model parameters (applied on Restart)");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("gamma (risk aversion)", &gamma_f, 0.01f, 2.0f, "%.3f");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("sigma (volatility)", &sigma_f, 0.1f, 10.0f, "%.2f");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("A (arrival intensity)", &A_f, 1.0f, 500.0f, "%.1f");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("k (arrival decay)", &k_f, 0.1f, 10.0f, "%.2f");

    ImGui::Spacing();
    ImGui::Text("Order book (applied on Restart)");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("book levels", &book_levels_i, 2, 30);
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("tick size", &tick_size_f, 0.01f, 1.0f, "%.3f");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("base level volume", &base_level_volume_f, 1.0f, 50.0f, "%.1f");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("replenish rate", &replenish_rate_f, 0.1f, 10.0f, "%.2f");
    ImGui::TextDisabled("book span = book levels x tick size = %.2f (should comfortably exceed the quote distance from mid)",
                         book_levels_i * tick_size_f);

    ImGui::Text("Running: %s",
                config.source == PriceSource::Historical
                    ? ("Historical (" + config.historical_csv + ")").c_str()
                    : "Synthetic");

    ImGui::Spacing();
    ImGui::Columns(5, nullptr, false);
    ImGui::Text("Time left: %.3f", last_state.time_remaining);
    ImGui::NextColumn();
    ImGui::Text("Inventory: %.0f", last_state.inventory);
    ImGui::NextColumn();
    ImGui::Text("Bid/Ask: %.2f / %.2f", last_state.bid, last_state.ask);
    ImGui::NextColumn();
    ImGui::Text("Cash: %.2f", last_state.cash);
    ImGui::NextColumn();
    ImGui::TextColored(last_state.pnl >= 0 ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(0.95f, 0.4f, 0.4f, 1),
                        "P&L: %.2f", last_state.pnl);
    ImGui::Columns(1);
    if (done)
    {
      ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Simulation complete. Press Restart to run again.");
    }

    ImGui::Spacing();

    if (ImPlot::BeginPlot("Price", ImVec2(-1, 180)))
    {
      // History keeps growing every tick; without this the axes auto-fit
      // once on this plot's first (nearly empty) frame and then stay
      // locked to that tiny range instead of tracking the full run.
      ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
      ImPlot::PlotLine("Mid", history.t.data(), history.mid.data(), static_cast<int>(history.t.size()));
      ImPlot::PlotLine("Bid", history.t.data(), history.bid.data(), static_cast<int>(history.t.size()));
      ImPlot::PlotLine("Ask", history.t.data(), history.ask.data(), static_cast<int>(history.t.size()));
      ImPlot::EndPlot();
    }

    if (ImPlot::BeginPlot("Order Book Depth", ImVec2(-1, 130)))
    {
      // The book's price range shifts every tick as mid moves, so keep both
      // axes continuously auto-fit instead of only on this plot's first frame.
      ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
      double bar_width = config.tick_size * 0.8;
      ImPlotSpec bid_spec;
      bid_spec.FillColor = ImVec4(0.3f, 0.9f, 0.4f, 0.6f);
      ImPlot::PlotBars("Bid depth", last_state.bid_book_prices.data(), last_state.bid_book_volumes.data(),
                        static_cast<int>(last_state.bid_book_prices.size()), bar_width, bid_spec);
      ImPlotSpec ask_spec;
      ask_spec.FillColor = ImVec4(0.95f, 0.4f, 0.4f, 0.6f);
      ImPlot::PlotBars("Ask depth", last_state.ask_book_prices.data(), last_state.ask_book_volumes.data(),
                        static_cast<int>(last_state.ask_book_prices.size()), bar_width, ask_spec);
      ImPlot::EndPlot();
    }

    if (ImPlot::BeginPlot("Inventory", ImVec2(-1, 100)))
    {
      ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
      ImPlot::PlotLine("Inventory", history.t.data(), history.inventory.data(), static_cast<int>(history.t.size()));
      ImPlot::EndPlot();
    }

    if (ImPlot::BeginPlot("P&L", ImVec2(-1, 100)))
    {
      ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
      ImPlot::PlotLine("P&L", history.t.data(), history.pnl.data(), static_cast<int>(history.t.size()));
      ImPlot::EndPlot();
    }

    ImGui::End();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
