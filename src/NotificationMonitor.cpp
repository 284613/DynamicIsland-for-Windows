#include "NotificationMonitor.h"

#include "Messages.h"
#include "EventBus.h"

#include <guiddef.h>

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Notifications;
using namespace Windows::UI::Notifications::Management;

NotificationMonitor::NotificationMonitor() = default;

NotificationMonitor::~NotificationMonitor() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void NotificationMonitor::Initialize(HWND hwnd, const std::vector<std::wstring>& allowedApps) {
    m_hwnd = hwnd;
    UpdateAllowedApps(allowedApps);
    m_running = true;
    m_thread = std::thread(&NotificationMonitor::Worker, this);
}

void NotificationMonitor::UpdateAllowedApps(const std::vector<std::wstring>& allowedApps) {
    std::lock_guard<std::mutex> lock(m_allowedAppsMutex);
    m_allowedApps = allowedApps;
}

bool NotificationMonitor::MarkProcessed(uint32_t notificationId) {
    if (m_processedNotifSet.find(notificationId) != m_processedNotifSet.end()) {
        return false;
    }

    m_processedNotifSet.insert(notificationId);
    m_processedNotifOrder.push_back(notificationId);
    while (m_processedNotifOrder.size() > kProcessedNotificationLimit) {
        const uint32_t oldest = m_processedNotifOrder.front();
        m_processedNotifOrder.pop_front();
        m_processedNotifSet.erase(oldest);
    }
    return true;
}

void NotificationMonitor::Worker() {
    winrt::init_apartment();

    try {
        UserNotificationListener listener = UserNotificationListener::Current();
        const auto access = listener.RequestAccessAsync().get();
        if (access != UserNotificationListenerAccessStatus::Allowed) {
            return;
        }

        while (m_running) {
            try {
                auto notifications = listener.GetNotificationsAsync(NotificationKinds::Toast).get();
                for (const auto& notif : notifications) {
                    const uint32_t notificationId = notif.Id();
                    if (!MarkProcessed(notificationId)) {
                        continue;
                    }

                    auto appInfo = notif.AppInfo();
                    if (!appInfo) {
                        continue;
                    }

                    const std::wstring appName = appInfo.DisplayInfo().DisplayName().c_str();
                    std::vector<std::wstring> allowedApps;
                    {
                        std::lock_guard<std::mutex> lock(m_allowedAppsMutex);
                        allowedApps = m_allowedApps;
                    }

                    bool isAllowed = false;
                    for (const auto& allowed : allowedApps) {
                        if (!allowed.empty() && appName.find(allowed) != std::wstring::npos) {
                            isAllowed = true;
                            break;
                        }
                    }
                    if (!isAllowed) {
                        continue;
                    }

                    std::wstring title = appName;
                    std::wstring body = L"收到新消息";
                    try {
                        auto notification = notif.Notification();
                        auto toast = notification.try_as<ToastNotification>();
                        if (toast) {
                            auto xml = toast.Content();
                            if (xml) {
                                auto textNodes = xml.GetElementsByTagName(L"text");
                                if (textNodes.Size() >= 1) title = textNodes.GetAt(0).InnerText().c_str();
                                if (textNodes.Size() >= 2) body = textNodes.GetAt(1).InnerText().c_str();
                            }
                        }
                    } catch (...) {
                    }

                    std::vector<uint8_t> iconData;
                    try {
                        auto displayInfo = appInfo.DisplayInfo();
                        if (displayInfo) {
                            auto logo = displayInfo.GetLogo(Windows::Foundation::Size(64, 64));
                            if (logo) {
                                iconData = ReadIconToMemory(logo);
                            }
                        }
                    } catch (...) {
                    }

                    std::wstring combined = title;
                    if (!body.empty() && body != L"收到新消息") {
                        combined += L": ";
                        combined += body;
                    }

                    AlertInfo info{
                        3,
                        appName,
                        combined,
                        iconData.empty() ? ExtractIconFromExe(appName) : L"",
                        std::move(iconData)
                    };
                    EventBus::GetInstance().PublishNotificationArrived(info);
                }
            } catch (...) {
            }

            Sleep(1500);
        }
    } catch (...) {
    }
}

std::vector<uint8_t> NotificationMonitor::ReadIconToMemory(const IRandomAccessStreamReference& icon) {
    if (!icon) {
        return {};
    }

    try {
        auto stream = icon.OpenReadAsync().get();
        DataReader reader{ stream };
        reader.LoadAsync(static_cast<uint32_t>(stream.Size())).get();

        std::vector<uint8_t> buffer(static_cast<size_t>(stream.Size()));
        reader.ReadBytes(buffer);
        return buffer;
    } catch (...) {
        OutputDebugStringW(L"ReadIconToMemory: failed to read notification icon\n");
    }

    return {};
}
