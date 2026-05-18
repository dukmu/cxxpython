#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>

namespace test_process
{
    struct CommandResult
    {
        int exit_code;
        std::string output;
    };

    inline std::filesystem::path write_script(const std::string &name, const std::string &source)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
        std::ofstream file(path, std::ios::binary);
        file << source;
        return path;
    }

    inline std::string read_text_file(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream output;
        output << file.rdbuf();
        return output.str();
    }

    inline std::string shell_quote(const std::filesystem::path &path)
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

    inline CommandResult run_command(const std::string &command)
    {
        FILE *pipe = popen((command + " 2>&1").c_str(), "r");
        if (!pipe)
            return CommandResult{127, "popen failed"};

        std::ostringstream output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
            output << buffer;

        const int status = pclose(pipe);
        if (status < 0)
            return CommandResult{127, output.str()};
        if (!WIFEXITED(status))
            return CommandResult{127, output.str()};
        return CommandResult{WEXITSTATUS(status), output.str()};
    }
}
