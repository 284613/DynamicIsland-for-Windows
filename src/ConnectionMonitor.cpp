//ConnectionMonitor.cpp
#include "ConnectionMonitor.h"
#include "Messages.h"
#include "EventBus.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace winrt::Windows::Networking::Connectivity;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Enumeration;

ConnectionMonitor::ConnectionMonitor() {}

ConnectionMonitor::~ConnectionMonitor() {
	m_running = false;
	if (m_thread.joinable()) m_thread.join();
}

void ConnectionMonitor::Initialize(HWND hwnd) {
	m_hwnd = hwnd;
	m_running = true;
	m_thread = std::thread(&ConnectionMonitor::Worker, this);
}

void ConnectionMonitor::Worker() {
	winrt::init_apartment();

	// 初始化获取当前连接，避免冷启动时乱弹窗
	try {
		auto profile = NetworkInformation::GetInternetConnectionProfile();
		if (profile && profile.IsWlanConnectionProfile()) {
			m_lastWifiName = profile.ProfileName().c_str();
		}

		auto selector = BluetoothDevice::GetDeviceSelectorFromConnectionStatus(BluetoothConnectionStatus::Connected);
		auto devices = DeviceInformation::FindAllAsync(selector).get();
		for (uint32_t i = 0; i < devices.Size(); ++i) {
			m_connectedBtDevices.insert(devices.GetAt(i).Id().c_str());
		}
	}
	catch (...) {}

	while (m_running) {
		try {
			// Wi-Fi 检查
			auto profile = NetworkInformation::GetInternetConnectionProfile();
			std::wstring currentWifi = L"";
			if (profile && profile.IsWlanConnectionProfile()) {
				currentWifi = profile.ProfileName().c_str(); // 获取具体的WiFi名称
			}

			if (!currentWifi.empty() && currentWifi != m_lastWifiName) {
				// 动态分配内存发给主线程，主线程负责 delete
				AlertInfo info{ 1, currentWifi, L"Wi-Fi" };
				EventBus::GetInstance().PublishNotificationArrived(info);
			}
			m_lastWifiName = currentWifi;

			// 蓝牙检查
			auto selector = BluetoothDevice::GetDeviceSelectorFromConnectionStatus(BluetoothConnectionStatus::Connected);
			auto devices = DeviceInformation::FindAllAsync(selector).get();
			std::set<std::wstring> currentBtDevices;

			for (uint32_t i = 0; i < devices.Size(); ++i) {
				auto dev = devices.GetAt(i);
				std::wstring id = dev.Id().c_str();
				std::wstring name = dev.Name().c_str(); // 获取蓝牙设备名称
				currentBtDevices.insert(id);

				if (m_connectedBtDevices.find(id) == m_connectedBtDevices.end()) {
					std::wstring typeStr = L"蓝牙设备";
					try {
						auto btDev = BluetoothDevice::FromIdAsync(id).get();
						if (btDev) {
							auto major = btDev.ClassOfDevice().MajorClass();
							if (major == BluetoothMajorClass::AudioVideo) typeStr = L"耳机/音响";
							else if (major == BluetoothMajorClass::Peripheral) typeStr = L"键盘/鼠标";
							else if (major == BluetoothMajorClass::Computer) typeStr = L"电脑";
							else if (major == BluetoothMajorClass::Phone) typeStr = L"手机";
							else if (major == BluetoothMajorClass::Wearable) typeStr = L"穿戴设备";
						}
					}
					catch (...) {}

					AlertInfo info{ 2, name, typeStr };
					EventBus::GetInstance().PublishNotificationArrived(info);
				}
			}
			m_connectedBtDevices = currentBtDevices;
		}
		catch (...) {}
		Sleep(2000);
	}
}

