# jpy_election — 3D Japan Election Map for the 51st House of Representatives General Election (2026)

[日本語](README.md) · [English](README.en.md) · [繁體中文](README.zh-TW.md)

An interactive **3D map of Japan** with a real-time **election dashboard** for the **51st general election of members of the House of Representatives** (第51回衆議院議員総選挙) held on **February 8, 2026**, along with the 27th Supreme Court judicial national review (最高裁判所裁判官国民審査).

Built in C++20 with **Bazel 9 (bzlmod)**, rendered in 3D using **Vulkan**, and drawn in 2D with **Skia**. It includes official Ministry of Internal Affairs and Communications (MIC, 総務省) election results across all 465 seats (289 single-member districts and 176 proportional representation seats across 11 regional blocs), 1,284 total candidacies across 22 parties, 432 portrait photos, and 1,931 municipal and sub-district geometric units.

The default interface language is **Japanese** (`--lang=ja`), with full support for **English** (`--lang=en`) and **Traditional Chinese** (`--lang=zh-TW`).

![Nationwide Final Results](docs/screenshots/01_nation_ja.png)

| | |
|---|---|
| ![Tokyo 1st District: Candidate Cards & Sekihairitsu](docs/screenshots/05_district_tokyo1_ja.png) | ![Counting Simulation (21:30 JST)](docs/screenshots/02_sim_2130_ja.png) |
| ![Pre-election Countdown View](docs/screenshots/04_preelection_ja.png) | ![Results Replay Mode (23:00 JST)](docs/screenshots/03_replay_2300_ja.png) |

### Charts & Analytic Views

| | |
|---|---|
| ![465-Seat Hemicycle Chart (F3)](docs/screenshots/08_chart_seats_ja.png) | ![289-District Grid View (F6)](docs/screenshots/09_chart_grid_ja.png) |
| ![English UI View](docs/screenshots/06_nation_en.png) | ![Traditional Chinese UI View](docs/screenshots/07_nation_zh_tw.png) |

> **About the Simulation Mode**
> The `--simulate` flag runs a synthetic, party-blind election night counting simulation from 20:00 poll close to 04:00. The numbers are randomly generated demo data and are clearly marked with a "模擬データ SIMULATION" badge at all times. To view or replay the official election results, launch without `--simulate` or use `--replay`.

---

## Features

- **3D Japan Map:**
  - Fast 3D rendering with Vulkan and Skia (4× MSAA, atmospheric fog, sea reference grid).
  - Smooth zoom and drill-down across all 47 prefectures, 289 single-member districts (SMDs), and 1,931 municipalities and split-district units.
  - Insets for Okinawa Prefecture and the Ogasawara Islands.
- **Full Japanese Electoral System Implementation:**
  - Parallel voting system: 289 Single-Member Districts (小選挙区) + 176 Proportional Representation (PR, 比例代表) seats across 11 regional blocs.
  - Dual-candidacy (重複立候補) support with automated *sekihairitsu* (惜敗率, margin of loss ratio) calculation and proportional revival (比例復活) detection.
  - D'Hondt method (ドント方式) allocation per PR bloc with statutory vote threshold forfeitures (valid votes < 10%) and party list seat reallocations.
- **Supreme Court Judicial Review:**
  - National and prefecture-level retention results for the 27th Supreme Court national review (Justices Junichi Takasu and Masami Okino).
- **Skia Real-Time Dashboard:**
  - 465-seat hemicycle parliament chart marking both majority (233 seats) and two-thirds supermajority (310 seats).
  - Party tally breakdown (district vs. PR list), PR vote shares, and closest margin-of-victory ranking across all 289 districts.
  - Win call stamps (当確 / 確定), zero-call notifications (ゼロ打ち), and PR revival badges.
- **Simulation & Replay Modes:**
  - `--simulate`: Synthetic party-blind counting night from 20:00 to 04:00 with variable speed (×1 to ×4096).
  - `--replay`: Chronological replay of the actual official election night results.
  - `--preelection`: Pre-election countdown view with party candidates and incumbent breakdown.
- **Live News & Sentiment Analysis (PiP):**
  - Live ingestion of Japanese RSS/Atom feeds (NHK, Yahoo! News, Google News, Japan Times, etc.).
  - Keyword and OpenAI-compatible LLM sentiment classification tagged to candidates and districts.
  - Picture-in-Picture (PiP) breaking news ticker.
- **Synthesized Ambient BGM:**
  - Procedural sound generator using Windows `waveOut` (or Linux ALSA). Mute with `V`.
