#pragma once
#include <windows.h>
#include <oleidl.h>
#include <shellapi.h>
#include "Messages.h"

#pragma comment(lib, "ole32.lib")

class DropManager : public IDropTarget {
private:
    ULONG m_ref = 1;
    HWND m_hwnd;

public:
    DropManager(HWND hwnd) : m_hwnd(hwnd) {}

    // --- IUnknown 接口 ---
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0) delete this;
        return ref;
    }

    // --- IDropTarget 接口 ---
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        *pdwEffect = DROPEFFECT_MOVE;
        PostMessage(m_hwnd, WM_DRAG_ENTER, 0, 0); // 通知 UI：文件悬停进来了！
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        *pdwEffect = DROPEFFECT_MOVE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        PostMessage(m_hwnd, WM_DRAG_LEAVE, 0, 0); // 通知 UI：文件移走了！
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        *pdwEffect = DROPEFFECT_MOVE;
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM stg;

        // 提取拖入的文件路径
        if (SUCCEEDED(pDataObj->GetData(&fmt, &stg))) {
            // 直接把 hDrop 传给主线程，不要在这里解锁，让主线程处理完再调用 DragFinish
            HDROP hDrop = (HDROP)stg.hGlobal;
            PostMessage(m_hwnd, WM_DROP_FILE, (WPARAM)hDrop, 0);
            // 注意：这里不要调用 GlobalUnlock 和 ReleaseStgMedium，主线程会处理
        }
        PostMessage(m_hwnd, WM_DRAG_LEAVE, 0, 0); // 重置 UI 状态
        return S_OK;
    }
};


