# 音乐播放器视觉重构计划 · Apple 黑胶 + 歌词 + 翻译 + 环形声波

- 立项日期：2026-04-28
- 设计稿：`docs/music_mockups/FINAL_apple_vinyl_AB_mode3_yahei.html`
- 风格定稿索引：`docs/music_mockups/index.html`（六方案对比）→ 选 1 号
- 范围：仅音乐组件视觉与歌词协同，不动状态机、命中分发、媒体监听等底层逻辑

---

## 1. 目标与边界

### 要做
1. **紧凑态（A 布局）**：黑胶 + 标题 + 副标题位换为当前歌词（含 YRC 逐字 wipe），可选挂一行小字译文；hover 时迷你 EQ → 三个迷你按钮。
2. **展开态（B 布局）**：左大黑胶（含**环形声波脉动**），右三行歌词列，仅当前句下挂小字译文（翻译模式三）；下方进度条 + 五个控制按钮（❤ / ⏮ / ⏯ / ⏭ / 歌词模式）。
3. **歌词翻译**：扩 `LyricsMonitor` 解析网易云 `tlyric`（标准 LRC 格式）；按时间戳与原词行对齐合并。
4. **字体统一**：所有音乐+歌词文本走 `Microsoft YaHei UI`（系统自带，零打包，加 `Segoe UI` 作英文回退）。
5. **配套设置项**：翻译显示四态、波形显示开关。

### 不做（保持现状）
- 不动 `DynamicIsland.cpp` 的状态机、模式切换、收缩态。
- 不动 `LayoutController` 弹簧动画与 DPI 处理。
- 不动 `MediaMonitor` / SMTC 接入逻辑。
- 不动 `WaveformComponent` 现有的紧凑态使用；展开态长条波形**移除**，改用黑胶外圈环形脉动（在 `MusicPlayerComponent` 内自绘，不复用 `WaveformComponent`）。
- 不新增第三方依赖。
- 不引入额外打包字体。

---

## 2. 文件改动地图

| 文件 | 改动类型 | 说明 |
|---|---|---|
| `include/Constants.h` | 改 | `MUSIC_EXPANDED_HEIGHT` 160 → ~240；`MUSIC_COMPACT_HEIGHT` 64 维持，新增 `MUSIC_COMPACT_HEIGHT_WITH_LYRIC = 76`（含歌词+译文时） |
| `include/IslandState.h` | 改 | `LyricData` 增加 `std::wstring translation;` 字段（按行翻译，可空） |
| `include/LyricsMonitor.h` | 改 | `FetchedLyrics` 增加 `std::wstring tlyricLrc;`；新增 `std::map<int64_t, std::wstring> m_translations` 缓存 |
| `src/LyricsMonitor.cpp` | 改 | `FetchLyricsFromNetEase` JSON 解析增加 `tlyric.lyric`；新增 LRC 翻译解析函数；`GetLyricData` 按当前行 `currentMs` 在翻译表查最近匹配并填入 `LyricData::translation` |
| `include/components/MusicPlayerComponent.h` | 改 | 新增成员：`m_hovered`（紧凑 hover）、`m_audioLevel`（环形脉动振幅）、`m_ringPhase`（环动画相位）；新增方法 `RenderVinylRings(...)`、`RenderCompactControls(...)`；删除 `RenderProgressBar`/`RenderPlaybackButtons` 中和长条波形相关的耦合（如有） |
| `src/components/MusicPlayerComponent.cpp` | 大改 | 见 §3 详细布局 |
| `include/components/LyricsComponent.h` | 改 | 新增「三行模式」绘制接口 `DrawThreeLines(rect, currentLine, prevLine, nextLine, contentAlpha, currentTimeMs)`；当前句支持挂译文 |
| `src/components/LyricsComponent.cpp` | 改 | 实现三行模式；YRC wipe 逻辑复用现有 word-level 高亮；译文行字号 11px、透明度 0.45 |
| `src/SettingsWindow.cpp` | 改 | 新增「歌词翻译」单选组（关闭 / 仅当前句 / 每行 / 仅译文）、「黑胶环形声波」开关 |
| `include/Settings.h` 或对应 settings 头 | 改 | 新增枚举 `LyricTranslationMode { Off, CurrentOnly, AllLines, TranslationOnly }`，配置字段 `bool vinylRingPulse` |
| `src/RenderEngine.cpp` | 小改 | 展开态长条波形调用点移除（如有），改为不在音乐展开态调用 `WaveformComponent` |

**保持不动**：`WaveformComponent.cpp/h`（其它场景仍可用）、`DynamicIsland.cpp`、`LayoutController.cpp`。

---

## 3. 详细布局规格

### 3.1 紧凑态（圆角胶囊）