- **Cross-Platform & Native Windows Support:**
  - Native MSVC build (C++20, /utf-8, DirectWrite font management).
  - Automatic fallback to bundled Google SwiftShader CPU Vulkan driver (`--swiftshader`) for headless CI and systems without discrete GPU.
  - Offscreen rendering and high-resolution screenshot export (`--headless --screenshot=FILE`).

---

## Official Seat Totals (51st General Election)

Official Ministry of Internal Affairs and Communications (MIC) results for the 51st House of Representatives election (February 8, 2026):

| Party | District (SMD) | Proportional (PR) | Total Seats | PR Vote Share |
|:---|---:|---:|---:|---:|
| **Liberal Democratic Party (自民 / LDP)** | 248 | 67 | **315** | 36.7% |
| **Centrist Reform Alliance (中道 / CRA)** | 7 | 42 | **49** | 18.2% |
| **Japan Innovation Party (維新 / Ishin)** | 20 | 16 | **36** | 8.6% |
| **Democratic Party for the People (国民 / DPFP)** | 8 | 20 | **28** | 9.7% |
| **Sanseito (参政)** | 0 | 15 | **15** | 7.4% |
| **Team Mirai (みらい)** | 0 | 11 | **11** | 6.7% |
| **Independents (無所属)** | 5 | 0 | **5** | — |
| **Japanese Communist Party (共産 / JCP)** | 0 | 4 | **4** | 4.4% |
| **Reiwa Shinsengumi (れいわ)** | 0 | 1 | **1** | 2.9% |
| **Genzei Nippon - Yukoku Alliance (減ゆ)** | 1 | 0 | **1** | 1.4% |
| **Conservative Party of Japan (保守 / CPJ)** | 0 | 0 | **0** | 2.5% |
| **Social Democratic Party (社民 / SDP)** | 0 | 0 | **0** | 1.3% |
| **Euthanasia System Party (安楽死)** | 0 | 0 | **0** | 0.0% |
| **Total** | **289** | **176** | **465** | **100.0%** |

*For complete candidate-level results across all 289 districts, see [docs/CANDIDATES.md](docs/CANDIDATES.md).*

---

## Controls

![Controls Overlay](docs/screenshots/10_help_overlay_ja.png)

| Input | Action |
|:---|:---|
| **Left-drag** | Pan map |
| **Right-drag / Middle-drag** | Orbit & tilt camera |
| **Scroll wheel / `+` `-`** | Zoom in / out |
| **Left-click** | Drill down (Nation → Prefecture → District → Municipality) |
| **Right-click / `Esc` / `Backspace`** | Go up one region level |
| **Arrow keys / `W` `A` `S` `D`** | Move camera |
| **`Q` / `E`** | Rotate camera |
| **`R` / `F`** | Tilt camera angle |
| **`1`** | District winner party color (default) |
| **`2`** | PR bloc leading party color |
| **`3`** | Margin-of-victory color |
| **`4`** | Turnout color |
| **`5`** | Supreme Court review retention color (press again to switch justice) |
| **`M`** | 3D map view |
| **`F2`** | Vote count trend chart |
| **`F3`** | 465-seat hemicycle chart |
| **`F4`** | Closest races ranking |
| **`F5`** | Party seat & vote breakdown |
| **`F6`** | 289-district regional grid view |
| **`Space`** | Pause / resume simulation |
| **`[` / `]`** | Step simulation back / forward 30 minutes |
| **`Page Down` / `Page Up`** | Decrease / increase simulation speed (×1 to ×4096) |
| **`F7`** | Restart simulation from 20:00 |
| **`I`** | Toggle Picture-in-Picture (PiP) news ticker |
| **`N`** | Toggle news panel |
| **`L`** | Switch language (日本語 → English → 繁體中文) |
| **`P`** | Pin current region as default home view |
| **`Home`** | Reset camera to home view |
| **`V`** | Toggle ambient music synthesizer |
| **`H`** | Toggle controls overlay |

---

## Building & Running

### Requirements
- **OS**: Windows 10/11 (x64) or Linux (Ubuntu 22.04+)
- **Compiler**: MSVC (Visual Studio 2022) or GCC/Clang with C++20 support
- **Build tool**: Bazel 9 (bzlmod)
- **Graphics**: Vulkan 1.2+ capable GPU (or bundled SwiftShader CPU driver)

### Windows Build & Run

Run the included `run.cmd` launcher script:

