#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_process.hpp"

namespace
{
    std::string program_with_args(const std::vector<std::string> &args)
    {
        std::string command = CXXPYTHON_PROGRAM_PATH;
        for (const std::string &arg : args)
        {
            command.push_back(' ');
            command += test_process::shell_quote(arg);
        }
        return command;
    }
}

TEST(Cli, ExecutesScriptFile)
{
    const std::filesystem::path script = test_process::write_script(
        "cxxpython_cli_success.py",
        "x = __name__ + ':' + __file__\n"
        "y = x + ':ok'\n");

    const test_process::CommandResult result =
        test_process::run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + test_process::shell_quote(script));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output, "");
    std::filesystem::remove(script);
}

TEST(Cli, ExecutesBuiltinPrintThroughScriptPath)
{
    const std::filesystem::path script = test_process::write_script(
        "cxxpython_cli_print.py",
        "print('hello', 42)\n");

    const test_process::CommandResult result =
        test_process::run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + test_process::shell_quote(script));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output, "hello 42\n");
    std::filesystem::remove(script);
}

TEST(Cli, PassesScriptPathAndArgumentsToSysArgv)
{
    const std::filesystem::path script = test_process::write_script(
        "cxxpython_cli_sys_argv.py",
        "import sys\n"
        "print(sys.argv[0])\n"
        "print(sys.argv[1])\n"
        "print(sys.argv[2])\n");

    const test_process::CommandResult result =
        test_process::run_command(program_with_args({script.string(), "alpha", "beta"}));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output,
              script.string() + "\n"
              "alpha\n"
              "beta\n");
    std::filesystem::remove(script);
}

TEST(Cli, SupportsCommandExecutionWithDashC)
{
    const test_process::CommandResult result = test_process::run_command(
        program_with_args({"-c", "print('hello', 42)"}));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output, "hello 42\n");
}

TEST(Cli, PassesCommandArgumentsToSysArgv)
{
    const test_process::CommandResult result = test_process::run_command(
        program_with_args({"-c", "import sys\nprint(sys.argv[0])\nprint(sys.argv[1])\nprint(sys.argv[2])", "alpha", "beta"}));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output,
              "-c\n"
              "alpha\n"
              "beta\n");
}

TEST(Cli, ReportsCommandPythonErrors)
{
    const test_process::CommandResult result = test_process::run_command(
        program_with_args({"-c", "missing"}));

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_EQ(result.output,
              "Traceback (most recent call last):\n"
              "  File \"<string>\", line 1, in <module>\n"
              "NameError: name 'missing' is not defined\n");
}

TEST(Cli, SupportsModuleExecutionWithDashM)
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path module = std::filesystem::absolute(directory / "cxxpython_cli_module_runner.py");

    std::ofstream(module) << "import sys\n"
                          "print(__name__)\n"
                          "print(__package__)\n"
                          "print(__spec__.name)\n"
                          "print(__spec__.origin)\n"
                          "print(sys.path[0])\n"
                          "print(sys.argv[0])\n"
                          "print(sys.argv[1])\n";

    const test_process::CommandResult result =
        test_process::run_command("cd " + test_process::shell_quote(directory) + " && " +
                                  program_with_args({"-m", "cxxpython_cli_module_runner", "tail"}));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output,
              std::string("__main__\n") +
                  "\n" +
                  "cxxpython_cli_module_runner\n" +
                  std::filesystem::absolute(module).string() + "\n" +
                  directory.string() + "\n" +
                  std::filesystem::absolute(module).string() + "\n" +
                  "tail\n");

    std::filesystem::remove(module);
}

TEST(Cli, ImportsSiblingPythonModule)
{
    const std::filesystem::path helper = test_process::write_script(
        "cxxpython_cli_import_helper.py",
        "value = 'from-helper'\n");
    const std::filesystem::path script = test_process::write_script(
        "cxxpython_cli_import_main.py",
        "helper = __import__('cxxpython_cli_import_helper')\n"
        "print(helper.value)\n");

    const test_process::CommandResult result =
        test_process::run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + test_process::shell_quote(script));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output, "from-helper\n");
    std::filesystem::remove(script);
    std::filesystem::remove(helper);
}

