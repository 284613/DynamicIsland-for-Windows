# 灵动岛优化计划
**创建时间：2026-04-04**
**负责人：秘书（自动执行中）**

## 优化项清单

| ID | 优化项 | 优先级 | 状态 | 备注 |
|----|--------|--------|------|------|
| OPT-01 | 全屏/游戏模式 | P0 | 待执行 | 改动小，收益大 |
| OPT-02 | 媒体功耗优化 | P1 | 待执行 | 事件驱动+休眠 |
| OPT-03 | 优先级调度 | P2 | 待执行 | P0-P3权重 |
| OPT-04 | 动画驱动升级 | P3 | 待执行 | 高复杂度 |
| OPT-05 | 歌词智能滚动 | P4 | 待执行 | 弹簧插值 |

## 执行规则
1. 串行执行，不并行
2. 每个任务完成后写状态到 OPT_STATUS.md
3. 编译通过后才算完成
4. 30分钟检查点：检查是否卡住

## OPT-01 详细设计：全屏/游戏防打扰模式

### 现状
灵动岛始终悬浮在屏幕顶部，全屏应用会被遮挡

### 目标
检测到全屏应用时，自动隐藏或静音灵动岛

### 实现方案
1. 在 DynamicIsland.cpp 的主循环中，每秒检测前台窗口
2. 如果前台窗口是全屏模式（窗口大小=屏幕分辨率），隐藏灵动岛
3. 退出全屏时恢复，并显示全屏期间缓存的通知汇总

### 检测方法
```cpp
BOOL IsFullscreen() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return FALSE;
    
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    
    RECT rc;
    GetWindowRect(hwnd, &rc);
    
    // 全屏判断：窗口占满整个显示器
    return (rc.left == mi.rcMonitor.left && 
            rc.top == mi.rcMonitor.top &&
            rc.right == mi.rcMonitor.right && 
            rc.bottom == mi.rcMonitor.bottom);
}
```

### 影响范围
- DynamicIsland.h/cpp: 添加 IsFullscreen() 方法和检测定时器
- 无需新增文件
