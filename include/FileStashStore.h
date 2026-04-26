#pragma once

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objidl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowsapp.lib")

struct FileStashItem {
    std::wstring stagedPath;
    std::wstring originalSourcePath;
    std::wstring displayName;
    std::wstring extension;
    uintmax_t sizeBytes = 0;
    FILETIME storedAt{};
};

namespace FileStashDetail {

inline std::filesystem::path GetAppDirectory() {
    wchar_t localAppData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        std::filesystem::path dir = std::filesystem::path(localAppData) / L"DynamicIsland";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    std::filesystem::path fallback = std::filesystem::temp_directory_path() / L"DynamicIsland";
    std::error_code ec;
    std::filesystem::create_directories(fallback, ec);
    return fallback;
}

inline std::filesystem::path GetStashDirectory() {
    std::filesystem::path dir = GetAppDirectory() / L"FileStash";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

inline std::filesystem::path GetIndexPath() {
    return GetAppDirectory() / L"file_stash.json";
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
    if (!ec) {
        return true;
    }

    ec.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    std::filesystem::remove(source, ec);
    return !ec;
}

inline std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }

    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }

    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), len, nullptr, nullptr);
    return utf8;
}

inline std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }

    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }

    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

inline std::wstring EscapeJson(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size() + 8);
    for (wchar_t ch : value) {
        switch (ch) {
        case L'\\': escaped += L"\\\\"; break;
        case L'"': escaped += L"\\\""; break;
        case L'\n': escaped += L"\\n"; break;
        case L'\r': escaped += L"\\r"; break;
        case L'\t': escaped += L"\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

inline uint64_t FileTimeToUInt64(const FILETIME& fileTime) {
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return value.QuadPart;
}

inline FILETIME UInt64ToFileTime(uint64_t rawValue) {
    ULARGE_INTEGER value{};
    value.QuadPart = rawValue;
    FILETIME fileTime{};
    fileTime.dwLowDateTime = value.LowPart;
    fileTime.dwHighDateTime = value.HighPart;
    return fileTime;
}

inline HGLOBAL CreateHDropData(const std::wstring& filePath) {
    const SIZE_T bytes = sizeof(DROPFILES) + ((filePath.size() + 2) * sizeof(wchar_t));
    HGLOBAL hMem = GlobalAlloc(GHND | GMEM_SHARE, bytes);
    if (!hMem) {
        return nullptr;
    }

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
    static constexpr size_t kDefaultMaxItems = 5;
    static inline size_t s_maxItems = kDefaultMaxItems;

    static size_t GetMaxItems() { return s_maxItems; }
    static void SetGlobalMaxItems(size_t maxItems) { s_maxItems = (std::max)(size_t(1), maxItems); }

    const std::vector<FileStashItem>& Items() const { return m_items; }
    bool HasItems() const { return !m_items.empty(); }
    size_t Count() const { return m_items.size(); }
    void SetMaxItems(size_t maxItems) { SetGlobalMaxItems(maxItems); }
    std::filesystem::path GetStoragePath() const { return FileStashDetail::GetIndexPath(); }

    bool Load() {
        m_items.clear();

        const std::filesystem::path path = GetStoragePath();
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            return true;
        }

        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.empty()) {
            return true;
        }

        bool removedMissingItems = false;
        try {
            const auto root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::hstring(FileStashDetail::Utf8ToWide(bytes)));
            if (root.HasKey(L"items")) {
                const auto items = root.GetNamedArray(L"items");
                for (const auto& value : items) {
                    const auto object = value.GetObject();
                    FileStashItem item;
                    if (object.HasKey(L"stagedPath")) item.stagedPath = object.GetNamedString(L"stagedPath").c_str();
                    if (object.HasKey(L"originalSourcePath")) item.originalSourcePath = object.GetNamedString(L"originalSourcePath").c_str();
                    if (object.HasKey(L"displayName")) item.displayName = object.GetNamedString(L"displayName").c_str();
                    if (object.HasKey(L"extension")) item.extension = object.GetNamedString(L"extension").c_str();
                    if (object.HasKey(L"sizeBytes")) item.sizeBytes = static_cast<uintmax_t>(object.GetNamedValue(L"sizeBytes").GetNumber());
                    if (object.HasKey(L"storedAt")) item.storedAt = FileStashDetail::UInt64ToFileTime(static_cast<uint64_t>(object.GetNamedValue(L"storedAt").GetNumber()));

                    if (item.stagedPath.empty()) {
                        continue;
                    }

                    std::error_code ec;
                    const std::filesystem::path stagedPath(item.stagedPath);
                    if (!std::filesystem::exists(stagedPath, ec) || std::filesystem::is_directory(stagedPath, ec)) {
                        removedMissingItems = true;
                        continue;
                    }

                    if (item.displayName.empty()) {
                        item.displayName = stagedPath.filename().wstring();
                    }
                    if (item.extension.empty()) {
                        item.extension = stagedPath.extension().wstring();
                    }
                    if (item.sizeBytes == 0) {
                        item.sizeBytes = std::filesystem::file_size(stagedPath, ec);
                    }

                    m_items.push_back(std::move(item));
                }
            }
        } catch (...) {
            m_items.clear();
            return false;
        }

        if (removedMissingItems) {
            Save();
        }
        return true;
    }

    bool Save() const {
        std::wostringstream out;
        out << L"{\n";
        out << L"  \"version\": 1,\n";
        out << L"  \"items\": [\n";
        for (size_t i = 0; i < m_items.size(); ++i) {
            const FileStashItem& item = m_items[i];
            out << L"    {\n";
            out << L"      \"stagedPath\": \"" << FileStashDetail::EscapeJson(item.stagedPath) << L"\",\n";
            out << L"      \"originalSourcePath\": \"" << FileStashDetail::EscapeJson(item.originalSourcePath) << L"\",\n";
            out << L"      \"displayName\": \"" << FileStashDetail::EscapeJson(item.displayName) << L"\",\n";
            out << L"      \"extension\": \"" << FileStashDetail::EscapeJson(item.extension) << L"\",\n";
            out << L"      \"sizeBytes\": " << item.sizeBytes << L",\n";
            out << L"      \"storedAt\": " << FileStashDetail::FileTimeToUInt64(item.storedAt) << L"\n";
            out << L"    }";
            if (i + 1 < m_items.size()) {
                out << L",";
            }
            out << L"\n";
        }
        out << L"  ]\n";
        out << L"}\n";

        const std::filesystem::path path = GetStoragePath();
        std::filesystem::create_directories(path.parent_path());
        const std::filesystem::path tempPath = path.wstring() + L".tmp";
        {
            std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                return false;
            }
            const std::string utf8 = FileStashDetail::WideToUtf8(out.str());
            file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
            if (!file.good()) {
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tempPath, path, ec);
            if (ec) {
                return false;
            }
        }
        return true;
    }

    bool AddPaths(const std::vector<std::wstring>& sourcePaths, std::wstring* errorMessage = nullptr) {
        bool addedAny = false;
        for (const auto& sourcePathStr : sourcePaths) {
            if (m_items.size() >= GetMaxItems()) {
                if (errorMessage) *errorMessage = L"File stash limit reached: " + std::to_wstring(GetMaxItems());
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
                if (errorMessage) *errorMessage = L"Failed to stash file";
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
            addedAny = true;
        }

        return !addedAny || Save();
    }

    bool RemoveIndex(size_t index) {
        if (index >= m_items.size()) return false;
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(m_items[index].stagedPath), ec);
        m_items.erase(m_items.begin() + index);
        return Save();
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
        (void)owner;
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