```
┌─────────────────────────────────────────────┐
│ [黑胶] 标题                            [EQ]│   ← 无歌词，高 64
│        艺术家                              │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│ [黑胶] 标题                            [EQ]│   ← 有歌词+翻译，高 76
│        I'm blinded by the lights           │
│        我已被光芒所眩目                    │
└─────────────────────────────────────────────┘

Hover 后：
┌─────────────────────────────────────────────┐
│ [黑胶] 标题                  [⏮][⏯][⏭]   │
│        I'm blinded by the lights           │
│        我已被光芒所眩目                    │
└─────────────────────────────────────────────┘
```

- 高度根据「是否有歌词 + 是否启用翻译」动态切换：64 / 76。切换走 `LayoutController` 现有的高度过渡（无需新增动画）。
- EQ 与按钮组占用同一段右侧区域；hover 进入 / 离开切换时用 200ms alpha 过渡。
- 命中区与现有播放/上一/下一按钮一致，复用 `m_hoveredButton` / `m_pressedButton`。

### 3.2 展开态（圆角矩形 28px）

```
┌─────────────────────────────────────────────┐
│  ╭───╮   Blinding Lights                    │
│  │ ◉ │   The Weeknd                         │
│  │  ⊙│   ─────────────────────────          │
│  ╰───╯   I'm drowning in the night          │
│   ↑      I'm blinded by the lights ◀ YRC    │
│   环形    我已被光芒所眩目                  │  ← 模式三仅当前句挂译
│   脉动    No, I can't sleep until ...       │
│                                             │
│   1:24  ━━━━━━━━━━●─────────────  3:20      │
│                                             │
│         ❤    ⏮    ⏯    ⏭    📜            │
└─────────────────────────────────────────────┘
```

- 总高约 240px（旧 160 + 三行歌词 60 + 控制条 20）。
- **环形脉动**：黑胶外圈绘制 2-3 圈半透明白色描边；半径 = 黑胶半径 + (8 + 振幅 × 14) px，stroke-width 1.5px，opacity 随半径线性衰减到 0；播放时相位以 deltaTime 推进、振幅来自 `m_audioLevel`（暂取 `m_isPlaying ? 0.5+0.5*sin(t) : 0`，未来可接 WASAPI peak）；暂停时圈定格在最外，opacity 衰减一半。
- 三行歌词左对齐，行间距 4px；当前句字号 16/700，上下行 12/normal，opacity 0.28 / 0.42。
- 译文紧贴当前句下方，11px / 0.45 透明、字重 400。
- 控制按钮 5 个，居中分布，间距 22px；播放按钮 40px 圆白底黑图标，其余 36px 透明 hover 高亮。
- 进度条点击/拖拽命中沿用现有逻辑，时间标签 11px / 0.5 透明。

### 3.3 字体策略

- 全局通过 `IDWriteFactory::CreateTextFormat` 指定 family `L"Microsoft YaHei UI"`，`L"Segoe UI"` 作英文 fallback（用 `IDWriteFontFallback` 配置 latin range → Segoe UI，CJK range → Microsoft YaHei UI）。
- 字号约定：标题 14、艺术家 11、歌词当前 16、歌词上下 12、译文 11、时间 11、按钮无字。

---

## 4. 翻译数据流

```
NetEase /lyric?id=xxx
   ├─ lrc.lyric        (LRC, 行级时间戳)
   ├─ yrc.lyric        (YRC, 字级时间戳)  ← 已用
   └─ tlyric.lyric     (LRC, 中文翻译)    ← 新增

LyricsMonitor::FetchLyricsFromNetEase
   → FetchedLyrics { yrcLyric, lrcLyric, tlyricLrc(新) }

LyricsMonitor (主循环 / GetLyricData)
   → 解析 tlyricLrc 为 map<startMs, wstring>
   → 按当前行的 currentMs 查最近匹配（容差 ±50ms）
   → 写入 LyricData::translation

LyricsComponent / MusicPlayerComponent
   → 渲染时根据 SettingsWindow 设置的 LyricTranslationMode 选择是否绘制
```

容错：
- `tlyric` 缺失：translation 为空字符串，UI 自动隐藏。
- 行级 LRC 与 YRC 行边界不一定对齐：以 YRC 行的 `currentMs` 为基准，向前查找最近的 LRC 翻译时间戳（不超过该行结束时间）。
- 翻译解析失败不影响原词显示。

---

## 5. 实施步骤

按提交粒度，每步可独立编译运行。

### Step 1 — 翻译数据通路（不改 UI）
- `LyricData::translation` 字段新增。
- `LyricsMonitor` JSON 解析增 `tlyric`、新增 LRC 解析与查询。
- 验证：在 `OutputDebugString` 打印当前行翻译，确认网易云返回正常的歌曲（如 Blinding Lights）能拿到中文译文。

### Step 2 — 字体回退集中化
- 抽一个 `CreateLyricTextFormat(size, weight)` 工具函数到 `RenderEngine` 或新文件 `MusicTextStyles.cpp`。
- 全部音乐+歌词文本走它，确保 family/fallback 一致。
- 验证：界面字体观感一致，CJK 不掉字。

