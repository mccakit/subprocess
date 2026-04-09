#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>

#include "test_config.h"

import std;
import subprocess;

using subprocess::CommandLine;
using subprocess::CompletedProcess;
using subprocess::PipeOption;
using subprocess::RunBuilder;

#ifdef _WIN32
#define EOL "\n"
#else
#define EOL "\n"
#endif

static std::string g_exe_dir;

static std::string dirname(std::string path)
{
    size_t slash_pos = path.size();
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == '/' || path[i] == '\\')
            slash_pos = i;
    }
    return path.substr(0, slash_pos);
}

static void prepend_this_to_path()
{
    std::string path = subprocess::cenv["PATH"];
    path = g_exe_dir + subprocess::kPathDelimiter + path;
    subprocess::cenv["PATH"] = path;
}

int main(int argc, char **argv)
{
    using subprocess::abspath;
    std::string path = subprocess::cenv["PATH"];
    std::string more_path = dirname(abspath(argv[0]));
    g_exe_dir = more_path;
    path += subprocess::kPathDelimiter;
    path += more_path;
    subprocess::cenv["PATH"] = path;

    return Catch::Session().run(argc, argv);
}

TEST_CASE("Path", "[env]")
{
    std::string path = subprocess::cenv["PATH"];
    REQUIRE(!path.empty());
}

TEST_CASE("UTF", "[utf]")
{
#ifdef _WIN32
    auto utf16 = subprocess::utf8_to_utf16("hello world");
    REQUIRE(utf16.size() == 11);
    auto utf8 = subprocess::utf16_to_utf8(utf16);
    REQUIRE(utf8 == "hello world");
#else
    SKIP("utf16 conversions not required on this platform");
#endif
}

TEST_CASE("EnvGuard", "[env]")
{
    using subprocess::cenv;
    std::string path = cenv["PATH"];
    std::string world = subprocess::cenv["HELLO"];
    REQUIRE(world == "");

    {
        subprocess::EnvGuard guard;
        subprocess::cenv["HELLO"] = "world";
        world = cenv["HELLO"];
        REQUIRE(world == "world");
    }
    world = cenv["HELLO"];
    REQUIRE(world == "");
    std::string new_path = cenv["PATH"];
    REQUIRE(path == new_path);
}

TEST_CASE("FindProgram", "[shell]")
{
    std::string path = subprocess::find_program("echo");
    REQUIRE(!path.empty());
}

TEST_CASE("PipeBinary", "[pipe]")
{
    auto pipe = subprocess::pipe_create();
    std::string str = "hello world\n";
    std::thread thread([&] {
        subprocess::sleep_seconds(1);
        auto transferred = subprocess::pipe_write(pipe.output, str.data(), str.size());
        REQUIRE(transferred == (subprocess::ssize_t)str.size());
    });
    std::vector<char> text;
    text.resize(32);
    double start = subprocess::monotonic_seconds();

    subprocess::ssize_t transferred = subprocess::pipe_read_some(pipe.input, text.data(), text.size());

    REQUIRE(transferred == (subprocess::ssize_t)str.size());

    double time_diff = subprocess::monotonic_seconds() - start;
    std::string str2 = text.data();
    thread.join();
    REQUIRE(str == str2);
    REQUIRE(time_diff == Catch::Approx(1.0).margin(0.1));
}

TEST_CASE("HelloWorld", "[run]")
{
    CompletedProcess completed = subprocess::run({"echo", "hello", "world"}, RunBuilder().cout(PipeOption::pipe));
    REQUIRE(completed.cout == "hello world" EOL);
    REQUIRE(completed.cerr.empty());
    REQUIRE(completed.returncode == 0);
    CommandLine args = {"echo", "hello", "world"};
    REQUIRE(completed.args == args);

    completed = subprocess::run({"echo", "hello", "world"}, RunBuilder().cout(PipeOption::cerr).cerr(PipeOption::pipe));

    REQUIRE(completed.cerr == "hello world" EOL);
    REQUIRE(completed.cout.empty());
    REQUIRE(completed.returncode == 0);
    REQUIRE(completed.args == args);
}

