#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>

namespace
{
    std::filesystem::path write_script(const std::string &name, const std::string &source)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
        std::ofstream file(path, std::ios::binary);
        file << source;
        return path;
    }

    std::string shell_quote(const std::filesystem::path &path)
    {
        std::string quoted = "'";
        for (char ch : path.string())
        {
            if (ch == '\'')
                quoted += "'\\''";
            else
                quoted.push_back(ch);
        }
        quoted.push_back('\'');
        return quoted;
    }

    struct CommandResult
    {
        int exit_code;
        std::string output;
    };

    CommandResult run_command(const std::string &command)
    {
        FILE *pipe = popen((command + " 2>&1").c_str(), "r");
        if (!pipe)
            return CommandResult{127, "popen failed"};

        std::ostringstream output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
            output << buffer;

        const int status = pclose(pipe);
        return CommandResult{status >= 0 ? WEXITSTATUS(status) : 127, output.str()};
    }
}

TEST(Cli, ExecutesScriptFile)
{
    const std::filesystem::path script = write_script(
        "cxxpython_cli_success.py",
        "x = __name__ + ':' + __file__\n"
        "y = x + ':ok'\n");

    const CommandResult result = run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + shell_quote(script));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output, "");
    std::filesystem::remove(script);
}

TEST(Cli, ReportsPythonErrors)
{
    const std::filesystem::path script = write_script("cxxpython_cli_name_error.py", "missing\n");

    const CommandResult result = run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + shell_quote(script));

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_EQ(result.output, "NameError: name 'missing' is not defined\n");
    std::filesystem::remove(script);
}

TEST(Cli, ReportsSyntaxErrorsWithLocation)
{
    const std::filesystem::path script = write_script("cxxpython_cli_syntax_error.py", "x = 1 +\n");

    const CommandResult result = run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + shell_quote(script));

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.output.find("SyntaxError: " + script.string() + ":1:8: expected expression"), std::string::npos);
    std::filesystem::remove(script);
}
