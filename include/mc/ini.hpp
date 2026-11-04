// Tolerant INI parser and writer used by the game adapters.
//
// The format is deliberately small: `[section]` headers, `key=value` pairs,
// `#`/`;` comments, and `\r\n` or `\n` line endings. Parsing never fails on a
// malformed line: the line is counted and skipped, so a hand-edited config can
// still load everything else. Duplicate keys resolve to the last value.
#pragma once

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mc {

namespace ini_detail {

inline bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

inline std::string ToLower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

inline std::string_view Trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && IsSpace(text[begin])) ++begin;
    std::size_t end = text.size();
    while (end > begin && IsSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

// A comment starts a token: `#` or `;` after whitespace. `#` at the start of a
// value (a hex colour) or inside a word is kept.
inline std::string_view StripInlineComment(std::string_view text) {
    for (std::size_t i = 1; i < text.size(); ++i) {
        if ((text[i] == '#' || text[i] == ';') && IsSpace(text[i - 1])) return text.substr(0, i);
    }
    return text;
}

} // namespace ini_detail

struct IniEntry {
    std::string section; // lowercase, empty for keys before the first header
    std::string key;     // lowercase
    std::string value;   // trimmed, original case
};

class IniFile {
public:
    static IniFile Parse(std::string_view text) {
        IniFile out;
        std::string section;
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t end = text.find('\n', pos);
            if (end == std::string_view::npos) end = text.size();
            std::string_view line = ini_detail::Trim(text.substr(pos, end - pos));
            pos = end + 1;
            if (line.empty() || line.front() == '#' || line.front() == ';') continue;
            if (line.front() == '[') {
                const std::size_t close = line.find(']');
                if (close == std::string_view::npos) {
                    ++out.malformed_;
                } else {
                    section = ini_detail::ToLower(ini_detail::Trim(line.substr(1, close - 1)));
                }
                continue;
            }
            const std::size_t eq = line.find('=');
            if (eq == std::string_view::npos) {
                ++out.malformed_;
                continue;
            }
            IniEntry entry;
            entry.section = section;
            entry.key = ini_detail::ToLower(ini_detail::Trim(line.substr(0, eq)));
            const std::string_view value = ini_detail::Trim(line.substr(eq + 1));
            entry.value = std::string(ini_detail::Trim(ini_detail::StripInlineComment(value)));
            if (entry.key.empty()) {
                ++out.malformed_;
                continue;
            }
            out.entries_.push_back(std::move(entry));
        }
        return out;
    }

    // Returns the last value for the key, or nullptr when it is absent.
    [[nodiscard]] const std::string* Get(std::string_view section, std::string_view key) const {
        const std::string wanted_section = ini_detail::ToLower(ini_detail::Trim(section));
        const std::string wanted_key = ini_detail::ToLower(ini_detail::Trim(key));
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (it->section == wanted_section && it->key == wanted_key) return &it->value;
        }
        return nullptr;
    }

    [[nodiscard]] bool Has(std::string_view section, std::string_view key) const { return Get(section, key) != nullptr; }

    [[nodiscard]] const std::vector<IniEntry>& Entries() const { return entries_; }

    [[nodiscard]] std::size_t MalformedLines() const { return malformed_; }

private:
    std::vector<IniEntry> entries_;
    std::size_t malformed_{0};
};

// Accepts 1/0, true/false, yes/no, on/off, enable/enabled, disable/disabled.
[[nodiscard]] inline bool ParseBool(std::string_view text, bool& out) {
    const std::string value = ini_detail::ToLower(ini_detail::Trim(text));
    if (value == "1" || value == "true" || value == "yes" || value == "on" || value == "enable" || value == "enabled") {
        out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off" || value == "disable" || value == "disabled") {
        out = false;
        return true;
    }
    return false;
}

// Base 0, so `0x100A1` works. Rejects trailing junk.
[[nodiscard]] inline bool ParseInt(std::string_view text, long& out) {
    const std::string value(ini_detail::Trim(text));
    if (value.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 0);
    if (errno != 0 || end == value.c_str() || *end != '\0') return false;
    out = parsed;
    return true;
}

