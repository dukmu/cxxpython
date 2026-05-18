#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "test_process.hpp"

namespace
{
    std::vector<std::filesystem::path> python_test_files()
    {
        std::vector<std::filesystem::path> files;
        const std::filesystem::path root = CXXPYTHON_PYTHON_TEST_DIR;
        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(root))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".py")
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    std::filesystem::path exit_file_for(const std::filesystem::path &script)
    {
        return script.string() + ".exit";
    }

    std::filesystem::path output_file_for(const std::filesystem::path &script)
    {
        return script.string() + ".out";
    }

    int expected_exit_code(const std::filesystem::path &script)
    {
        const std::filesystem::path path = exit_file_for(script);
        if (!std::filesystem::exists(path))
            return 0;
        return std::stoi(test_process::read_text_file(path));
    }

    std::string expected_output(const std::filesystem::path &script)
    {
        return test_process::read_text_file(output_file_for(script));
    }
}

TEST(PythonScripts, RunsGoldenFiles)
{
    const std::vector<std::filesystem::path> files = python_test_files();
    ASSERT_FALSE(files.empty());

    for (const std::filesystem::path &script : files)
    {
        SCOPED_TRACE(script.string());
        ASSERT_TRUE(std::filesystem::exists(output_file_for(script)));

        const test_process::CommandResult result =
            test_process::run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + test_process::shell_quote(script));

        EXPECT_EQ(result.exit_code, expected_exit_code(script));
        EXPECT_EQ(result.output, expected_output(script));
    }
}
