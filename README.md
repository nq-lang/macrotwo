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

