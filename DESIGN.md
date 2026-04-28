# Design System: Dynamic Island for Windows

This document defines the visual language, design tokens, and UI components for the Dynamic Island project. It serves as a bridge between design intent and implementation, optimized for both human developers and AI agents.

## 1. Visual Theme & Atmosphere
The design follows the **Apple Fluid Design** philosophy:
- **Atmosphere:** Minimalist, immersive, and responsive.
- **Glassmorphism:** Uses 80% opacity with subtle backdrop saturation and blur (where supported by DirectComposition).
- **Motion-First:** Every change in state must be driven by spring physics to ensure a "physical" and "organic" feel.
- **Surface:** Deep dark backgrounds with vibrant semantic accents.

## 2. Design Tokens

### 2.1 Color Palette
Semantic tokens mapped to hex codes for implementation.

| Token | Hex / RGBA | Usage |
|-------|------------|-------|
| `surface-primary` | `#14141a` (80% Alpha) | Main capsule background |
| `accent-wifi` | `#1acc4d` | WiFi connection status |
| `accent-bluetooth` | `#3380ff` | Bluetooth connectivity |
| `accent-file` | `#1a99ff` | File transfer and storage |
| `accent-error` | `#ff3b30` | Low battery or critical alerts |
| `text-primary` | `#ffffff` | Primary headings and info |
| `text-secondary` | `rgba(255, 255, 255, 0.6)` | Subtitles and metadata |
| `progress-track` | `rgba(255, 255, 255, 0.2)` | Inactive progress bar |
| `progress-fill` | `#ffffff` | Active progress bar fill |

### 2.2 Typography
Based on **Microsoft YaHei UI** for CJK and **Segoe UI** for English backdrops.

- **Primary Typeface:** Microsoft YaHei UI (Medium/Semibold)
- **Fallback:** Segoe UI / SF Pro (where available)
- **Iconography:** Segoe MDL2 Assets (for system icons)

| Level | Size | Weight | Tracking | Usage |
|-------|------|--------|----------|-------|
| **Heading** | 14px | 600 | -0.2px | Song titles, Alert titles |
| **Body** | 12px | 400 | -0.1px | Artist names, Time, Weather |
| **Lyrics** | 16px | 700 | 0px | Current active lyric line |
| **Caption** | 10px | 400 | 0px | Progress timestamps, Translation info |

### 2.3 Layout & Spacing
- **Base Unit:** 4px
- **Corner Radius:** 
  - Main Capsule: 16px - 28px (dynamic based on height)
  - Album Art: 6px
- **Padding:**
  - Compact: 12px horizontal, 8px vertical
  - Expanded: 16px horizontal, 16px vertical

## 3. Component Stylings

### 3.1 Main Island Capsule
The core container that morphs between states.

| Mode | Dimensions (WxH) | Corner Radius | Content |
|------|------------------|---------------|---------|
| `Idle` | 80x28 px | 14px | Digital Clock / Weather Icon |
| `MusicCompact` | 200x64/76 px | 20px | Title + Lyrics + EQ/Buttons |
| `MusicExpanded` | 340x240 px | 28px | Large Vinyl + Ring Pulse + 3-Line Lyrics |
| `WeatherExpanded` | 340x160 px | 28px | Ambient Background + 2x3 Hourly Grid |
| `Alert` | 220x40 px | 20px | Icon + Title + Status Text |
| `Volume` | 180x32 px | 16px | Icon + Level Bar |

### 3.2 Music Player (Expanded)
- **Vinyl Styling:** Large black vinyl disc with dynamic album art in center.
- **Circular Ring Pulse:** 2-3 semi-transparent rings pulsing from the vinyl based on audio amplitude.
- **Controls:** Five circular buttons (36-40px) including Heart, Prev, Play/Pause, Next, and Lyric Mode.
- **Lyrics:** Three-line vertical layout with center-focused YRC wipe. Current line supports translation sub-text.

## 4. Animation Principles (Spring Physics)
All transitions use a **Mass-Spring-Damper** model.

| Preset | Stiffness | Damping | Mass | Description |
|--------|-----------|---------|------|-------------|
| `Bouncy` | 180.0 | 12.0 | 1.0 | For alerts and expansion (high energy) |
| `Smooth` | 100.0 | 18.0 | 1.0 | For opacity and subtle position changes |
| `Default` | 120.0 | 14.0 | 1.0 | Balanced movement for size changes |

## 5. Agent Prompt Guide
When generating UI code or assets for this project:
1. **Always use Direct2D/DirectComposition** for rendering.
2. **Prioritize `Spring.h`** for all coordinate and alpha calculations.
3. **Clip all content** within the capsule geometry using `ID2D1RoundedRectangleGeometry`.
4. **Adhere to the 80% alpha** rule for backgrounds to maintain "Glassmorphism" consistency.
5. **Use anti-aliased text** (`D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE`) for high-DPI clarity.
