#pragma once

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objidl.h>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

struct FileStashItem {
    std::wstring stagedPath;
    std::wstring originalSourcePath;
    std::wstring displayName;
    std::wstring extension;
    uintmax_t sizeBytes = 0;
    FILETIME storedAt{};
};

namespace FileStashDetail {

inline std::filesystem::path GetStashDirectory() {
    wchar_t localAppData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        std::filesystem::path dir = std::filesystem::path(localAppData) / L"DynamicIsland" / L"FileStash";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }
    std::filesystem::path dir = std::filesystem::temp_directory_path() / L"DynamicIsland_FileStash";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

inline std::filesystem::path MakeUniqueDestination(const std::filesystem::path& directory, const std::filesystem::path& sourcePath) {
    std::filesystem::path stem = sourcePath.stem();
    std::filesystem::path ext = sourcePath.extension();
    std::filesystem::path candidate = directory / sourcePath.filename();
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = directory / (stem.wstring() + L" (" + std::to_wstring(suffix++) + L")" + ext.wstring());
    }
    return candidate;
}

inline bool MovePathRobust(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code ec;
    std::filesystem::rename(source, destination, ec);
    if (!ec) return true;

    ec.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return false;
    std::filesystem::remove(source, ec);
    return !ec;
}

inline HGLOBAL CreateHDropData(const std::wstring& filePath) {
    const SIZE_T bytes = sizeof(DROPFILES) + ((filePath.size() + 2) * sizeof(wchar_t));
    HGLOBAL hMem = GlobalAlloc(GHND | GMEM_SHARE, bytes);
    if (!hMem) return nullptr;

    auto* dropFiles = static_cast<DROPFILES*>(GlobalLock(hMem));
    if (!dropFiles) {
        GlobalFree(hMem);
        return nullptr;
    }

    dropFiles->pFiles = sizeof(DROPFILES);
    dropFiles->fWide = TRUE;
    auto* pathBuffer = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(dropFiles) + sizeof(DROPFILES));
    wcscpy_s(pathBuffer, filePath.size() + 2, filePath.c_str());
    pathBuffer[filePath.size() + 1] = L'\0';
    GlobalUnlock(hMem);
    return hMem;
}

class FileDragDataObject final : public IDataObject {
public:
    explicit FileDragDataObject(std::wstring filePath)
        : m_filePath(std::move(filePath)),
          m_preferredEffectFormat(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT))) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppvObject = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
        if (ref == 0) delete this;
        return ref;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* formatEtc, STGMEDIUM* medium) override {
        if (!formatEtc || !medium) return E_INVALIDARG;
        medium->pUnkForRelease = nullptr;

        if (formatEtc->cfFormat == CF_HDROP && (formatEtc->tymed & TYMED_HGLOBAL)) {
            HGLOBAL hDrop = CreateHDropData(m_filePath);
            if (!hDrop) return E_OUTOFMEMORY;
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = hDrop;
            return S_OK;
        }

        if (formatEtc->cfFormat == m_preferredEffectFormat && (formatEtc->tymed & TYMED_HGLOBAL)) {
            HGLOBAL hEffect = GlobalAlloc(GHND | GMEM_SHARE, sizeof(DWORD));
            if (!hEffect) return E_OUTOFMEMORY;
            auto* effect = static_cast<DWORD*>(GlobalLock(hEffect));
            if (!effect) {
                GlobalFree(hEffect);
                return E_OUTOFMEMORY;
            }
            *effect = DROPEFFECT_MOVE;
            GlobalUnlock(hEffect);
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = hEffect;
            return S_OK;
        }

        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* formatEtc) override {
        if (!formatEtc) return E_INVALIDARG;
        if (formatEtc->cfFormat == CF_HDROP && (formatEtc->tymed & TYMED_HGLOBAL)) return S_OK;
        if (formatEtc->cfFormat == m_preferredEffectFormat && (formatEtc->tymed & TYMED_HGLOBAL)) return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* outFormatEtc) override {
        if (outFormatEtc) outFormatEtc->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** enumFormatEtc) override {
        if (!enumFormatEtc) return E_POINTER;
        *enumFormatEtc = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;

        FORMATETC formats[2] = {};
        formats[0].cfFormat = CF_HDROP;
        formats[0].dwAspect = DVASPECT_CONTENT;
        formats[0].lindex = -1;
        formats[0].tymed = TYMED_HGLOBAL;
        formats[1].cfFormat = m_preferredEffectFormat;
        formats[1].dwAspect = DVASPECT_CONTENT;
        formats[1].lindex = -1;
        formats[1].tymed = TYMED_HGLOBAL;
        return SHCreateStdEnumFmtEtc(2, formats, enumFormatEtc);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    ~FileDragDataObject() = default;

    long m_refCount = 1;
    std::wstring m_filePath;
    CLIPFORMAT m_preferredEffectFormat = 0;
};

