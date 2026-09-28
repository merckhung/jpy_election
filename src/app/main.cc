// jpy_election: interactive 3D map + live dashboard for Japan's 51st House of
// Representatives general election, 2026-02-08 (Vulkan 3D, Skia 2D).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "src/app/app.h"

#ifdef _WIN32
#include <windows.h>  // after app.h: windows.h #defines DrawText
#endif

namespace {

void Usage() {
  std::printf(
      "使い方 / Usage: jpy_election [flags]\n"
      "  (default: the official final results of the 51st general election, data/election/2026/results.json)\n"
      "  --root=DIR          project directory containing data/ and assets/ (default: .)\n"
      "  --results=FILE      results JSON to watch (default: data/election/2026/results.json)\n"
      "  --simulate          run a synthetic, party-blind, clearly-labelled counting-night DEMO (模擬)\n"
      "  --replay            replay the real final results over a simulated counting night (再現)\n"
      "  --preelection       pre-election view with countdown (data/election/2026/results_preelection.json)\n"
      "  --now=YYYY-MM-DDTHH:MM  override the current time (JST) for the countdown\n"
      "  --no_music          disable the ambient background music\n"
      "  --sim_speed=X       simulation speed: 1 (real time), 2, 4, ... 4096 (default 64)\n"
      "  --sim_clock=HH:MM   start the simulated count at this time (20:00-04:00 JST)\n"
      "  --sim_progress=P    start at fraction P of the night (20:00 + P*8h)\n"
      "  --seed=N            simulation seed\n"
      "  --focus=CODE        start zoomed into a region (prefecture 13, district 13-01, unit 13101)\n"
      "  --mode=N            colouring: 0 SMD leader, 1 PR leading party, 2 margin, 3 turnout,\n"
      "                      4 national review (国民審査)\n"
      "  --lang=L            UI language: ja (default, 日本語), en (English), zh-TW (繁體中文)\n"
      "  --width=W --height=H\n"
      "  --headless          render offscreen (no window); use with --screenshot\n"
      "  --screenshot=FILE   headless: write a PNG and exit\n"
      "  --record=DIR        headless: write a PNG frame sequence (see --record_seconds/--fps)\n"
      "  --record_seconds=S --fps=F\n"
      "  --db=FILE           SQLite database (default <root>/jpy_election.db)\n"
      "  --news              fetch + classify news (data/news/feeds.json)\n"
      "  --news_config=FILE  alternative feeds config\n"
      "  --llm_base_url=URL  OpenAI-compatible endpoint (default https://api.openai.com/v1)\n"
      "  --llm_model=NAME    model name (default gpt-4o-mini); key from $OPENAI_API_KEY\n"
      "  --no_llm            classify with the offline keyword heuristic only\n"
      "  --mock_news         generate labelled mock news (default with --simulate --news)\n"
      "  --mock_news_rate=N  mock items per (simulated) hour (default 12)\n"
      "  --hover=CODE        headless: show a region as hovered\n"
      "  --yaw=DEG --pitch=DEG  initial camera angles\n"
#ifdef _WIN32
      "  --font_dir=DIR      unused on Windows (fonts come from DirectWrite)\n"
#else
      "  --font_dir=DIR      directory scanned for CJK fonts (default /usr/share/fonts)\n"
#endif
      "  --validation        enable VK_LAYER_KHRONOS_validation\n"
      "  --swiftshader       render with the bundled SwiftShader (CPU) Vulkan driver\n"
      "  --help_overlay      start with the controls overlay visible\n"
      "  --show_news         start with the news panel open (key N)\n"
      "  --chart=NAME        start in a secondary chart: trend|seats|margins|parties|grid\n"
      "  --pip=0             hide the picture-in-picture latest-news window (key I)\n"
      "  --chart_tour=S      headless recording: cycle map and charts every S seconds\n");
}

bool Flag(const char* arg, const char* name, std::string* value) {
  const size_t n = std::strlen(name);
  if (std::strncmp(arg, name, n) != 0) return false;
  if (arg[n] == '=') {
    *value = arg + n + 1;
    return true;
  }
  if (arg[n] == '\0') {
    *value = "1";
    return true;
  }
  return false;
}

bool On(const std::string& v) { return v != "0" && v != "false"; }

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);  // usage text and logs are UTF-8
#endif
  jpy::app::AppOptions o;
  // `bazel run` starts in the runfiles tree; default to the workspace.
  if (const char* ws = std::getenv("BUILD_WORKSPACE_DIRECTORY")) o.root = ws;
  for (int i = 1; i < argc; ++i) {
    std::string v;
    const char* a = argv[i];
    if (Flag(a, "--root", &v)) o.root = v;
    else if (Flag(a, "--results", &v)) o.results_path = v;
    else if (Flag(a, "--simulate", &v)) o.simulate = On(v);
    else if (Flag(a, "--replay", &v)) o.replay = On(v);
    else if (Flag(a, "--preelection", &v)) o.preelection = On(v);
    else if (Flag(a, "--now", &v)) {
      int y = 0, mo = 0, d = 0, h = 0, mi = 0;
      if (std::sscanf(v.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) < 3) {
        std::fprintf(stderr, "--now expects YYYY-MM-DDTHH:MM (JST)\n");
        return 2;
      }
      o.now = v;
    }
    else if (Flag(a, "--no_music", &v)) o.music = !On(v);
    else if (Flag(a, "--sim_speed", &v)) o.sim_speed = std::atof(v.c_str());
    else if (Flag(a, "--sim_clock", &v)) {
      int h = 0, mi = 0;
      if (std::sscanf(v.c_str(), "%d:%d", &h, &mi) != 2) {
        std::fprintf(stderr, "--sim_clock expects HH:MM\n");
        return 2;
      }
      // Minutes after the 20:00 close; times after midnight belong to the
      // same counting night.
      if (h < 12) h += 24;
      o.sim_clock = (h - 20) * 60.0 + mi;
      if (o.sim_clock < 0) o.sim_clock = 0;
    }
    else if (Flag(a, "--record_seconds", &v)) o.record_seconds = std::atof(v.c_str());
    else if (Flag(a, "--record", &v)) o.record_dir = v;
    else if (Flag(a, "--fps", &v)) o.record_fps = std::atof(v.c_str());
    else if (Flag(a, "--db", &v)) o.db_path = v;
    else if (Flag(a, "--news_config", &v)) o.news_config = v;
    else if (Flag(a, "--news", &v)) o.news = v != "0";
    else if (Flag(a, "--llm_base_url", &v)) o.llm.base_url = v;
    else if (Flag(a, "--llm_model", &v)) o.llm.model = v;
    else if (Flag(a, "--no_llm", &v)) o.no_llm = v != "0";
    else if (Flag(a, "--mock_news_rate", &v)) o.mock_news_per_hour = std::atof(v.c_str());
    else if (Flag(a, "--mock_news", &v)) o.mock_news = v != "0";
    else if (Flag(a, "--sim_progress", &v)) o.sim_progress = std::atof(v.c_str());
    else if (Flag(a, "--seed", &v)) o.seed = std::strtoull(v.c_str(), nullptr, 10);
    else if (Flag(a, "--focus", &v)) o.focus = v;
    else if (Flag(a, "--hover", &v)) o.hover = v;
    else if (Flag(a, "--mode", &v)) o.mode = std::atoi(v.c_str());
    else if (Flag(a, "--lang", &v)) {
      if (!jpy::ui::ParseLang(v, &o.lang)) {
        std::fprintf(stderr, "unknown --lang=%s (use ja, en or zh-TW)\n", v.c_str());
        return 2;
      }
    }
    else if (Flag(a, "--width", &v)) o.width = std::atoi(v.c_str());
    else if (Flag(a, "--height", &v)) o.height = std::atoi(v.c_str());
    else if (Flag(a, "--headless", &v)) o.headless = v != "0";
    else if (Flag(a, "--screenshot", &v)) o.screenshot = v;
    else if (Flag(a, "--yaw", &v)) o.yaw_deg = static_cast<float>(std::atof(v.c_str()));
    else if (Flag(a, "--pitch", &v)) o.pitch_deg = static_cast<float>(std::atof(v.c_str()));
    else if (Flag(a, "--font_dir", &v)) o.font_dir = v;
    else if (Flag(a, "--validation", &v)) o.validation = v != "0";
    else if (Flag(a, "--swiftshader", &v)) o.swiftshader = On(v);
    else if (Flag(a, "--help_overlay", &v)) o.help = true;
    else if (Flag(a, "--show_news", &v)) o.show_news = true;
    else if (Flag(a, "--pip", &v)) o.pip = On(v);
    else if (Flag(a, "--chart_tour", &v)) o.chart_tour = std::atof(v.c_str());
    else if (Flag(a, "--chart", &v)) {
      const char* names[] = {"map", "trend", "seats", "margins", "parties", "grid"};
      o.chart = -1;
      for (int k = 0; k < 6; ++k) {
        if (v == names[k]) o.chart = k;
      }
      if (o.chart < 0) {
        std::fprintf(stderr, "--chart: map|trend|seats|margins|parties|grid\n");
        return 2;
      }
    }
    else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      Usage();
      return 0;
    } else {
      std::fprintf(stderr, "unknown flag: %s\n", a);
      Usage();
      return 2;
    }
  }
  if (o.simulate + o.replay + o.preelection > 1) {
    std::fprintf(stderr, "--simulate, --replay and --preelection are mutually exclusive\n");
    return 2;
  }
  jpy::app::App app;
  return app.Run(o);
}
