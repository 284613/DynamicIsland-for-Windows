#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// ============================================
// 局域网文件传输 - 跨设备快速传文件
// ============================================

// 文件传输消息类型
enum class LanMessageType {
    DeviceAnnounce,    // 设备公告
    FileOffer,         // 文件发送请求
    FileAccept,        // 接收方接受
    FileReject,        // 接收方拒绝
    FileData,          // 文件数据
    FileComplete,       // 传输完成
};

// 设备信息
struct LanDevice {
    std::wstring name;
    std::wstring ip;
    int port;
};

// 传输进度回调
using TransferProgressCallback = std::function<void(float progress)>;
// 接收文件回调
using FileReceiveCallback = std::function<void(const std::wstring& filename, int size)>;

class LanTransferManager {
public:
    LanTransferManager();
    ~LanTransferManager();
    
    // 初始化（启动UDP广播监听）
    bool Initialize();
    
    // 关闭
    void Shutdown();
    
    // 广播自己的存在
    void AnnounceDevice(const std::wstring& deviceName, int port);
    
    // 发现设备
    void DiscoverDevices();
    
    // 获取发现到的设备列表
    std::vector<LanDevice> GetDevices() const;
    
    // 发送文件
    bool SendFile(const std::wstring& deviceIp, int port, const std::wstring& filePath, 
                  TransferProgressCallback progressCallback = nullptr);
    
    // 设置接收回调
    void SetReceiveCallback(FileReceiveCallback callback) { m_receiveCallback = callback; }
    
    // 处理接收（需要每帧调用）
    void Process();

private:
    // UDP广播端口
    static const int BROADCAST_PORT = 45678;
    static const int DATA_PORT = 45679;
    
    SOCKET m_broadcastSocket = INVALID_SOCKET;
    SOCKET m_dataSocket = INVALID_SOCKET;
    
    std::vector<LanDevice> m_devices;
    bool m_initialized = false;
    
    FileReceiveCallback m_receiveCallback;
    
    // 线程句柄
    HANDLE m_broadcastThread = nullptr;
    HANDLE m_listenThread = nullptr;
    bool m_running = false;
};

// 初始化WS2
bool InitWinsock();


