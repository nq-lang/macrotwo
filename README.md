# MACRO INTELLIGENCE TERMINAL
**Institutional-grade C++23 Macro-Analytics Desktop Application**
`Internal Use Only — Unclassified`

---

## Three-Section Architecture

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│  FILTER RAIL (220px)   │            CENTRE COLUMN              │  CONTEXT (300px) │
├────────────────────────┤                                       ├──────────────────┤
│  FEED DOMAINS          │  SECTION 1 — MACRO GLOBE (37%)       │  LOCATION        │
│  ● Macro               │  Left: overlay/satellite toggles      │  MACRO SNAPSHOT  │
│  ● Micro               │  Centre: osgEarth WGS84 globe FBO    │  PROVIDERS       │
│  ● Geopolitical        │  Right: market stats + alert stream   │                  │
│  ● Central Bank        ├───────────────────────────────────────┤                  │
│  ● Monetary Policy     │  SECTION 2 — GEO-SCOPED FEED (34%)   │                  │
│  ● Global News         │  Event-driven; 2-second debounce      │                  │
│  ● Military            │  7 stacked narrative feed modules:    │                  │
│                        │  • MACROECONOMIC DEVELOPMENTS         │                  │
│  GLOBE OVERLAYS        │  • MICROECONOMIC DEVELOPMENTS         │                  │
│  ● GMSI Heat           │  • GEOPOLITICAL & GEO-TENSIONS        │                  │
│  ● Admin Boundaries    │  • CENTRAL BANK UPDATES               │                  │
│  ● Satellite           │  • MONETARY POLICY                    │                  │
│  ● Gridlines           │  • GLOBAL / REGIONAL NEWS             │                  │
│                        │  • MILITARY & WAR NEWS                │                  │
│  BOUNDARY MODE         ├───────────────────────────────────────┤                  │
│  DE JURE (UN)          │  SECTION 3 — TOPOGRAPHY (29%)         │                  │
│                        │  3D sector-regime mesh + tables       │                  │
│                        │  LLM rationale (claude-sonnet-4-6)    │                  │
└────────────────────────┴───────────────────────────────────────┴──────────────────┘
│████████████████████████ STATUS BAR: sources · ok · err · UTC clock · sev legend ██│
└────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Section 2 — Event-Driven Feed (New Architecture)

**Globe viewport → 2-second debounce → GeoScopedFetcher → 7 FeedModules**

| Zoom Tier | Resolution | APIs Queried |
|---|---|---|
| 0 — Global  | World     | NewsAPI global, GNews, Finnhub, Polygon |
| 1 — Regional| Continent | NewsAPI q=continent, WorldNewsAPI, GNews |
| 2 — Country | Country   | NewsAPI country filter, FRED releases, GNews |
| 3 — Local   | State/City| WorldNewsAPI, NewsData.io, GNews city search |
| 4 — Satellite| Ground   | **Suppressed** — satellite pane active |

Debounce: globe viewport must be **stable for 2 seconds** before any API call fires.
A cyan/amber progress bar in the Section 2 header shows countdown and fetch progress.

---

## Prerequisites

| Tool | Version |
|---|---|
| CMake | ≥ 3.28 |
| vcpkg | latest |
| Ninja | any |
| GCC | ≥ 13 (C++23) or Clang ≥ 17 or MSVC ≥ 19.38 |
| OpenGL | 4.6 core |
| osgEarth | ≥ 3.3 (optional — stub renderer if absent) |

---

## Quick Start

```bash
# 1. Bootstrap vcpkg
git clone https://github.com/microsoft/vcpkg.git
export VCPKG_ROOT=$(pwd)/vcpkg
./vcpkg/bootstrap-vcpkg.sh

# 2. Clone terminal
git clone <repo-url> macro-terminal && cd macro-terminal

# 3. Configure + build
cmake --preset debug \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build/debug --parallel

# 4. Set API keys
cp .env.example .env
# Edit .env with your keys

# 5. Run
cd build/debug
./bin/macro-terminal

# or headless (for Codespace / CI)
xvfb-run -a ./bin/macro-terminal
```

---

## VS Codespace

1. Open this repository in GitHub Codespaces.
2. The `.devcontainer/setup.sh` runs automatically and installs all dependencies.
3. The terminal builds automatically in `Debug` mode.
4. Run in the integrated terminal:
   ```bash
   cp .env.example .env
   # fill in .env
   cd build/debug && xvfb-run -a ./bin/macro-terminal
   ```

---

## API Keys Required (.env)

```bash
# Market Data
FRED_API_KEY=
ALPHA_VANTAGE_API_KEY=
POLYGON_API_KEY=
MARKETSTACK_API_KEY=
TRADIER_API_KEY=
FINNHUB_API_KEY=
AXIONQUANT_API_KEY=

# News / Sentiment (all 4 used for Section 2 geo-scoped feeds)
NEWSAPI_API_KEY=
GNEWS_API_KEY=
NEWSDATAIO_API_KEY=
WORLDNEWSAPI_API_KEY=

# LLM (Section 3 rationale)
ANTHROPIC_API_KEY=

# Remote Sensing
NASA_GIBS_API_KEY=

# Optional
MAPBOX_API_KEY=
GEE_PROJECT_ID=
GEE_SERVICE_ACCOUNT_JSON=
```

---

## Testing

```bash
cmake --build build/debug --target macro-tests
ctest --preset default --output-on-failure
```

Test suites:
- `test_section2.cpp` — ArticleRecord, FeedDomain, zoom tier, debounce, query building
- `test_geo_context.cpp` — GeoSelectionContext, AppStateBus pub/sub
- `test_factor_model.cpp` — Eigen OLS, TerrainGrid mdspan
- `test_gmsi.cpp` — GMSI seeding, stress ordering, color mapping
- `test_providers.cpp` — NormalizedRecord validation, JSON safety

---

## File Structure

```
macro-terminal/
├── .devcontainer/          VS Codespace setup
├── .github/workflows/      GitHub Actions CI
├── src/
│   ├── app/                AppStateBus, GeoSelectionContext, Secrets, Application
│   ├── globe/              GlobeLayer (osgEarth FBO), GMSIComputer
│   ├── tables/             Section 2 — TablesLayer, FeedModule, GeoScopedFetcher,
│   │                                    ArticleRecord (NEW remodel)
│   ├── topography/         Section 3 — FactorModel, TopographyLayer, LLMRationaleService
│   ├── providers/          14 data providers (FRED, Finnhub, Polygon, AlphaVantage,
│   │                        MarketStack, Tradier, AxionQuant, IMF, WorldBank,
│   │                        OpenMeteo, WHO, NASAGIBS, GEE, NewsAggregator)
│   └── ui_common/          Theme, StatusBar, FilterRail, LocationContextPanel
└── tests/                  Catch2 unit tests
```

---

*C++23 · OpenGL 4.6 · Dear ImGui (docking) · osgEarth · Eigen3 · libcurl · nlohmann/json*