```cmd
# Launch default view (Japanese UI, official final results)
.\run.cmd

# Launch with English UI
.\run.cmd --lang=en

# Launch with Traditional Chinese UI
.\run.cmd --lang=zh-TW

# Simulated election night count (synthetic data)
.\run.cmd --simulate --sim_clock=21:30

# Official results replay mode
.\run.cmd --replay --sim_clock=23:00

# Pre-election countdown view
.\run.cmd --preelection

# Focus on a specific district (e.g. Tokyo 1st district)
.\run.cmd --focus=13-01

# Force SwiftShader CPU rendering
.\run.cmd --swiftshader

# Headless screenshot generation
.\run.cmd --headless --screenshot=screenshot.png
```

Or invoke Bazel directly:

```cmd
# Build the binary
bazel build //:jpy_election

# Run all 7 test suites
bazel test //tests/...

# Run the application
bazel run //:jpy_election
```

### Linux Build & Run

```sh
sudo apt install build-essential libvulkan1 mesa-vulkan-drivers libglfw3-dev \
                 libcurl4-openssl-dev libasound2-dev glslang-tools git

bazel run //:jpy_election
```

---

## Command-Line Options

```
Usage: jpy_election [flags]

General:
  --root=DIR            Project root directory containing data/ and assets/ (default: .)
  --results=FILE        Results JSON file to watch (default: data/election/2026/results.json)
  --lang=L              UI language: ja (default, 日本語), en (English), zh-TW (繁體中文)
  --focus=CODE          Zoom into region code (e.g. 13 for Tokyo, 13-01 for Tokyo 1st)
  --mode=N              Color mode: 0 SMD winner, 1 PR leader, 2 Margin, 3 Turnout, 4 Review
  --no_music            Disable procedural background music
  --width=W --height=H  Window dimensions (default: 1600x900)

Simulation & Replay:
  --simulate            Run party-blind simulated counting night DEMO
  --replay              Replay official results chronologically
  --preelection         Show pre-election countdown view
  --now=YYYY-MM-DDTHH:MM Override current timestamp (JST) for countdown
  --sim_clock=HH:MM     Start simulation clock (20:00 to 04:00 JST)
  --sim_speed=X         Simulation speed factor (1 to 4096, default: 64)
  --seed=N              Random seed for synthetic counting simulation

Headless & Recording:
  --headless            Render offscreen without creating a window
  --screenshot=FILE     Save offscreen render to PNG and exit
  --record=DIR          Write sequential PNG frames to directory
  --record_seconds=S    Recording duration in seconds
  --fps=F               Recording frames per second
  --chart_tour=S        Cycle secondary charts every S seconds while recording

News & Sentiment:
  --news                Enable news feed retrieval and classification
  --news_config=FILE    Alternative news feed JSON config
  --mock_news           Generate synthetic labeled news (default with --simulate)
  --no_llm              Use offline keyword heuristics instead of LLM
  --llm_base_url=URL    OpenAI-compatible endpoint (default: https://api.openai.com/v1)
  --llm_model=NAME      Model name (default: gpt-4o-mini)
```

---

## Data Sources & Pipeline

The pipeline under `tools/` processes official election files:

1. **`tools/fetch_raw.py`**:
   - Downloads official 51st & 50th general election candidate lists and results from MIC (総務省).
   - Downloads Akira Nishizawa's 2022 revised House of Representatives electoral district shapefiles.
   - Downloads SmartNews SMRI municipal boundary TopoJSON.
2. **`tools/build_data.py`**:
   - Assembles 1,892 Japanese municipalities + Northern Territories into 1,931 electoral units across 289 districts.
   - Clips municipal boundaries split across districts using shapely STRtree spatial indexing.
   - Generates compact `data/map/japan.topo.json` (8,759 arcs).
3. **`tools/build_election.py`**:
   - Parses official MIC spreadsheets and candidate lists into 22 parties, 1,284 candidate profiles with furigana, dual-candidacy, and sekihairitsu rankings.
   - Generates `parties.json`, `candidates.json`, `election.json`, and `results.json`.
4. **`tools/fetch_photos.py`**:
   - Downloads 432 portrait photographs with verified CC-BY / CC0 / Public Domain licenses into `assets/photos/`.

---

## Licenses & Credits

- **Codebase**: Apache-2.0 License
- **Map Data**:
  - Ministry of Land, Infrastructure, Transport and Tourism (MLIT) National Land Numerical Information
  - Akira Nishizawa (Regional and Transport Data Research Institute) Electoral District Shapefiles
  - SmartNews Media Research Institute (SMRI)
- **Candidate Photos**:
  - Full author attributions and license details are documented in [assets/photos/CREDITS.json](assets/photos/CREDITS.json).