class FileDragDropSource final : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppvObject = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
        if (ref == 0) delete this;
        return ref;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override {
        if (escapePressed) return DRAGDROP_S_CANCEL;
        if ((keyState & MK_LBUTTON) == 0) return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    ~FileDragDropSource() = default;
    long m_refCount = 1;
};

inline bool ShellExecuteVerb(const std::wstring& filePath, const wchar_t* verb) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_INVOKEIDLIST;
    sei.lpVerb = verb;
    sei.lpFile = filePath.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) == TRUE;
}

} // namespace FileStashDetail

class FileStashStore {
public:
    static constexpr size_t kMaxItems = 5;

    const std::vector<FileStashItem>& Items() const { return m_items; }
    bool HasItems() const { return !m_items.empty(); }
    size_t Count() const { return m_items.size(); }

    bool AddPaths(const std::vector<std::wstring>& sourcePaths, std::wstring* errorMessage = nullptr) {
        for (const auto& sourcePathStr : sourcePaths) {
            if (m_items.size() >= kMaxItems) {
                if (errorMessage) *errorMessage = L"最多暂存 5 个文件";
                return false;
            }

            std::filesystem::path sourcePath(sourcePathStr);
            std::error_code ec;
            if (!std::filesystem::exists(sourcePath, ec) || std::filesystem::is_directory(sourcePath, ec)) {
                continue;
            }

            std::filesystem::path stashDir = FileStashDetail::GetStashDirectory();
            std::filesystem::path destination = FileStashDetail::MakeUniqueDestination(stashDir, sourcePath);
            if (!FileStashDetail::MovePathRobust(sourcePath, destination)) {
                if (errorMessage) *errorMessage = L"文件暂存失败";
                return false;
            }

            FileStashItem item;
            item.stagedPath = destination.wstring();
            item.originalSourcePath = sourcePath.wstring();
            item.displayName = destination.filename().wstring();
            item.extension = destination.extension().wstring();
            item.sizeBytes = std::filesystem::file_size(destination, ec);
            GetSystemTimeAsFileTime(&item.storedAt);
            m_items.push_back(std::move(item));
        }
        return true;
    }

    bool RemoveIndex(size_t index) {
        if (index >= m_items.size()) return false;
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(m_items[index].stagedPath), ec);
        m_items.erase(m_items.begin() + index);
        return true;
    }

    bool PreviewIndex(size_t index) const {
        if (index >= m_items.size()) return false;
        return FileStashDetail::ShellExecuteVerb(m_items[index].stagedPath, L"preview");
    }

    bool OpenIndex(size_t index) const {
        if (index >= m_items.size()) return false;
        return FileStashDetail::ShellExecuteVerb(m_items[index].stagedPath, L"open");
    }

    bool BeginMoveDrag(HWND owner, size_t index, bool& moveCompleted) const {
        moveCompleted = false;
        if (index >= m_items.size()) return false;

        auto* dataObject = new FileStashDetail::FileDragDataObject(m_items[index].stagedPath);
        auto* dropSource = new FileStashDetail::FileDragDropSource();
        DWORD effect = DROPEFFECT_NONE;
        HRESULT hr = DoDragDrop(dataObject, dropSource, DROPEFFECT_MOVE, &effect);
        dataObject->Release();
        dropSource->Release();
        moveCompleted = SUCCEEDED(hr) && (effect & DROPEFFECT_MOVE);
        return SUCCEEDED(hr);
    }

private:
    std::vector<FileStashItem> m_items;
};
