#pragma once
// The launcher's updates without Windows calls: GitHub's answer about the newest
// release (JSON), what it offers, and which files of Spidy's folder an update
// replaces, adds and retires. tests/launcher_tests.cpp checks them.
#include "launcher_text.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cwctype>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spidy::launcher {

// "1.2.3", the versions Spidy's release tags carry (after their "v").
inline bool releaseVersion(std::string_view text) {
    int parts = 0;
    for (size_t i = 0; i < text.size(); ++parts) {
        const size_t start = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9')
            ++i;
        if (i == start || i - start > 5)
            return false;
        if (i < text.size() && (text[i++] != '.' || i == text.size()))
            return false;
    }
    return parts == 3;
}

inline bool newerVersion(std::string_view candidate, std::string_view current) {
    return parseVersion(candidate) > parseVersion(current);
}

// The newest release on GitHub, as far as an update needs it.
struct Release {
    std::string version;              // "0.2.7"
    std::string page;                 // its page on GitHub
    std::string zipName, zipUrl;      // the player zip, Spidy-<version>-win64.zip
    uint64_t zipSize{};
    std::string sha256;               // the zip's, 64 lower-case hex digits
    std::vector<std::string> changes; // its notes' list of changes
};

// The release in GitHub's answer to /repos/<owner>/<repo>/releases/latest; nullopt, with `error`
// saying why, when it lacks a player zip or that zip's SHA-256. The SHA-256 is the asset's
// digest (GitHub's own), else the one the release notes give (.github/workflows/release.yml).
inline std::optional<Release> parseRelease(std::string_view answer, std::string& error) {
    const auto root = parseJson(answer);
    if (!root || root->type != Json::Type::object) {
        error = "GitHub's answer could not be read.";
        return std::nullopt;
    }
    const std::string tag = root->text("tag_name");
    Release release;
    release.version = tag.substr(!tag.empty() && (tag[0] == 'v' || tag[0] == 'V') ? 1 : 0);
    if (!releaseVersion(release.version)) {
        error = "The newest release has no version number (its tag is \"" + tag + "\").";
        return std::nullopt;
    }
    release.page = root->text("html_url");
    release.zipName = "Spidy-" + release.version + "-win64.zip";
    const auto hex = [](std::string text) -> std::string {
        for (char& c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == std::string::npos ? text : "";
    };
    if (const Json* assets = root->get("assets"))
        for (const Json& asset : assets->items) {
            if (asset.text("name") != release.zipName)
                continue;
            release.zipUrl = asset.text("browser_download_url");
            if (const Json* size = asset.get("size"); size && size->type == Json::Type::number && size->number > 0)
                release.zipSize = static_cast<uint64_t>(size->number);
            const std::string digest = asset.text("digest");
            if (digest.rfind("sha256:", 0) == 0)
                release.sha256 = hex(digest.substr(7));
        }
    if (release.zipUrl.rfind("https://", 0) != 0 || !release.zipSize) {
        error = "Spidy " + release.version + " has no " + release.zipName + " to download.";
        return std::nullopt;
    }
    const std::string body = root->text("body");
    if (release.sha256.empty()) {
        const std::string label = "SHA-256 of " + release.zipName + ": `";
        if (const size_t at = body.find(label); at != std::string::npos)
            release.sha256 = hex(body.substr(at + label.size(), 64));
    }
    if (release.sha256.empty()) {
        error = "Spidy " + release.version + " gives no SHA-256 for its zip, so it cannot be checked.";
        return std::nullopt;
    }
    for (size_t at = 0; at < body.size();) {
        const size_t end = std::min(body.find('\n', at), body.size());
        std::string line = body.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("- ", 0) == 0 && line.size() > 2)
            release.changes.push_back(line.substr(2));
    }
    return release;
}

// What an update does to the files of Spidy's folder. Paths are relative to the folder, with
// '/' between their parts.
struct UpdatePlan {
    std::vector<std::wstring> replace; // in the folder and the package: the package's takes its place
    std::vector<std::wstring> add;     // only in the package
    std::vector<std::wstring> retire;  // only in the folder, in a folder the package fills (tools, python...)
};

// `installed`: the folder's files, `package`: the new zip's. Files beside the launcher, reports\ and
// folders the package has no files in are never retired: they may be the player's.
inline UpdatePlan updatePlan(const std::vector<std::wstring>& installed, const std::vector<std::wstring>& package) {
    const auto lower = [](std::wstring text) {
        for (wchar_t& c : text)
            c = static_cast<wchar_t>(std::towlower(c));
        return text;
    };
    const auto parent = [](const std::wstring& path) {
        const size_t slash = path.rfind(L'/');
        return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    };
    std::set<std::wstring> have, ship, folders;
    for (const auto& path : installed)
        have.insert(lower(path));
    for (const auto& path : package) {
        ship.insert(lower(path));
        if (const auto folder = lower(parent(path)); !folder.empty())
            folders.insert(folder);
    }
    UpdatePlan plan;
    for (const auto& path : package)
        (have.count(lower(path)) ? plan.replace : plan.add).push_back(path);
    for (const auto& path : installed) {
        const std::wstring key = lower(path);
        if (!ship.count(key) && folders.count(lower(parent(path))) && key.rfind(L"reports/", 0) != 0)
            plan.retire.push_back(path);
    }
    return plan;
}

} // namespace spidy::launcher