TEST_CASE("HelloWorldBuilder", "[run]")
{
    CompletedProcess completed = subprocess::RunBuilder({"echo", "hello", "world"}).cout(PipeOption::pipe).run();
    REQUIRE(completed.cout == "hello world" EOL);
    REQUIRE(completed.cerr.empty());
    REQUIRE(completed.returncode == 0);
    CommandLine args = {"echo", "hello", "world"};
    REQUIRE(completed.args == args);

    completed = subprocess::RunBuilder({"echo", "hello", "world"}).cout(PipeOption::cerr).cerr(PipeOption::pipe).run();

    REQUIRE(completed.cerr == "hello world" EOL);
    REQUIRE(completed.cout.empty());
    REQUIRE(completed.returncode == 0);
    REQUIRE(completed.args == args);
}

TEST_CASE("HelloWorldBuilderSmaller", "[run]")
{
    CompletedProcess completed = subprocess::RunBuilder {"echo", "hello", "world"}.cout(PipeOption::pipe).run();
    REQUIRE(completed.cout == "hello world" EOL);
    REQUIRE(completed.cerr.empty());
    REQUIRE(completed.returncode == 0);
    CommandLine args = {"echo", "hello", "world"};
    REQUIRE(completed.args == args);

    completed = subprocess::RunBuilder {"echo", "hello", "world"}.cout(PipeOption::cerr).cerr(PipeOption::pipe).run();

    REQUIRE(completed.cerr == "hello world" EOL);
    REQUIRE(completed.cout.empty());
    REQUIRE(completed.returncode == 0);
    REQUIRE(completed.args == args);
}

TEST_CASE("HelloWorld20", "[run]")
{
    CompletedProcess completed = subprocess::run({"echo", "hello", "world"}, {.cout = PipeOption::pipe});
    REQUIRE(completed.cout == "hello world" EOL);
    REQUIRE(completed.cerr.empty());
    REQUIRE(completed.returncode == 0);
    CommandLine args = {"echo", "hello", "world"};
    REQUIRE(completed.args == args);

    completed = subprocess::run({"echo", "hello", "world"}, {.cout = PipeOption::cerr, .cerr = PipeOption::pipe});
    REQUIRE(completed.cerr == "hello world" EOL);
    REQUIRE(completed.cout.empty());
    REQUIRE(completed.returncode == 0);
    REQUIRE(completed.args == args);
}

TEST_CASE("NotFound", "[run]")
{
    REQUIRE_THROWS(subprocess::run({"yay-322"}));
}

TEST_CASE("Cin", "[run]")
{
    auto completed = subprocess::run({"cat"}, RunBuilder().cin("hello world").cout(PipeOption::pipe));
    REQUIRE(completed.cout == "hello world");
}

TEST_CASE("NewEnvironment", "[env]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();

    subprocess::EnvMap env = subprocess::current_env_copy();
    REQUIRE(subprocess::cenv["HELLO"].to_string() == "");
    env["HELLO"] = "world";
    REQUIRE(subprocess::cenv["HELLO"].to_string() == "");

    auto completed = subprocess::RunBuilder({"printenv", "HELLO"}).cout(PipeOption::pipe).env(env).run();

    REQUIRE(completed.cout == "world" EOL);
}

TEST_CASE("Sleep", "[time]")
{
    subprocess::StopWatch timer;
    subprocess::sleep_seconds(1);
    REQUIRE(timer.seconds() == Catch::Approx(1).margin(0.5));
}

TEST_CASE("CerrToCout", "[run]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();
    CommandLine args = {"echo", "hello", "world"};

    subprocess::cenv["USE_CERR"] = "1";
    subprocess::find_program_clear_cache();

    auto completed = RunBuilder({"echo", "hello", "world"})
                         .cout(subprocess::PipeOption::pipe)
                         .cerr(subprocess::PipeOption::pipe)
                         .env(subprocess::current_env_copy())
                         .run();
    REQUIRE(completed.cout == "");
    REQUIRE(completed.cerr == "hello world" EOL);
    REQUIRE(completed.args == args);

    completed = RunBuilder(args).cerr(subprocess::PipeOption::cout).cout(PipeOption::pipe).run();

    REQUIRE(completed.cout == "hello world" EOL);
    REQUIRE(completed.cerr == "");
    REQUIRE(completed.args == args);
}

