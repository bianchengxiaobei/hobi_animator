#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace anim::validate {

enum class Severity {
    Error = 0,
    Warning = 1,
    Info = 2,
};

struct Issue {
    Severity severity = Severity::Error;
    std::string code;
    std::string file;
    std::string clip;
    std::string path;
    std::string message;
};

struct ClipStats {
    float length = 0.0f;
    std::size_t channels = 0;
    std::size_t markers = 0;
    std::size_t events = 0;
};

class Report {
public:
    std::vector<Issue> issues;
    std::map<std::string, ClipStats> clipStats;
    std::size_t fileCount = 0;
    std::size_t clipCount = 0;

    void add(Severity severity, std::string code, std::string file = {},
             std::string clip = {}, std::string path = {}, std::string message = {});
    bool hasErrors() const noexcept;
    bool hasWarnings() const noexcept;
    std::size_t count(Severity severity) const noexcept;
    std::vector<Issue> sortedIssues() const;
    std::string toJson(bool strict) const;
};

}  // namespace anim::validate
