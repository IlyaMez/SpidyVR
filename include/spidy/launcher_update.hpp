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

// A JSON value: enough of JSON to read GitHub's answers.
struct Json {
    enum class Type { null, boolean, number, string, array, object };
    Type type = Type::null;
    bool boolean{};
    double number{};
    std::string string;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    // An object's member named `key`; nullptr when there is none.
    const Json* get(std::string_view key) const {
        for (const auto& [name, value] : members)
            if (name == key)
                return &value;
        return nullptr;
    }
    // A string member's text; empty for anything else.
    std::string text(std::string_view key) const {
        const Json* value = get(key);
        return value && value->type == Type::string ? value->string : std::string();
    }
};

namespace detail {
class JsonReader {
public:
    explicit JsonReader(std::string_view text) : s_(text) {}

    std::optional<Json> read() {
        Json value;
        if (!this->value(value, 0))
            return std::nullopt;
        space();
        return i_ == s_.size() ? std::optional<Json>(std::move(value)) : std::nullopt;
    }

private:
    void space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r'))
            ++i_;
    }

    bool word(std::string_view text) {
        if (s_.substr(i_, text.size()) != text)
            return false;
        i_ += text.size();
        return true;
    }

    bool hex4(size_t at, unsigned& code) const {
        if (at + 4 > s_.size())
            return false;
        const auto [end, error] = std::from_chars(s_.data() + at, s_.data() + at + 4, code, 16);
        return error == std::errc() && end == s_.data() + at + 4;
    }

    static void utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | code >> 6);
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | code >> 12);
            out += static_cast<char>(0x80 | (code >> 6 & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | code >> 18);
            out += static_cast<char>(0x80 | (code >> 12 & 0x3F));
            out += static_cast<char>(0x80 | (code >> 6 & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool string(std::string& out) {
        if (!word("\""))
            return false;
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return false;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size())
                return false;
            switch (const char escape = s_[i_++]) {
            case '"':
            case '\\':
            case '/': out += escape; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code{};
                if (!hex4(i_, code))
                    return false;
                i_ += 4;
                // A character beyond the first 64K is two escapes, a surrogate pair; half of one is U+FFFD.
                if (code >= 0xD800 && code < 0xDC00) {
                    unsigned low{};
                    if (s_.substr(i_, 2) == "\\u" && hex4(i_ + 2, low) && low >= 0xDC00 && low < 0xE000) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        i_ += 6;
                    } else {
                        code = 0xFFFD;
                    }
                } else if (code >= 0xDC00 && code < 0xE000) {
                    code = 0xFFFD;
                }
                utf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool value(Json& out, int depth) {
        // GitHub nests a few levels; a deeper text is not an answer of theirs.
        if (depth > 64)
            return false;
        space();
        if (i_ >= s_.size())
            return false;
        const char c = s_[i_];
        if (c == '{') {
            ++i_;
            out.type = Json::Type::object;
            space();
            if (word("}"))
                return true;
            for (;;) {
                space();
                std::string name;
                if (!string(name))
                    return false;
                space();
                if (!word(":"))
                    return false;
                Json member;
                if (!value(member, depth + 1))
                    return false;
                out.members.emplace_back(std::move(name), std::move(member));
                space();
                if (word("}"))
                    return true;
                if (!word(","))
                    return false;
            }
        }
        if (c == '[') {
            ++i_;
            out.type = Json::Type::array;
            space();
            if (word("]"))
                return true;
            for (;;) {
                Json item;
                if (!value(item, depth + 1))
                    return false;
                out.items.push_back(std::move(item));
                space();
                if (word("]"))
                    return true;
                if (!word(","))
                    return false;
            }
        }
        if (c == '"') {
            out.type = Json::Type::string;
            return string(out.string);
        }
        if (word("true")) {
            out.type = Json::Type::boolean;
            out.boolean = true;
            return true;
        }
        if (word("false")) {
            out.type = Json::Type::boolean;
            return true;
        }
        if (word("null"))
            return true;
        size_t end = i_;
        while (end < s_.size() && std::string_view("+-0123456789.eE").find(s_[end]) != std::string_view::npos)
            ++end;
        const char* first = s_.data() + i_;
        const auto [stop, error] = std::from_chars(first, s_.data() + end, out.number);
        if (end == i_ || error != std::errc() || stop != s_.data() + end)
            return false;
        out.type = Json::Type::number;
        i_ = end;
        return true;
    }

    std::string_view s_;
    size_t i_ = 0;
};
} // namespace detail

inline std::optional<Json> parseJson(std::string_view text) { return detail::JsonReader(text).read(); }

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
