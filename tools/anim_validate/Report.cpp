#include "Report.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "anim/integration/JsonWriter.h"

namespace anim::validate {
namespace {

const char* severityName(Severity severity) {
    switch (severity) {
        case Severity::Error:
            return "error";
        case Severity::Warning:
            return "warning";
        case Severity::Info:
            return "info";
    }
    return "error";
}

bool issueLess(const Issue& a, const Issue& b) {
    if (a.severity != b.severity) {
        return static_cast<int>(a.severity) < static_cast<int>(b.severity);
    }
    if (a.file != b.file) return a.file < b.file;
    if (a.clip != b.clip) return a.clip < b.clip;
    if (a.path != b.path) return a.path < b.path;
    if (a.code != b.code) return a.code < b.code;
    return a.message < b.message;
}

}  // namespace

void Report::add(Severity severity, std::string code, std::string file, std::string clip,
                 std::string path, std::string message) {
    issues.push_back(Issue{severity, std::move(code), std::move(file), std::move(clip),
                           std::move(path), std::move(message)});
}

bool Report::hasErrors() const noexcept {
    for (const Issue& issue : issues) {
        if (issue.severity == Severity::Error) return true;
    }
    return false;
}

bool Report::hasWarnings() const noexcept {
    for (const Issue& issue : issues) {
        if (issue.severity == Severity::Warning) return true;
    }
    return false;
}

std::size_t Report::count(Severity severity) const noexcept {
    std::size_t total = 0;
    for (const Issue& issue : issues) {
        if (issue.severity == severity) ++total;
    }
    return total;
}

std::vector<Issue> Report::sortedIssues() const {
    std::vector<Issue> sorted = issues;
    std::stable_sort(sorted.begin(), sorted.end(), issueLess);
    return sorted;
}

std::string Report::toJson(bool strict) const {
    const std::vector<Issue> sorted = sortedIssues();
    const std::size_t errors = count(Severity::Error);
    const std::size_t warnings = count(Severity::Warning);
    const bool ok = errors == 0 && (!strict || warnings == 0);

    std::ostringstream out;
    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"ok\": " << (ok ? "true" : "false") << ",\n";
    out << "  \"strict\": " << (strict ? "true" : "false") << ",\n";
    out << "  \"summary\": {\n";
    out << "    \"files\": " << fileCount << ",\n";
    out << "    \"clips\": " << clipCount << ",\n";
    out << "    \"errors\": " << errors << ",\n";
    out << "    \"warnings\": " << warnings << "\n";
    out << "  },\n";
    out << "  \"issues\": [";
    if (sorted.empty()) {
        out << "],\n";
    } else {
        out << "\n";
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            const Issue& issue = sorted[i];
            out << "    {\n";
            out << "      \"severity\": \"" << severityName(issue.severity) << "\",\n";
            out << "      \"code\": \"" << json::escape(issue.code) << "\",\n";
            out << "      \"file\": \"" << json::escape(issue.file) << "\",\n";
            out << "      \"clip\": \"" << json::escape(issue.clip) << "\",\n";
            out << "      \"path\": \"" << json::escape(issue.path) << "\",\n";
            out << "      \"message\": \"" << json::escape(issue.message) << "\"\n";
            out << "    }";
            if (i + 1 < sorted.size()) out << ",";
            out << "\n";
        }
        out << "  ],\n";
    }
    out << "  \"stats\": {\n";
    out << "    \"clips\": {";
    if (clipStats.empty()) {
        out << "}\n";
    } else {
        out << "\n";
        std::size_t index = 0;
        for (const auto& [name, stats] : clipStats) {
            out << "      \"" << json::escape(name) << "\": {\"length\": "
                << json::number(stats.length) << ", \"channels\": " << stats.channels
                << ", \"markers\": " << stats.markers << ", \"events\": " << stats.events
                << "}";
            if (++index < clipStats.size()) out << ",";
            out << "\n";
        }
        out << "    }\n";
    }
    out << "  }\n";
    out << "}\n";
    return out.str();
}

}  // namespace anim::validate
