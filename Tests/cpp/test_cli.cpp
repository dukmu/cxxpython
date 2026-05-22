#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "test_process.hpp"

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
