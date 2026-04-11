//NotificationMonitor.cpp
#include "NotificationMonitor.h"
#include "Messages.h"
#include "EventBus.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <fstream>
#include <shellapi.h>
#include <guiddef.h>
#include <winrt/Windows.Foundation.Collections.h>
#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::UI::Notifications;
using namespace Windows::UI::Notifications::Management;
using namespace Windows::Storage::Streams;

NotificationMonitor::NotificationMonitor() {}

NotificationMonitor::~NotificationMonitor() {
	m_running = false;
	if (m_thread.joinable()) m_thread.join();
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

void NotificationMonitor::Worker() {
	winrt::init_apartment();

	try {
		UserNotificationListener listener = UserNotificationListener::Current();
		while (m_running) {
			try {
				auto access = listener.RequestAccessAsync().get();
				if (access == UserNotificationListenerAccessStatus::Allowed) {
					auto notifications = listener.GetNotificationsAsync(NotificationKinds::Toast).get();

					for (auto const& notif : notifications) {
						uint32_t id = notif.Id();

						if (m_processedNotifs.find(id) != m_processedNotifs.end()) continue;
						m_processedNotifs.insert(id);

						auto appInfo = notif.AppInfo();
						if (!appInfo) continue;

						std::wstring appName = appInfo.DisplayInfo().DisplayName().c_str();

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

						if (isAllowed) {
							std::wstring title = appName;
							std::wstring body = L"收到新消息";
							std::vector<uint8_t>* iconData = nullptr;

							try {
								// 尝试提取通知文本内容
								auto notification = notif.Notification();
								auto toast = notification.try_as<winrt::Windows::UI::Notifications::ToastNotification>();
								if (toast) {
									auto xml = toast.Content();
									if (xml) {
										auto textNodes = xml.GetElementsByTagName(L"text");
										if (textNodes.Size() >= 1) title = textNodes.GetAt(0).InnerText().c_str();
										if (textNodes.Size() >= 2) body = textNodes.GetAt(1).InnerText().c_str();
									}
								}
							}
							catch (...) {}

							try {
								// 尝试提取应用图标
								auto appInfoObj = notif.AppInfo();
								if (appInfoObj) {
									auto displayInfo = appInfoObj.DisplayInfo();
									if (displayInfo) {
										auto logo = displayInfo.GetLogo(Windows::Foundation::Size(64, 64));
										if (logo) {
											iconData = ReadIconToMemory(logo);
										}
									}
								}
							}
							catch (...) {}

							std::wstring combined = title;
							if (!body.empty() && body != L"收到新消息") {
							    combined += L": ";
							    combined += body;
							}

							AlertInfo info{ 3, appName, combined, iconData ? L"" : ExtractIconFromExe(appName), iconData ? *iconData : std::vector<uint8_t>() };
							EventBus::GetInstance().PublishNotificationArrived(info);
						}
					}
				}
			}
			catch (...) {}
			Sleep(1500);
		}
	}
	catch (...) {}
}

std::vector<uint8_t>* NotificationMonitor::ReadIconToMemory(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& icon) {
	if (!icon) return nullptr;

	try {
		auto stream = icon.OpenReadAsync().get();
		DataReader reader{ stream };
		reader.LoadAsync(static_cast<uint32_t>(stream.Size())).get();

		std::vector<uint8_t>* buffer = new std::vector<uint8_t>(stream.Size());
		reader.ReadBytes(*buffer);

		return buffer;
	}
	catch (...) {
		OutputDebugStringW(L"ReadIconToMemory: 读取流异常\n");
	}
	return nullptr;
}
