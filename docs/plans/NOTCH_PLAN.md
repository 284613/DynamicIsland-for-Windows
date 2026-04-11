# 刘海改造记录

## 结果

主岛已从胶囊改为 macOS 风格刘海：
- 顶部贴顶，`TOP_MARGIN = 0`
- 仅底部两角圆角，`NOTCH_BOTTOM_RADIUS = 12`
- 副岛继续保持胶囊
- mini / collapsed 尺寸调整为 `96x24`

## 已完成改动

- `include/Constants.h`
  - `UI::TOP_MARGIN` 改为 `0.0f`
  - 新增 `UI::NOTCH_BOTTOM_RADIUS = 12.0f`
  - `MINI_WIDTH / MINI_HEIGHT` 改为 `96 / 24`
- `src/WindowManager.cpp`
  - 窗口初始 `posY` 改为 `0`
- `src/RenderEngine.cpp`
  - 主岛由 `RoundedRect` 改为 `PathGeometry` 刘海路径
  - 主岛裁剪层改为同形状 geometry
  - 副岛保持原胶囊渲染
- `src/DynamicIsland.cpp`
  - `UpdateWindowRegion()` 改为顶部平直、底部圆角的组合 region
  - `WM_NCHITTEST` 和主岛相关命中逻辑改为贴顶坐标
- `src/LayoutController.cpp`
  - 播放按钮和进度条命中使用新的顶部常量
- `include/RenderEngine.h`
  - 移除主岛胶囊圆角动画成员

## 验证

已通过：
```powershell
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
```

运行时应确认：
- idle 态贴顶、顶部平直、底部圆角
- 展开态顶部保持平直
- 副岛仍为胶囊
- 区域外可穿透
