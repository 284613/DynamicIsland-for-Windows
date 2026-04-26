#include "face_core/FaceTemplateStore.h"

#include <windows.h>
#include <dpapi.h>
#include <shlobj.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

#pragma comment(lib, "Crypt32.lib")

namespace fs = std::filesystem;

namespace face_core {

namespace {

constexpr uint32_t kMagic = 0x54534346;  // 'FCST' little-endian
constexpr uint16_t kVersion = 1;
constexpr int kMaxNameLen = 32;

std::wstring DefaultStorePath() {
    wchar_t* localApp = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localApp))) return L"";
    fs::path p(localApp);
    CoTaskMemFree(localApp);
    p /= L"DynamicIsland";
    fs::create_directories(p);
    p /= L"faces.bin";
    return p.wstring();
}

// Append little-endian POD of arbitrary size.
template <typename T>
void Append(std::vector<uint8_t>& buf, const T& v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    buf.insert(buf.end(), p, p + sizeof(T));
}

bool ReadFile(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    BOOL ok = ::ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
    CloseHandle(h);
    return ok && got == out.size();
}

void WriteFile(const std::wstring& path, const std::vector<uint8_t>& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("face store: CreateFile failed");
    DWORD wrote = 0;
    BOOL ok = ::WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &wrote, nullptr);
    CloseHandle(h);
    if (!ok || wrote != data.size()) throw std::runtime_error("face store: write failed");
}

}  // namespace

FaceTemplateStore::FaceTemplateStore() : path_(DefaultStorePath()) {}

bool FaceTemplateStore::Load() {
    templates_.clear();
    std::vector<uint8_t> blob;
    if (!ReadFile(path_, blob) || blob.empty()) return true;  // empty store is fine

    DATA_BLOB in{static_cast<DWORD>(blob.size()), blob.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        return false;
    }
    std::vector<uint8_t> plain(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);

    if (plain.size() < 8) return false;
    size_t off = 0;
    uint32_t magic;
    std::memcpy(&magic, plain.data() + off, 4); off += 4;
    if (magic != kMagic) return false;
    uint16_t version, count;
    std::memcpy(&version, plain.data() + off, 2); off += 2;
    std::memcpy(&count, plain.data() + off, 2); off += 2;
    if (version != kVersion) return false;

    constexpr size_t kEmbBytes = sizeof(float) * kEmbeddingDim;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + 1 > plain.size()) return false;
        uint8_t nlen = plain[off++];
        if (nlen > kMaxNameLen || off + nlen + 1 + kEmbBytes > plain.size()) return false;
        FaceTemplate t;
        t.name.assign(reinterpret_cast<const char*>(plain.data() + off), nlen);
        off += nlen;
        t.angleTag = plain[off++];
        std::memcpy(t.embedding.data(), plain.data() + off, kEmbBytes);
        off += kEmbBytes;
        templates_.push_back(std::move(t));
    }
    return true;
}

void FaceTemplateStore::Save() {
    std::vector<uint8_t> plain;
    Append(plain, kMagic);
    Append(plain, kVersion);
    uint16_t count = static_cast<uint16_t>(templates_.size());
    Append(plain, count);
    for (const auto& t : templates_) {
        uint8_t nlen = static_cast<uint8_t>(std::min<size_t>(t.name.size(), kMaxNameLen));
        plain.push_back(nlen);
        plain.insert(plain.end(), t.name.begin(), t.name.begin() + nlen);
        plain.push_back(t.angleTag);
        const auto* e = reinterpret_cast<const uint8_t*>(t.embedding.data());
        plain.insert(plain.end(), e, e + sizeof(float) * kEmbeddingDim);
    }

    DATA_BLOB in{static_cast<DWORD>(plain.size()), plain.data()};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"DynamicIsland.faces", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        // Retry without LOCAL_MACHINE flag — falls back to user-scoped key.
        if (!CryptProtectData(&in, L"DynamicIsland.faces", nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            throw std::runtime_error("face store: CryptProtectData failed");
        }
    }
    std::vector<uint8_t> blob(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    WriteFile(path_, blob);
}

void FaceTemplateStore::Add(const FaceTemplate& tpl) { templates_.push_back(tpl); }

bool FaceTemplateStore::Remove(const std::string& name) {
    auto it = std::remove_if(templates_.begin(), templates_.end(),
                             [&](const FaceTemplate& t) { return t.name == name; });
    if (it == templates_.end()) return false;
    templates_.erase(it, templates_.end());
    return true;
}

std::vector<std::string> FaceTemplateStore::ListNames() const {
    std::vector<std::string> out;
    for (const auto& t : templates_) {
        if (std::find(out.begin(), out.end(), t.name) == out.end()) out.push_back(t.name);
    }
    return out;
}

size_t FaceTemplateStore::CountForName(const std::string& name) const {
    return static_cast<size_t>(std::count_if(templates_.begin(), templates_.end(),
                                             [&](const FaceTemplate& t) { return t.name == name; }));
}

void FaceTemplateStore::Clear() { templates_.clear(); }

MatchResult FaceTemplateStore::Match(const Embedding& probe, float threshold) const {
    MatchResult r;
    for (const auto& t : templates_) {
        float s = FaceRecognizer::CosineSimilarity(probe, t.embedding);
        if (s > r.score) {
            r.score = s;
            r.name = t.name;
        }
    }
    r.matched = r.score >= threshold && !r.name.empty();
    return r;
}

}  // namespace face_core
