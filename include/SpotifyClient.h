#pragma once
#include <string>
#include <functional>

// ============================================
// Spotify API - 深度Spotify集成
// ============================================

// Spotify播放状态
enum class SpotifyPlaybackState {
    Playing,
    Paused,
    Stoped,
    Unknown
};

// Spotify曲目信息
struct SpotifyTrack {
    std::wstring id;
    std::wstring name;
    std::wstring artist;
    std::wstring album;
    int durationMs;       // 毫秒
    int positionMs;       // 当前播放位置
    bool isPlaying;
};

// Spotify API 客户端
class SpotifyClient {
public:
    SpotifyClient();
    ~SpotifyClient();
    
    // OAuth认证
    bool Authenticate(const std::wstring& clientId, const std::wstring& clientSecret);
    bool AuthenticateWithCode(const std::wstring& authCode);
    void Logout();
    
    // 是否已认证
    bool IsAuthenticated() const { return m_authenticated; }
    
    // 获取当前播放状态
    SpotifyPlaybackState GetPlaybackState();
    
    // 获取当前曲目
    SpotifyTrack GetCurrentTrack();
    
    // 播放控制
    bool Play();
    bool Pause();
    bool NextTrack();
    bool PreviousTrack();
    bool Seek(int positionMs);
    
    // 设置音量 (0-100)
    bool SetVolume(int volume);
    
    // 回调：播放状态变化
    void SetOnPlaybackStateChangedCallback(std::function<void(SpotifyPlaybackState)> callback) {
        m_onStateChanged = callback;
    }

private:
    std::wstring m_accessToken;
    std::wstring m_refreshToken;
    bool m_authenticated = false;
    
    std::function<void(SpotifyPlaybackState)> m_onStateChanged;
    
    // HTTP请求辅助
    std::wstring HttpGet(const std::wstring& url);
    std::wstring HttpPost(const std::wstring& url, const std::wstring& body);
};

// 简化的Spotify Web API端点
namespace SpotifyAPI {
    const std::wstring AUTH_URL = L"https://accounts.spotify.com/authorize";
    const std::wstring TOKEN_URL = L"https://accounts.spotify.com/api/token";
    const std::wstring API_BASE_URL = L"https://api.spotify.com/v1";
    
    // 获取当前播放
    const std::wstring GET_CURRENT_PLAYBACK = L"/me/player";
    
    // 播放控制
    const std::wstring PLAY = L"/me/player/play";
    const std::wstring PAUSE = L"/me/player/pause";
    const std::wstring NEXT = L"/me/player/next";
    const std::wstring PREVIOUS = L"/me/player/previous";
    const std::wstring SEEK = L"/me/player/seek";
    const std::wstring VOLUME = L"/me/player/volume";
}