TEST_CASE("CoutToCerr", "[run]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();

    auto completed =
        RunBuilder({"echo", "hello", "world"}).cerr(subprocess::PipeOption::pipe).cout(PipeOption::cerr).run();

    REQUIRE(completed.cout == "");
    REQUIRE(completed.cerr == "hello world" EOL);
}

TEST_CASE("Poll", "[popen]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();
    auto popen = RunBuilder({"sleep", "3"}).popen();
    subprocess::StopWatch timer;

    int count = 0;
    while (!popen.poll())
        ++count;
    INFO("poll did " << count << " iterations");
    REQUIRE(count > 100);

    popen.close();

    double timeout = timer.seconds();
    REQUIRE(timeout == Catch::Approx(3).margin(0.5));
}

TEST_CASE("RunTimeout", "[popen]")
{
#ifdef _WIN32
    SKIP("windows crashes on this test");
#endif
    subprocess::EnvGuard guard;
    prepend_this_to_path();
    subprocess::StopWatch timer;
    REQUIRE_THROWS_AS(subprocess::run({"sleep", "3"}, {.timeout = 1}), subprocess::TimeoutExpired);
    double timeout = timer.seconds();
    REQUIRE(timeout == Catch::Approx(1).margin(0.5));
}

TEST_CASE("WaitTimeout", "[popen]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();
    auto popen = RunBuilder({"sleep", "10"}).new_process_group(true).popen();
    subprocess::StopWatch timer;

    REQUIRE_THROWS_AS(popen.wait(3), subprocess::TimeoutExpired);

    popen.terminate();
    popen.close();

    double timeout = timer.seconds();
    REQUIRE(timeout == Catch::Approx(3).margin(0.5));
}

TEST_CASE("2ProcessConnect", "[popen]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();

    subprocess::PipePair pipe = subprocess::pipe_create(false);
    REQUIRE(!!pipe);

    subprocess::Popen cat = RunBuilder({"cat"}).cout(PipeOption::pipe).cin(pipe.input).popen();
    subprocess::Popen echo = RunBuilder({"echo", "hello", "world"}).cout(pipe.output).popen();
    pipe.close();
    CompletedProcess process = subprocess::run(cat);
    echo.close();
    cat.close();
    REQUIRE(process.cout == "hello world" EOL);
}

TEST_CASE("Kill", "[popen]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();

    auto popen = RunBuilder({"sleep", "10"}).popen();
    subprocess::StopWatch timer;
    std::thread thread([&] {
        subprocess::sleep_seconds(3);
        popen.kill();
    });

    thread.detach();
    popen.close();

    double timeout = timer.seconds();
    REQUIRE(timeout == Catch::Approx(3).margin(0.5));
}

TEST_CASE("Terminate", "[popen]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();

    auto popen = RunBuilder({"sleep", "10"}).new_process_group(true).popen();
    subprocess::StopWatch timer;
    std::thread thread([&] {
        subprocess::sleep_seconds(3);
        popen.terminate();
    });

    thread.detach();
    popen.close();

    double timeout = timer.seconds();
    REQUIRE(timeout == Catch::Approx(3).margin(0.5));
}

TEST_CASE("SIGINT", "[popen]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();

    auto popen = RunBuilder({"sleep", "10"}).new_process_group(true).popen();
    subprocess::StopWatch timer;
    std::thread thread([&] {
        subprocess::sleep_seconds(3);
#ifdef _WIN32
        popen.kill();
#else
        popen.send_signal(subprocess::SigNum::PSIGINT);
#endif
    });

    popen.close();
    if (thread.joinable())
        thread.join();

    double timeout = timer.seconds();
    REQUIRE(timeout == Catch::Approx(3).margin(0.5));
}

