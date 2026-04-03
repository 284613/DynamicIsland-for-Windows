#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

// ============================================
// 插件接口 - 第三方扩展支持
// ============================================

// 插件信息
struct PluginInfo {
    std::wstring name;           // 插件名称
    std::wstring version;       // 版本号
    std::wstring author;        // 作者
    std::wstring description;   // 描述
};

// 插件状态
enum class PluginState {
    Loaded,      // 已加载
    Unloaded,    // 未加载
    Error,       // 加载错误
};

// 插件接口（所有插件必须实现）
class IPlugin {
public:
    virtual ~IPlugin() = default;
    
    // 插件信息
    virtual PluginInfo GetInfo() const = 0;
    
    // 初始化（插件加载时调用）
    virtual bool Initialize() = 0;
    
    // 卸载（插件卸载时调用）
    virtual void Shutdown() = 0;
    
    // 更新（每帧调用）
    virtual void Update(float deltaTime) = 0;
    
    // 渲染（如果有UI）
    virtual void Render() {}
};

// 插件加载器
class PluginManager {
public:
    PluginManager();
    ~PluginManager();
    
    // 加载指定目录下的所有插件
    bool LoadPlugins(const std::wstring& pluginDir);
    
    // 卸载所有插件
    void UnloadAll();
    
    // 重新加载
    bool ReloadPlugins();
    
    // 获取插件数量
    int GetCount() const { return (int)m_plugins.size(); }
    
    // 获取插件
    IPlugin* GetPlugin(int index);
    IPlugin* GetPlugin(const std::wstring& name);
    
    // 更新所有插件
    void UpdateAll(float deltaTime);
    
    // 渲染所有插件
    void RenderAll();

private:
    std::vector<std::unique_ptr<IPlugin>> m_plugins;
    std::wstring m_pluginDir;
};

// ============================================
// 示例插件接口 - 时钟插件
// ============================================
class IClockPlugin : public IPlugin {
public:
    // 获取当前时间显示
    virtual std::wstring GetTimeString() const = 0;
    
    // 获取日期显示
    virtual std::wstring GetDateString() const = 0;
};

// ============================================
// 示例插件接口 - 天气插件
// ============================================
class IWeatherPlugin : public IPlugin {
public:
    // 获取天气描述
    virtual std::wstring GetWeatherDescription() const = 0;
    
    // 获取温度
    virtual float GetTemperature() const = 0;
};