// Rejects trailing junk and non-finite values (nan/inf).
[[nodiscard]] inline bool ParseFloat(std::string_view text, float& out) {
    const std::string value(ini_detail::Trim(text));
    if (value.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (errno != 0 || end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
    out = parsed;
    return true;
}

// Returns a copy of `text` with every key in `updates` set inside `section`.
// Comments, blank lines, unrelated keys and other sections are preserved
// byte-for-byte. Missing keys are inserted at the end of the section; a
// missing section is appended. `section` is empty for global (headerless) keys.
[[nodiscard]] inline std::string IniSetValues(std::string_view text,
                                              std::string_view section,
                                              const std::vector<std::pair<std::string, std::string>>& updates) {
    const std::string target = ini_detail::ToLower(ini_detail::Trim(section));
    const bool crlf = text.find("\r\n") != std::string_view::npos;
    const std::string newline = crlf ? "\r\n" : "\n";

    struct Line {
        std::string text;    // without the terminator
        std::string ending;  // "\r\n", "\n" or "" for the last unterminated line
        std::string section; // section this line belongs to
        std::string key;     // lowercase key when the line is `key=value`
        bool is_header{false};
    };

    std::vector<Line> lines;
    std::string current;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string line(text.substr(pos, end - pos));
        std::string ending = end < text.size() ? "\n" : "";
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            if (!ending.empty()) ending = "\r\n";
        }
        Line parsed;
        parsed.text = line;
        parsed.ending = ending;
        parsed.section = current;
        const std::string_view trimmed = ini_detail::Trim(line);
        if (!trimmed.empty() && trimmed.front() == '[') {
            const std::size_t close = trimmed.find(']');
            if (close != std::string_view::npos) {
                current = ini_detail::ToLower(ini_detail::Trim(trimmed.substr(1, close - 1)));
                parsed.section = current;
                parsed.is_header = true;
            }
        } else if (!trimmed.empty() && trimmed.front() != '#' && trimmed.front() != ';') {
            const std::size_t eq = trimmed.find('=');
            if (eq != std::string_view::npos) parsed.key = ini_detail::ToLower(ini_detail::Trim(trimmed.substr(0, eq)));
        }
        lines.push_back(std::move(parsed));
        pos = end + 1;
    }

    std::vector<bool> seen(updates.size(), false);
    std::size_t section_header = lines.size();
    std::size_t section_end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].section == target) {
            if (lines[i].is_header && section_header == lines.size()) section_header = i;
            section_end = i + 1;
        }
    }

    const bool create_section = !target.empty() && section_header == lines.size();
    std::size_t insert_line = lines.size();
    if (!create_section && !target.empty()) {
        // Insert after the section's last non-blank line, before the next header.
        insert_line = section_header + 1;
        for (std::size_t i = section_header + 1; i < section_end; ++i) {
            if (!ini_detail::Trim(lines[i].text).empty()) insert_line = i + 1;
        }
    }

    auto append_updates = [&](std::string& out) {
        for (std::size_t u = 0; u < updates.size(); ++u) {
            if (seen[u]) continue;
            out += updates[u].first;
            out += '=';
            out += updates[u].second;
            out += newline;
        }
    };

    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i == insert_line) append_updates(out);
        bool replaced = false;
        if (lines[i].section == target && !lines[i].key.empty()) {
            for (std::size_t u = 0; u < updates.size(); ++u) {
                if (lines[i].key == ini_detail::ToLower(updates[u].first)) {
                    out += updates[u].first;
                    out += '=';
                    out += updates[u].second;
                    out += lines[i].ending.empty() ? newline : lines[i].ending;
                    seen[u] = true;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) {
            out += lines[i].text;
            out += lines[i].ending;
        }
    }
    if (insert_line == lines.size()) {
        if (!out.empty() && out.back() != '\n') out += newline;
        if (create_section) {
            out += '[';
            out += target;
            out += ']';
            out += newline;
        }
        append_updates(out);
    }
    return out;
}

} // namespace mc