TEST_CASE("NonBlock", "[pipe]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();
    using subprocess::pipe_read;
    using subprocess::pipe_write;
    auto pipe = subprocess::pipe_create(false);

    auto cat = subprocess::Popen(subprocess::CommandLine {"cat"},
                                 {
                                     .cin = pipe.input,
                                     .cout = PipeOption::pipe,
                                 });
    pipe.disown_input();

    subprocess::pipe_set_blocking(cat.cout, false);
    std::string str = "hello world";
    std::thread thread([&] {
        subprocess::sleep_seconds(0.1);
        auto transferred = pipe_write(pipe.output, str.data(), str.size());
        REQUIRE(transferred == (subprocess::ssize_t)str.size());
    });
    std::vector<char> buffer;
    buffer.resize(1024);
    int iterations = 0;

    do
    {
        ++iterations;
        auto transferred = pipe_read(cat.cout, &buffer[0], buffer.size());
        if (transferred < 0)
        {
            REQUIRE(transferred > 0);
            break;
        }
        if (transferred == 0)
            continue;
        REQUIRE(transferred == (subprocess::ssize_t)str.size());
        break;
    } while (true);
    REQUIRE(iterations > 2);
    REQUIRE(strcmp(&buffer[0], str.data()) == 0);
    pipe.close();

    if (thread.joinable())
        thread.join();
}

TEST_CASE("HighFrequency", "[run]")
{
    subprocess::EnvGuard guard;
    subprocess::cenv["BINARY"] = "1";
    for (int i = 0; i < 100; ++i)
    {
        std::stringstream stream;
        std::ostream *ostream = &stream;
        auto completed =
            subprocess::run({"echo", "hello", "world"}, subprocess::RunOptions {.cout = ostream, .check = false});
        auto str = stream.str();
        REQUIRE(str == "hello world\n");
        REQUIRE(completed.returncode == 0);
        if (str != "hello world\n" || completed.returncode != 0)
            break;
    }
}

TEST_CASE("File", "[pipe]")
{
    subprocess::EnvGuard guard;
    subprocess::cenv["BINARY"] = "1";
    subprocess::PipeHandle handle = subprocess::pipe_file("test.txt", "w");
    std::string str = "hello world\n";
    subprocess::run(subprocess::CommandLine {"echo", "hello", "world"}, {.cout = handle});
    subprocess::pipe_close(handle);
    handle = subprocess::pipe_file("test.txt", "r");
    REQUIRE(handle != subprocess::kBadPipeValue);
    std::vector<char> data;
    data.resize(1024);
    subprocess::ssize_t transferred = subprocess::pipe_read(handle, data.data(), data.size());
    REQUIRE(transferred == (subprocess::ssize_t)str.size());
    std::string str2 = data.data();
    REQUIRE(str == str2);
}

TEST_CASE("WriteClosed", "[pipe]")
{
    subprocess::EnvGuard guard;
    prepend_this_to_path();

    class DevZeroStreamBuf : public std::streambuf
    {
        public:
            using Super = std::streambuf;
            using int_type = typename Super::int_type;
            using char_type = std::streambuf::char_type;
            using traits_type = Super::traits_type;
            void close()
            {
                open = false;
            }

        protected:
            int_type underflow() override
            {
                return open ? traits_type::to_int_type('\0') : traits_type::eof();
            }
            std::streamsize xsgetn(char_type *s, std::streamsize n) override
            {
                if (!open)
                    return 0;
                std::memset(s, 0, n * sizeof(char_type));
                return n;
            }
            int_type overflow(int_type c) override
            {
                return open ? traits_type::not_eof(c) : traits_type::eof();
            }
            std::streamsize xsputn(const char_type *s, std::streamsize n) override
            {
                return open ? n : 0;
            }
            bool open = true;
    };

    DevZeroStreamBuf zero;
    std::istream stream(&zero);

    std::vector<int> order;
    std::mutex mutex;

    subprocess::Popen echo = RunBuilder({"echo", "hello", "world"}).cin(&stream).cout(PipeOption::pipe).popen();

    std::thread thread([&] {
        subprocess::sleep_seconds(1);
        std::unique_lock<std::mutex> lock(mutex);
        order.push_back(1);
        zero.close();
    });

    std::vector<char> buffer;
    buffer.resize(64);
    auto transferred = subprocess::pipe_read(echo.cout, &buffer[0], buffer.size());
    std::string expected = "hello world" EOL;
    REQUIRE(transferred == (subprocess::ssize_t)expected.size());
    echo.close();
    std::unique_lock<std::mutex> lock(mutex);
    order.push_back(0);
    lock.unlock();
    if (thread.joinable())
        thread.join();

    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == 0);
    REQUIRE(order[1] == 1);
    std::string cout = &buffer[0];
    REQUIRE(cout == expected);
}