### Step 3 — 紧凑态 A 布局
- `RenderCompact` 重排：副标题位渲染歌词；如启用翻译再加一行；高度联动 64↔76。
- Hover 时切换 EQ ↔ 按钮组；按钮命中沿用现有逻辑。
- 验证：无歌词、有 LRC、有 YRC、有译文 4 种数据组合分别正常。

### Step 4 — 展开态 B 布局（不含环形脉动）
- `RenderExpanded` 重排：左黑胶（保持现有静态）/ 右三行歌词列 / 进度条 / 5 按钮。
- `LyricsComponent::DrawThreeLines` 新接口；译文按模式三仅挂当前句。
- `MUSIC_EXPANDED_HEIGHT` 改 240，确认 `LayoutController` 弹簧无吐字。
- 验证：动态歌词跳行平滑，YRC wipe 落在中间一行。

### Step 5 — 黑胶环形声波脉动
- `RenderVinylRings`：3 圈，半径 = baseR + offset + audioLevel × amp，opacity 衰减。
- `Update(deltaTime)` 推进 `m_ringPhase`；`m_audioLevel` 暂用 sin 模拟；播放/暂停切换有 300ms 平滑过渡。
- 移除展开态对长条 `WaveformComponent` 的调用。
- 验证：播放时脉动节律柔和（不闪），暂停立即收敛但不抖。

### Step 6 — 设置项
- `LyricTranslationMode` 单选组、`vinylRingPulse` 开关；写入现有 INI / 配置流。
- 设置变更热更新到组件（已有事件机制照用）。
- 验证：四种翻译模式切换、关闭脉动后退化为静态黑胶外圈描边。

### Step 7 — 收尾
- 编译 Debug + Release。
- 冒烟：无歌词 / LRC / YRC / 翻译四态 × 紧凑/展开/暂停 = 12 个用例。
- `git status -sb` 排除 `.omx/` `.claude/` 后单提交。

---

## 6. 验收用例（冒烟）

| # | 场景 | 预期 |
|---|---|---|
| 1 | 无 SMTC 会话 | 紧凑态隐藏（IsActive=false），不绘制 |
| 2 | 有会话无歌词 | 紧凑态高 64，副标题为艺术家 |
| 3 | 有 LRC 无 YRC | 紧凑/展开都有歌词，无 wipe，行级跳变 |
| 4 | 有 YRC | wipe 高亮按字推进，紧凑态 + 展开态同步 |
| 5 | 有 tlyric | 紧凑态多挂一行 11px 译文，展开态当前句下挂译文 |
| 6 | 设置改翻译为「关闭」 | 译文行立即消失，岛体高度回落 |
| 7 | 设置改翻译为「仅译文」 | 中间行显示译文为主，原文淡化为副 |
| 8 | 暂停 | 黑胶停转，环形圈定格淡出，wipe 暂停 |
| 9 | 紧凑态 hover | 200ms 内 EQ 淡出、三按钮淡入 |
| 10 | 展开态点击歌词模式按钮 | 留作未来「沉浸态」入口，本期暂触发 placeholder（或灰禁） |
| 11 | DPI 150% | 所有圆角、字号、间距 round 到整像素，无毛边 |
| 12 | 关闭环形脉动 | 黑胶仅一圈静态描边，CPU 占用回落 |

---

## 7. 风险与回退

| 风险 | 缓解 |
|---|---|
| 网易云 `tlyric` 偶发返回空对象 | 解析容错 + 缺失即隐藏，不报错 |
| 高度从 160 → 240 撑破 LayoutController 弹簧上限 | Step 4 先单独改高度跑一次，确认弹簧无 overshoot；必要时调 spring stiffness |
| 环形脉动每帧重画 3 圈描边 → 60fps 下 GPU 微涨 | `m_audioLevel` 变化阈值 < 0.01 时跳过重绘；脉动开关默认开但可关 |
| 新字体回退导致历史 LRC（含日韩）字形掉字 | `IDWriteFontFallback` 显式覆盖 hiragana/katakana/hangul 区段到 Microsoft YaHei UI（CJK 通用 fallback 已覆盖） |
| 翻译模式切换到「仅译文」时 YRC wipe 失去意义 | 该模式下退化为整行高亮，不做 wipe |

回退预案：每步单提交，任一步出问题可单独 revert，不影响其它已落地步骤。

---

## 8. 不在本期范围（候选未来项）

- D 沉浸态（第三档高度，5-7 行歌词全屏）
- 真实音频频谱接入（WASAPI loopback → 环形脉动振幅）
- 浅色主题 / 自定义字体打包（Inter / HarmonyOS / 文楷）
- 歌词长按切换原文/译文（紧凑态）
- 「歌词模式按钮」实际跳转沉浸态

---

完。
