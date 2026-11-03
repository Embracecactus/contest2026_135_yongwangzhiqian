<!-- SPDX-License-Identifier: Apache-2.0 -->
# 傻妞 App design system

Platform: native Android Views (no Compose). Mode: **Operate** — people come to
connect, check and adjust a physical companion, so familiar Material structure
leads and the brand lives in one signature.

## Concept

The app is the device's third display. The interface is porcelain and ink; the
only chromatic colour is the mint of her eyes (`shaniu_eye`), used only for what
is actually live: her eyes and verified links. When she is unreachable the app
reads grey — that is the connection signal, not decoration.

**Signature:** `CompanionPresenceView`, her two round displays. Mood is a
projection of verified session state only (never optimistic):

| Mood | When |
|---|---|
| ABSENT | no device claimed on this phone |
| OFFLINE | claimed, no authenticated link |
| SEARCHING | scanning / connecting / reconnecting |
| UNSURE | authenticated, snapshot not fresh |
| AWAKE | ready |
| ENGAGED | a conversation turn is running |
| DROWSY | no Wi-Fi or voice service not ready |
| HAPPY / ASLEEP | preview only (Customize) |

## Tokens

- Colour: `res/values[-night]/colors.xml` (`shaniu_*`) is the single source; the
  Material roles in `styles.xml` and `CompanionDesign` read it. Dynamic Color is
  **deliberately off**: a wallpaper-derived accent would break the
  "colour means alive" rule. Warning (amber) and danger (red) sit on top.
- Type: `CompanionDesign.Type` (sp, system face): Display 30, Title 22,
  Headline 16, Body 15, Callout 14, Caption 12.5, Label 12, Numeral 40 light.
- Space: 8 dp grid, 24 dp page gutter, 560 dp max content column.
- Shape: groups 22, controls 16, sheets 28 (top corners), buttons are pills.
- Touch: 48 dp minimum, rows 64 dp.

## Components (`CompanionPage`)

Grouped `settingsRow` (icon well, chevron/check/switch), `primaryButton` (one
filled action per view, always the next step — e.g. "选择本地更新包" until a
package is inspected, then "开始固件更新"; a gated action stays visible as a
disabled secondary), `secondaryButton` (outlined), `notice`,
`connectionChain` (phone → 傻妞 → 语音服务; live = mint, pending = dashed,
broken = amber gap, unknown = dotted), `volumeCard`, `segments`,
`updateIntroduction` / `updateProgress` (timeline; failure in danger tone),
`sheetHeader`, `presence` (in the discovery sheet it shrinks to 96 dp once
candidates are listed, so the list stays above the fold), `hero` (subtitle in
warning tone when verification failed).

## Code layout

`MainActivity` owns state, lifecycle and device/gateway protocol. Page
rendering lives in per-page files as `MainActivity` extensions:
`DevicePage.kt`, `CustomizePage.kt`, `UpdatePage.kt`, `SettingsPage.kt`,
`CompanionSheet.kt`, `LegacyConsolePages.kt` (legacy console mode) and
`MainActivityViews.kt` (shared view builders and labels). Members that the
instrumentation probes reach by reflection stay in `MainActivity` and are
public (not `internal`) so their JVM names are not mangled.

## Navigation

Four destinations (设备 · 定制 · 更新 · 设置): bottom bar below 600 dp, navigation
rail at 600 dp and wider. Detail pages keep a back stack, so Back and the
highlighted destination follow the path the user took. Outcome/failure
messages appear in the status banner above the page, never below the fold.

## Motion

- Tabs: fade-through, 140 ms, no travel (switched often).
- Drill-in / back: shared X axis, 24 dp, 220 ms emphasized-decelerate.
- Sheets: in 260 ms decelerate, out 180 ms decelerate. Tab pill 200 ms.
- Her eyes are the only ambient motion (blink, scan, pulse), ~30 fps, driven
  by uptime so re-renders never restart them.
- All motion follows the system "remove animations" setting.

## Rules

- No eyebrow/kicker labels above headings; labels sit beside their values.
- Never show device success before the device reads it back.
- Copy is Chinese, product voice warm and plain; errors name the recovery.
