# Settings Window Rewrite Execution Plan

## Scope

Rewrite `SettingsWindow` from GDI + Win32 child controls to a Direct2D-rendered, animated window that writes to the same `config.ini` consumed by `DynamicIsland`.

## Guardrails

- Keep the public `SettingsWindow` entry points (`Create`, `Show`, `Hide`, `Toggle`, `LoadSettings`, `SaveSettings`, `ApplySettings`) stable.
- Remove dependence on child `STATIC`, `BUTTON`, and `TRACKBAR` controls inside the settings surface.
- Reuse existing `Spring` physics and avoid new dependencies.
- Preserve current tray entry flow and `WM_SETTINGS_APPLY` callback behavior.

## Execution Slices

1. Replace the current window internals with D2D/DWrite resources, custom control models, and animation state.
2. Rebuild navigation, cards, toggles, sliders, buttons, and text inputs as self-drawn controls with hit testing.
3. Switch persistence to the executable-adjacent `config.ini`, add the new sections/keys, and wire `DynamicIsland` reload on apply.
4. Verify with a release build and fix any compile/runtime regressions introduced by the rewrite.

## Verification Target

- Settings window compiles and opens.
- Category switch animates without recreating Win32 child controls.
- Save/apply writes expected INI sections.
- `DynamicIsland` reloads config after `WM_SETTINGS_APPLY`.

## Current Status

Completed:
- `SettingsWindow` has been moved to a Direct2D / DirectWrite self-drawn implementation.
- Child `STATIC` / `BUTTON` / `TRACKBAR` controls were removed from the settings surface.
- Navigation, cards, toggles, sliders, text inputs, footer buttons, scrolling, and page transitions are all running through the custom control model.
- Persistence now targets the executable-adjacent `config.ini`.
- `WM_SETTINGS_APPLY` now triggers `DynamicIsland` config reload, notification whitelist refresh, weather refresh, and low-battery threshold update.
- Release x64 currently builds successfully.

Follow-up cleanup still worth doing:
- `src/SettingsWindow_part2.cpp` has already been merged back into `src/SettingsWindow.cpp`.
- Further profile settings-window repaint frequency during drag-heavy interactions.
- Add more visual polish items from the redesign plan that are not yet implemented, such as richer traffic-light hover affordances and appearance-page theme tooling.
