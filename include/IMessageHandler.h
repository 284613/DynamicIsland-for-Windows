// IMessageHandler.h
#pragma once
#include <windows.h>

class IMessageHandler {
public:
    virtual ~IMessageHandler() {}
    virtual LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) = 0;
};