TEST(Cli, ReportsPythonErrors)
{
    const std::filesystem::path script =
        test_process::write_script("cxxpython_cli_name_error.py", "missing\n");

    const test_process::CommandResult result =
        test_process::run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + test_process::shell_quote(script));

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_EQ(result.output,
              "Traceback (most recent call last):\n"
              "  File \"" + script.string() + "\", line 1, in <module>\n"
              "NameError: name 'missing' is not defined\n");
    std::filesystem::remove(script);
}

TEST(Cli, ReportsNestedPythonTracebacks)
{
    const std::filesystem::path script =
        test_process::write_script("cxxpython_cli_nested_name_error.py",
                                   "def fail():\n"
                                   "    missing\n"
                                   "fail()\n");

    const test_process::CommandResult result =
        test_process::run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + test_process::shell_quote(script));

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_EQ(result.output,
              "Traceback (most recent call last):\n"
              "  File \"" + script.string() + "\", line 3, in <module>\n"
              "  File \"" + script.string() + "\", line 2, in fail\n"
              "NameError: name 'missing' is not defined\n");
    std::filesystem::remove(script);
}

TEST(Cli, ReportsSyntaxErrorsWithLocation)
{
    const std::filesystem::path script =
        test_process::write_script("cxxpython_cli_syntax_error.py", "x = 1 +\n");

    const test_process::CommandResult result =
        test_process::run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + test_process::shell_quote(script));

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.output.find("SyntaxError: " + script.string() + ":1:8: expected expression"), std::string::npos);
    std::filesystem::remove(script);
}

TEST(Cli, MapsExitHelperToProcessStatus)
{
    const std::filesystem::path script =
        test_process::write_script("cxxpython_cli_exit_helper.py", "exit(7)\n");

    const test_process::CommandResult result =
        test_process::run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + test_process::shell_quote(script));

    EXPECT_EQ(result.exit_code, 7);
    EXPECT_EQ(result.output, "");
    std::filesystem::remove(script);
}

TEST(Cli, MapsSystemExitToProcessStatusWithoutTraceback)
{
    const std::filesystem::path script =
        test_process::write_script("cxxpython_cli_system_exit.py", "raise SystemExit(3)\n");

    const test_process::CommandResult result =
        test_process::run_command(std::string(CXXPYTHON_PROGRAM_PATH) + " " + test_process::shell_quote(script));

    EXPECT_EQ(result.exit_code, 3);
    EXPECT_EQ(result.output, "");
    std::filesystem::remove(script);
}

TEST(Cli, RunsInteractiveReplFromStandardInput)
{
    const test_process::CommandResult result = test_process::run_command(
        "printf \"%s\\n\" \"1 + 2\" \"x = 'ok'\" \"x\" \"try:\" "
        "\"    raise TypeError('boom')\" \"except TypeError as e:\" \"    print(e)\" \"\" | " +
        std::string(CXXPYTHON_PROGRAM_PATH));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output,
              "cxxpython 0.1.0-dev\n"
              ">>> 3\n"
              ">>> >>> 'ok'\n"
              ">>> ... ... ... ... TypeError('boom')\n"
              ">>> ");
}

TEST(Cli, RunsNestedMultilineReplDefinitions)
{
    const test_process::CommandResult result = test_process::run_command(
        "printf \"%s\\n\" \"class A:\" \"    def __init__(self):\" "
        "\"        self.value = 'ok'\" \"\" \"A().value\" | " +
        std::string(CXXPYTHON_PROGRAM_PATH));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output,
              "cxxpython 0.1.0-dev\n"
              ">>> ... ... ... >>> 'ok'\n"
              ">>> ");
}

TEST(Cli, RequiresBlankLineToSubmitInteractiveBlocks)
{
    const test_process::CommandResult result = test_process::run_command(
        "printf \"%s\\n\" \"def f():\" \"    return 1\" \"\" \"f()\" | " +
        std::string(CXXPYTHON_PROGRAM_PATH));

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.output,
              "cxxpython 0.1.0-dev\n"
              ">>> ... ... >>> 1\n"
              ">>> ");
}

TEST(Cli, ReplExitHelperReturnsStatus)
{
    const test_process::CommandResult result =
        test_process::run_command("printf \"%s\\n\" \"exit(4)\" | " + std::string(CXXPYTHON_PROGRAM_PATH));

    EXPECT_EQ(result.exit_code, 4);
    EXPECT_EQ(result.output,
              "cxxpython 0.1.0-dev\n"
              ">>> ");
}
