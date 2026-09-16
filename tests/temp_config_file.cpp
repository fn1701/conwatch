#include "temp_config_file.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

TempConfigFile::TempConfigFile(const std::string &contents)
{
    m_path = (fs::temp_directory_path()
              / ("conwatch-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this))
                 + ".yaml"))
                 .string();
    std::ofstream out(m_path, std::ios::trunc);
    out << contents;
}

TempConfigFile::~TempConfigFile()
{
    fs::remove(m_path);
}

const std::string &TempConfigFile::path() const
{
    return m_path;
}
