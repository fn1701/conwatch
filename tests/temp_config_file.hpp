#pragma once

#include <string>

// Writes `contents` to a fresh temp file and returns its path; the
// file is removed when the returned guard goes out of scope.
class TempConfigFile
{
public:
    explicit TempConfigFile(const std::string &contents);
    ~TempConfigFile();

    const std::string &path() const;

private:
    std::string m_path;
};
