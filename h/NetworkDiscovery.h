#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

// Discovered device info
struct DiscoveredDevice {
    std::wstring ipAddress;
    std::wstring deviceName;
    std::wstring deviceId;
    ULONGLONG lastSeen;
    bool isOnline;
};

// Network discovery service using UDP broadcast
class NetworkDiscovery {
public:
    NetworkDiscovery();
    ~NetworkDiscovery();

    // Start discovery service
    bool Start(int port = 45678);

    // Stop discovery service
    void Stop();

    // Get list of discovered devices
    std::vector<DiscoveredDevice> GetDevices() const;

    // Broadcast presence to local network
    void BroadcastPresence(const std::wstring& deviceName, const std::wstring& deviceId);

    // Check if service is running
    bool IsRunning() const { return m_running.load(); }

private:
    void DiscoveryLoop();
    void ListenLoop();
    void CleanupOldDevices();

    int m_port = 45678;
    SOCKET m_broadcastSocket = INVALID_SOCKET;
    SOCKET m_listenSocket = INVALID_SOCKET;
    
    std::atomic<bool> m_running{false};
    std::thread m_discoveryThread;
    std::thread m_listenThread;
    
    mutable std::mutex m_devicesMutex;
    std::vector<DiscoveredDevice> m_devices;
    
    static const int BROADCAST_INTERVAL_MS = 3000;
    static const int DEVICE_TIMEOUT_MS = 10000;
};

inline NetworkDiscovery::NetworkDiscovery() {}

inline NetworkDiscovery::~NetworkDiscovery() {
    Stop();
}

inline bool NetworkDiscovery::Start(int port) {
    if (m_running.load()) return true;
    
    m_port = port;
    
    // Create UDP socket for broadcast
    m_broadcastSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_broadcastSocket == INVALID_SOCKET) {
        return false;
    }

    // Enable broadcast
    BOOL broadcast = TRUE;
    setsockopt(m_broadcastSocket, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast));

    // Create listen socket
    m_listenSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_listenSocket == INVALID_SOCKET) {
        closesocket(m_broadcastSocket);
        m_broadcastSocket = INVALID_SOCKET;
        return false;
    }

    // Bind to port
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(m_listenSocket);
        closesocket(m_broadcastSocket);
        m_listenSocket = INVALID_SOCKET;
        m_broadcastSocket = INVALID_SOCKET;
        return false;
    }

    // Set socket to non-blocking
    u_long mode = 1;
    ioctlsocket(m_listenSocket, FIONBIO, &mode);

    m_running = true;
    
    // Start threads
    m_discoveryThread = std::thread(&NetworkDiscovery::DiscoveryLoop, this);
    m_listenThread = std::thread(&NetworkDiscovery::ListenLoop, this);

    return true;
}

inline void NetworkDiscovery::Stop() {
    m_running = false;
    
    if (m_discoveryThread.joinable()) {
        m_discoveryThread.join();
    }
    if (m_listenThread.joinable()) {
        m_listenThread.join();
    }

    if (m_broadcastSocket != INVALID_SOCKET) {
        closesocket(m_broadcastSocket);
        m_broadcastSocket = INVALID_SOCKET;
    }
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    std::lock_guard<std::mutex> lock(m_devicesMutex);
    m_devices.clear();
}

inline std::vector<DiscoveredDevice> NetworkDiscovery::GetDevices() const {
    std::lock_guard<std::mutex> lock(m_devicesMutex);
    return m_devices;
}

inline void NetworkDiscovery::BroadcastPresence(const std::wstring& deviceName, const std::wstring& deviceId) {
    if (m_broadcastSocket == INVALID_SOCKET || !m_running.load()) return;

    // Broadcast message format: "DI|deviceName|deviceId"
    std::wstring msg = L"DI|" + deviceName + L"|" + deviceId;
    std::string msgA(msg.begin(), msg.end());

    sockaddr_in broadcastAddr = {};
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(m_port);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    sendto(m_broadcastSocket, msgA.c_str(), (int)msgA.length(), 0,
        (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
}

inline void NetworkDiscovery::DiscoveryLoop() {
    while (m_running.load()) {
        // Clean up old devices
        CleanupOldDevices();
        
        // Sleep until next broadcast
        std::this_thread::sleep_for(std::chrono::milliseconds(BROADCAST_INTERVAL_MS));
    }
}

inline void NetworkDiscovery::ListenLoop() {
    char buffer[512];
    sockaddr_in fromAddr = {};
    int fromLen = sizeof(fromAddr);

    while (m_running.load()) {
        int bytes = recvfrom(m_listenSocket, buffer, sizeof(buffer) - 1, 0,
            (sockaddr*)&fromAddr, &fromLen);

        if (bytes > 0) {
            buffer[bytes] = '\0';
            
            // Parse message: "DI|name|id"
            std::string msg(buffer);
            if (msg.substr(0, 3) == "DI|") {
                size_t firstSep = msg.find('|', 3);
                size_t secondSep = msg.find('|', firstSep + 1);
                
                if (firstSep != std::string::npos && secondSep != std::string::npos) {
                    std::string name = msg.substr(3, firstSep - 3);
                    std::string id = msg.substr(firstSep + 1, secondSep - firstSep - 1);
                    
                    char ipStr[16];
                    inet_ntoa_s(fromAddr.sin_addr, ipStr, sizeof(ipStr));
                    
                    DiscoveredDevice device;
                    device.ipAddress = std::wstring(ipStr, ipStr + strlen(ipStr));
                    device.deviceName = std::wstring(name.begin(), name.end());
                    device.deviceId = std::wstring(id.begin(), id.end());
                    device.lastSeen = GetTickCount64();
                    device.isOnline = true;
                    
                    std::lock_guard<std::mutex> lock(m_devicesMutex);
                    
                    // Update or add device
                    bool found = false;
                    for (auto& d : m_devices) {
                        if (d.deviceId == device.deviceId) {
                            d.lastSeen = device.lastSeen;
                            d.isOnline = true;
                            d.ipAddress = device.ipAddress;
                            found = true;
                            break;
                        }
                    }
                    
                    if (!found) {
                        m_devices.push_back(device);
                    }
                }
            }
        }
        
        fromLen = sizeof(fromAddr);
    }
}

inline void NetworkDiscovery::CleanupOldDevices() {
    ULONGLONG now = GetTickCount64();
    
    std::lock_guard<std::mutex> lock(m_devicesMutex);
    
    for (auto& device : m_devices) {
        if (now - device.lastSeen > DEVICE_TIMEOUT_MS) {
            device.isOnline = false;
        }
    }
    
    // Remove devices that have been offline for a while
    m_devices.erase(
        std::remove_if(m_devices.begin(), m_devices.end(),
            [now](const DiscoveredDevice& d) {
                return !d.isOnline && (now - d.lastSeen > DEVICE_TIMEOUT_MS * 2);
            }),
        m_devices.end()
    );
}




