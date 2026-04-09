module;

#ifdef _WIN32
#ifndef __MINGW32__
#define NOMINMAX
#endif
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#else
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

#include <cerrno>
#include <csignal>
#endif

#include <cstdio>
#include <cstring>
#include <cstdlib>
export module subprocess:process_builder;

import std;
import :basic_types;
import :pipe;
import :pipe_var;
import :shell_utils_fwd;
import :utf8_to_utf16;
import :environ;

// ============================================================================
// Exported declarations
// ============================================================================

export namespace subprocess
{

    struct RunOptions
    {
            PipeVar cin = PipeOption::inherit;
            PipeVar cout = PipeOption::inherit;
            PipeVar cerr = PipeOption::inherit;
            bool new_process_group = false;
            std::string cwd;
            double timeout = -1;
            bool check = false;
            EnvMap env;
    };

    class ProcessBuilder;

    struct Popen
    {
        public:
            Popen()
            {
            }
            Popen(CommandLine command, const RunOptions &options);
            Popen(CommandLine command, RunOptions &&options);
            Popen(const Popen &) = delete;
            Popen &operator=(const Popen &) = delete;
            Popen(Popen &&);
            Popen &operator=(Popen &&);
            ~Popen();

            PipeHandle cin = kBadPipeValue;
            PipeHandle cout = kBadPipeValue;
            PipeHandle cerr = kBadPipeValue;
            pid_t pid = 0;
            int returncode = kBadReturnCode;
            std::string cwd;
            CommandLine args;

            void ignore_cout();
            void ignore_cerr();
            void ignore_output();
            bool poll();
            int wait(double timeout = -1);
            bool send_signal(int signal);
            bool terminate();
            bool kill();
            void close();
            void close_cin();

            friend ProcessBuilder;

        private:
            void init(CommandLine &command, RunOptions &options);
            std::thread cin_thread;
            std::thread cout_thread;
            std::thread cerr_thread;
#ifdef _WIN32
            PROCESS_INFORMATION process_info;
#endif
    };

    class ProcessBuilder
    {
        public:
            std::vector<PipeHandle> child_close_pipes;
            PipeHandle cin_pipe = kBadPipeValue;
            PipeHandle cout_pipe = kBadPipeValue;
            PipeHandle cerr_pipe = kBadPipeValue;
            PipeOption cin_option = PipeOption::inherit;
            PipeOption cout_option = PipeOption::inherit;
            PipeOption cerr_option = PipeOption::inherit;
            bool new_process_group = false;
            EnvMap env;
            std::string cwd;
            CommandLine command;

            std::string windows_command();
            std::string windows_args();
            std::string windows_args(const CommandLine &command);
            Popen run();
            Popen run_command(const CommandLine &command);
    };

    CompletedProcess run(Popen &popen, bool check = false);
    CompletedProcess run(CommandLine command, RunOptions options = {});

    struct RunBuilder
    {
            RunOptions options;
            CommandLine command;

            RunBuilder();
            RunBuilder(CommandLine cmd);
            RunBuilder(std::initializer_list<std::string> command);
            RunBuilder &check(bool ch);
            RunBuilder &cin(const PipeVar &cin);
            RunBuilder &cout(const PipeVar &cout);
            RunBuilder &cerr(const PipeVar &cerr);
            RunBuilder &cwd(std::string cwd);
            RunBuilder &env(const EnvMap &env);
            RunBuilder &timeout(double timeout);
            RunBuilder &new_process_group(bool new_group);
            operator RunOptions() const;
            CompletedProcess run();
            Popen popen();
    };

    double monotonic_seconds();
    double sleep_seconds(double seconds);

    class StopWatch
    {
        public:
            StopWatch();
            void start();
            double seconds() const;

        private:
            double mStart;
    };

} // namespace subprocess

// ============================================================================
// Implementation
// ============================================================================

#if defined(__MINGW32__) || (defined(__MINGW64__) && !defined(_MT) && !defined(__MINGW_USE_VC2005_THUNK))
#define USE_POLLING_WAIT 1
#else
#define USE_POLLING_WAIT 0
#endif

#ifndef _WIN32
extern "C" char **environ;
#endif

namespace subprocess
{

    using details::throw_os_error;

    // --- RunBuilder ---

    RunBuilder::RunBuilder()
    {
    }
    RunBuilder::RunBuilder(CommandLine cmd) : command(cmd)
    {
    }
    RunBuilder::RunBuilder(std::initializer_list<std::string> command) : command(command)
    {
    }
    RunBuilder &RunBuilder::check(bool ch)
    {
        options.check = ch;
        return *this;
    }
    RunBuilder &RunBuilder::cin(const PipeVar &cin)
    {
        options.cin = cin;
        return *this;
    }
    RunBuilder &RunBuilder::cout(const PipeVar &cout)
    {
        options.cout = cout;
        return *this;
    }
    RunBuilder &RunBuilder::cerr(const PipeVar &cerr)
    {
        options.cerr = cerr;
        return *this;
    }
    RunBuilder &RunBuilder::cwd(std::string cwd)
    {
        options.cwd = cwd;
        return *this;
    }
    RunBuilder &RunBuilder::env(const EnvMap &env)
    {
        options.env = env;
        return *this;
    }
    RunBuilder &RunBuilder::timeout(double timeout)
    {
        options.timeout = timeout;
        return *this;
    }
    RunBuilder &RunBuilder::new_process_group(bool new_group)
    {
        options.new_process_group = new_group;
        return *this;
    }
    RunBuilder::operator RunOptions() const
    {
        return options;
    }
    CompletedProcess RunBuilder::run()
    {
        return subprocess::run(command, options);
    }
    Popen RunBuilder::popen()
    {
        return Popen(command, options);
    }

    // --- StopWatch ---

    StopWatch::StopWatch()
    {
        start();
    }
    void StopWatch::start()
    {
        mStart = monotonic_seconds();
    }
    double StopWatch::seconds() const
    {
        return monotonic_seconds() - mStart;
    }

    // --- ProcessBuilder simple methods ---

    Popen ProcessBuilder::run()
    {
        return run_command(this->command);
    }

    // --- Popen inline-equivalent methods ---

    void Popen::ignore_cout()
    {
        pipe_ignore_and_close(cout);
        cout = kBadPipeValue;
    }
    void Popen::ignore_cerr()
    {
        pipe_ignore_and_close(cerr);
        cerr = kBadPipeValue;
    }
    void Popen::ignore_output()
    {
        ignore_cout();
        ignore_cerr();
    }
    void Popen::close_cin()
    {
        if (cin != kBadPipeValue)
        {
            pipe_close(cin);
            cin = kBadPipeValue;
        }
    }

    // --- details ---

    namespace details
    {
        void throw_os_error(const char *function, int errno_code)
        {
            if (errno_code == 0)
                return;
            std::string message = function;
            message += " failed: " + std::to_string(errno_code) + ": ";
            message += std::strerror(errno_code);
            throw OSError(message);
        }

        struct NoSigPipe
        {
#ifndef _WIN32
                NoSigPipe()
                {
                    sigprocmask(SIG_BLOCK, NULL, &old_state);
                    sigset_t set = old_state;
                    sigaddset(&set, SIGPIPE);
                    sigprocmask(SIG_BLOCK, &set, NULL);
                }
                ~NoSigPipe()
                {
                    sigprocmask(SIG_BLOCK, &old_state, NULL);
                }

            private:
                sigset_t old_state;
#endif
        };
    } // namespace details

    // --- monotonic_seconds / sleep_seconds ---

    double monotonic_seconds()
    {
        static bool needs_init = true;
        static std::chrono::steady_clock::time_point begin;
        static double last_value = 0;
        if (needs_init)
        {
            begin = std::chrono::steady_clock::now();
            needs_init = false;
        }
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::chrono::duration<double> duration = now - begin;
        double result = duration.count();
        if (result < last_value)
            return last_value;
        last_value = result;
        return result;
    }

    double sleep_seconds(double seconds)
    {
        StopWatch watch;
        std::chrono::duration<double> duration(seconds);
        std::this_thread::sleep_for(duration);
        return watch.seconds();
    }

    // --- pipe threading helpers ---

    struct AutoClosePipe
    {
            AutoClosePipe(PipeHandle handle)
            {
                mHandle = handle;
            }
            ~AutoClosePipe()
            {
                close();
            }
            void close()
            {
                if (mHandle != kBadPipeValue)
                {
                    pipe_close(mHandle);
                    mHandle = kBadPipeValue;
                }
            }

        private:
            PipeHandle mHandle;
    };

    static std::thread pipe_thread(PipeHandle input, std::ostream *output)
    {
        return std::thread([=]() {
            details::NoSigPipe noSigPipe;
            AutoClosePipe autoclose(input);
            std::vector<char> buffer(2048);
            while (true)
            {
                ssize_t transfered = pipe_read(input, &buffer[0], buffer.size());
                if (transfered <= 0)
                    break;
                output->write(&buffer[0], transfered);
            }
        });
    }

    [[nodiscard]]
    static ssize_t fwrite_fully(FILE *output, const void *buffer, size_t size)
    {
        const std::uint8_t *cursor = reinterpret_cast<const std::uint8_t *>(buffer);
        ssize_t total = 0;
        while (total < (ssize_t)size)
        {
            auto transferred = fwrite(cursor, 1, size - total, output);
            if (transferred == 0)
                break;
            cursor += transferred;
            total += transferred;
        }
        return total;
    }

    static std::thread pipe_thread(PipeHandle input, FILE *output)
    {
        return std::thread([=]() {
            details::NoSigPipe noSigPipe;
            AutoClosePipe autoclose(input);
            std::vector<char> buffer(2048);
            while (true)
            {
                ssize_t transferred = pipe_read(input, &buffer[0], buffer.size());
                if (transferred <= 0)
                    break;
                transferred = fwrite_fully(output, &buffer[0], transferred);
                if (transferred <= 0)
                    break;
            }
        });
    }

    static std::thread pipe_thread(FILE *input, PipeHandle output)
    {
        return std::thread([=]() {
            details::NoSigPipe noSigPipe;
            AutoClosePipe autoclose(output);
            std::vector<char> buffer(2048);
            while (true)
            {
                ssize_t transferred = fread(&buffer[0], 1, buffer.size(), input);
                if (transferred <= 0)
                    break;
                transferred = pipe_write_fully(output, &buffer[0], transferred);
                if (transferred <= 0)
                    break;
            }
        });
    }

    static std::thread pipe_thread(std::string &input, PipeHandle output)
    {
        return std::thread([input(std::move(input)), output]() {
            details::NoSigPipe noSigPipe;
            AutoClosePipe autoclose(output);
            std::size_t pos = 0;
            while (pos < input.size())
            {
                ssize_t transferred = pipe_write_fully(output, input.c_str() + pos, input.size() - pos);
                if (transferred <= 0)
                    break;
                pos += transferred;
            }
        });
    }

    static std::thread pipe_thread(std::istream *input, PipeHandle output)
    {
        return std::thread([=]() {
            details::NoSigPipe noSigPipe;
            AutoClosePipe autoclose(output);
            std::vector<char> buffer(2048);
            while (true)
            {
                input->read(&buffer[0], buffer.size());
                ssize_t transferred = input->gcount();
                if (input->bad())
                    break;
                if (transferred <= 0)
                {
                    if (input->eof())
                        break;
                    continue;
                }
                transferred = pipe_write_fully(output, &buffer[0], transferred);
                if (transferred <= 0)
                    break;
            }
        });
    }

    static std::thread setup_redirect_stream(PipeHandle input, PipeVar &output)
    {
        PipeVarIndex index = static_cast<PipeVarIndex>(output.index());
        switch (index)
        {
        case PipeVarIndex::handle:
        case PipeVarIndex::option:
            break;
        case PipeVarIndex::string:
        case PipeVarIndex::istream:
            throw std::domain_error("expected something to output to");
        case PipeVarIndex::ostream:
            return pipe_thread(input, std::get<std::ostream *>(output));
        case PipeVarIndex::file:
            return pipe_thread(input, std::get<FILE *>(output));
        }
        return {};
    }

    static std::thread setup_redirect_stream(PipeVar &input, PipeHandle output)
    {
        PipeVarIndex index = static_cast<PipeVarIndex>(input.index());
        switch (index)
        {
        case PipeVarIndex::handle:
        case PipeVarIndex::option:
            break;
        case PipeVarIndex::string:
            return pipe_thread(std::get<std::string>(input), output);
        case PipeVarIndex::istream:
            return pipe_thread(std::get<std::istream *>(input), output);
        case PipeVarIndex::ostream:
            throw std::domain_error("reading from std::ostream doesn't make sense");
        case PipeVarIndex::file:
            return pipe_thread(std::get<FILE *>(input), output);
        }
        return {};
    }

    // --- Popen core ---

    Popen::Popen(CommandLine command, const RunOptions &optionsIn)
    {
        RunOptions options = optionsIn;
        init(command, options);
    }

    Popen::Popen(CommandLine command, RunOptions &&optionsIn)
    {
        RunOptions options = std::move(optionsIn);
        init(command, options);
    }

    void Popen::init(CommandLine &command, RunOptions &options)
    {
        ProcessBuilder builder;
        builder.cin_option = get_pipe_option(options.cin);
        builder.cout_option = get_pipe_option(options.cout);
        builder.cerr_option = get_pipe_option(options.cerr);

        if (builder.cin_option == PipeOption::specific)
        {
            builder.cin_pipe = std::get<PipeHandle>(options.cin);
            if (builder.cin_pipe == kBadPipeValue)
                throw std::invalid_argument("bad pipe value for cin");
        }
        if (builder.cout_option == PipeOption::specific)
        {
            builder.cout_pipe = std::get<PipeHandle>(options.cout);
            if (builder.cout_pipe == kBadPipeValue)
                throw std::invalid_argument("Popen constructor: bad pipe value for cout");
        }
        if (builder.cerr_option == PipeOption::specific)
        {
            builder.cerr_pipe = std::get<PipeHandle>(options.cerr);
            if (builder.cout_pipe == kBadPipeValue)
                throw std::invalid_argument("Popen constructor: bad pipe value for cout");
        }

        builder.new_process_group = options.new_process_group;
        builder.env = options.env;
        builder.cwd = options.cwd;

        *this = builder.run_command(command);

        cin_thread = setup_redirect_stream(options.cin, cin);
        cout_thread = setup_redirect_stream(cout, options.cout);
        cerr_thread = setup_redirect_stream(cerr, options.cerr);
        if (cin_thread.joinable())
            cin = kBadPipeValue;
        if (cout_thread.joinable())
            cout = kBadPipeValue;
        if (cerr_thread.joinable())
            cerr = kBadPipeValue;
    }

    Popen::Popen(Popen &&other)
    {
        *this = std::move(other);
    }

    Popen &Popen::operator=(Popen &&other)
    {
        close();
        cin = other.cin;
        cout = other.cout;
        cerr = other.cerr;
        pid = other.pid;
        returncode = other.returncode;
        args = std::move(other.args);
#ifdef _WIN32
        process_info = other.process_info;
        other.process_info = {0};
#endif
        other.cin = kBadPipeValue;
        other.cout = kBadPipeValue;
        other.cerr = kBadPipeValue;
        other.pid = 0;
        other.returncode = -1000;
        cin_thread = std::move(other.cin_thread);
        cout_thread = std::move(other.cout_thread);
        cerr_thread = std::move(other.cerr_thread);
        return *this;
    }

    Popen::~Popen()
    {
        close();
    }

    void Popen::close()
    {
        if (cin_thread.joinable())
            cin_thread.join();
        if (cout_thread.joinable())
            cout_thread.join();
        if (cerr_thread.joinable())
            cerr_thread.join();
        if (cin != kBadPipeValue)
            pipe_close(cin);
        if (cout != kBadPipeValue)
            pipe_close(cout);
        if (cerr != kBadPipeValue)
            pipe_close(cerr);
        cin = cout = cerr = kBadPipeValue;
        if (pid)
        {
            wait();
#ifdef _WIN32
            CloseHandle(process_info.hProcess);
            CloseHandle(process_info.hThread);
#endif
        }
        pid = 0;
        returncode = kBadReturnCode;
        args.clear();
    }

    bool Popen::terminate()
    {
        return send_signal(PSIGTERM);
    }

    bool Popen::kill()
    {
        return send_signal(PSIGKILL);
    }

    // --- ProcessBuilder shared ---

    std::string ProcessBuilder::windows_command()
    {
        return this->command[0];
    }

    std::string ProcessBuilder::windows_args()
    {
        return this->windows_args(this->command);
    }

    std::string ProcessBuilder::windows_args(const CommandLine &command)
    {
        std::string args;
        for (unsigned int i = 0; i < command.size(); ++i)
        {
            if (i > 0)
                args += ' ';
            args += escape_shell_arg(command[i]);
        }
        return args;
    }

    // --- run() ---

    CompletedProcess run(Popen &popen, bool check)
    {
        CompletedProcess completed;
        std::thread cout_thread;
        std::thread cerr_thread;
        if (popen.cout != kBadPipeValue)
        {
            cout_thread = std::thread([&]() {
                try
                {
                    completed.cout = pipe_read_all(popen.cout);
                }
                catch (...)
                {
                }
                pipe_close(popen.cout);
                popen.cout = kBadPipeValue;
            });
        }
        if (popen.cerr != kBadPipeValue)
        {
            cerr_thread = std::thread([&]() {
                try
                {
                    completed.cerr = pipe_read_all(popen.cerr);
                }
                catch (...)
                {
                }
                pipe_close(popen.cerr);
                popen.cerr = kBadPipeValue;
            });
        }
        if (cout_thread.joinable())
            cout_thread.join();
        if (cerr_thread.joinable())
            cerr_thread.join();

        popen.wait();
        completed.returncode = popen.returncode;
        completed.args = CommandLine(popen.args.begin() + 1, popen.args.end());
        if (check)
        {
            CalledProcessError error("failed to execute " + popen.args[0]);
            error.cmd = popen.args;
            error.returncode = completed.returncode;
            error.cout = std::move(completed.cout);
            error.cerr = std::move(completed.cerr);
            throw error;
        }
        return completed;
    }

    CompletedProcess run(CommandLine command, RunOptions options)
    {
        Popen popen(command, std::move(options));
        CompletedProcess completed;
        std::thread cout_thread;
        std::thread cerr_thread;
        if (popen.cout != kBadPipeValue)
        {
            cout_thread = std::thread([&]() {
                try
                {
                    completed.cout = pipe_read_all(popen.cout);
                }
                catch (...)
                {
                }
                pipe_close(popen.cout);
                popen.cout = kBadPipeValue;
            });
        }
        if (popen.cerr != kBadPipeValue)
        {
            cerr_thread = std::thread([&]() {
                try
                {
                    completed.cerr = pipe_read_all(popen.cerr);
                }
                catch (...)
                {
                }
                pipe_close(popen.cerr);
                popen.cerr = kBadPipeValue;
            });
        }
        if (cout_thread.joinable())
            cout_thread.join();
        if (cerr_thread.joinable())
            cerr_thread.join();

        try
        {
            popen.wait(options.timeout);
        }
        catch (subprocess::TimeoutExpired &expired)
        {
            popen.send_signal(subprocess::SigNum::PSIGTERM);
            try
            {
                popen.wait(1.0 / 20.0);
            }
            catch (subprocess::TimeoutExpired &expired)
            {
                popen.kill();
            }
            popen.wait();
            subprocess::TimeoutExpired timeout("subprocess::run timeout reached");
            timeout.cmd = command;
            timeout.timeout = options.timeout;
            timeout.cout = std::move(completed.cout);
            timeout.cerr = std::move(completed.cerr);
            throw timeout;
        }

        completed.returncode = popen.returncode;
        completed.args = command;
        if (options.check && completed.returncode != 0)
        {
            CalledProcessError error("failed to execute " + command[0]);
            error.cmd = command;
            error.returncode = completed.returncode;
            error.cout = std::move(completed.cout);
            error.cerr = std::move(completed.cerr);
            throw error;
        }
        return completed;
    }

    // ==========================================================================
    // Platform-specific: Windows
    // ==========================================================================
#ifdef _WIN32

    static STARTUPINFO g_startupInfo;
    static bool g_startupInfoInit = false;

    static void init_startup_info()
    {
        if (g_startupInfoInit)
            return;
        GetStartupInfo(&g_startupInfo);
    }

    static bool disable_inherit(PipeHandle handle)
    {
        return !!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);
    }

    static std::string lastErrorString()
    {
        LPTSTR lpMsgBuf = nullptr;
        DWORD dw = GetLastError();
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL,
                      dw,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (LPTSTR)&lpMsgBuf,
                      0,
                      NULL);
        std::string message = lptstr_to_string((LPTSTR)lpMsgBuf);
        LocalFree(lpMsgBuf);
        return message;
    }

    static DWORD wait_for_process(HANDLE handle, DWORD *exit_code, DWORD millis, double poll_ms = 100)
    {
        if (!USE_POLLING_WAIT || millis == INFINITE)
        {
            DWORD result = WaitForSingleObject(handle, millis);
            if (result == WAIT_TIMEOUT)
                return result;
            else if (result == WAIT_ABANDONED)
            {
                DWORD error = GetLastError();
                throw OSError("WAIT_ABANDONED error:" + std::to_string(error));
            }
            else if (result == WAIT_FAILED)
            {
                DWORD error = GetLastError();
                throw OSError("WAIT_FAILED error:" + std::to_string(error) + ":" + lastErrorString());
            }
            else if (result != WAIT_OBJECT_0)
            {
                throw OSError("WaitForSingleObject failed: " + std::to_string(result));
            }
            bool ok = !!GetExitCodeProcess(handle, exit_code);
            if (!ok)
            {
                DWORD error = GetLastError();
                throw OSError("GetExitCodeProcess failed: " + std::to_string(error) + ":" + lastErrorString());
            }
            return WAIT_OBJECT_0;
        }
        else
        {
            auto start = monotonic_seconds();
            int64_t remaining_ms = millis * 1000;
            while (true)
            {
                bool ok = !!GetExitCodeProcess(handle, exit_code);
                if (!ok)
                {
                    DWORD error = GetLastError();
                    throw OSError("GetExitCodeProcess failed: " + std::to_string(error) + ":" + lastErrorString());
                }
                else if (*exit_code != STILL_ACTIVE)
                    return WAIT_OBJECT_0;
                remaining_ms = millis - (monotonic_seconds() - start) * 1000;
                if (remaining_ms <= 0)
                    break;
                sleep_seconds(poll_ms / 1000.0);
            }
            return WAIT_TIMEOUT;
        }
    }

    bool Popen::poll()
    {
        if (returncode != kBadReturnCode)
            return true;
        DWORD exit_code = 0;
        DWORD result = wait_for_process(process_info.hProcess, &exit_code, 0);
        if (result == WAIT_TIMEOUT)
            return false;
        else if (result != WAIT_OBJECT_0)
            throw OSError("unkown error wait_for_process failed");
        returncode = exit_code;
        return true;
    }

    int Popen::wait(double timeout)
    {
        if (returncode != kBadReturnCode)
            return returncode;
        DWORD ms = timeout < 0 ? INFINITE : (DWORD)(timeout * 1000.0);
        DWORD exit_code = 0;
        DWORD result = wait_for_process(process_info.hProcess, &exit_code, ms);
        if (result == WAIT_TIMEOUT)
            throw TimeoutExpired("timeout of " + std::to_string(ms) + " expired");
        else if (result != WAIT_OBJECT_0)
            throw OSError("unkown error wait_for_process failed");
        returncode = exit_code;
        return returncode;
    }

    bool Popen::send_signal(int signum)
    {
        if (returncode != kBadReturnCode)
            return false;
        bool success = false;
        if (signum == PSIGKILL)
            return TerminateProcess(process_info.hProcess, 137);
        else if (signum == PSIGINT)
            success = GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid);
        else
            success = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
        if (!success)
        {
            std::string str = lastErrorString();
            // TODO: better error handling
        }
        return success;
    }

    Popen ProcessBuilder::run_command(const CommandLine &command)
    {
        static_assert(sizeof(wchar_t) == 2, "wchar_t must be of size 2");
        static_assert(sizeof(wchar_t) == sizeof(char16_t), "wchar_t must be of size 2");

        std::string program = find_program(command[0]);
        if (program.empty())
            throw CommandNotFoundError("command not found " + command[0]);
        init_startup_info();

        Popen process;
        PipePair cin_pair;
        PipePair cout_pair;
        PipePair cerr_pair;
        PipePair closed_pair;

        SECURITY_ATTRIBUTES saAttr = {0};
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        STARTUPINFOW siStartInfo = {0};
        BOOL bSuccess = FALSE;

        siStartInfo.cb = sizeof(siStartInfo);
        siStartInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        siStartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

        if (cin_option == PipeOption::close)
        {
            cin_pair = pipe_create(true);
            siStartInfo.hStdInput = cin_pair.input;
            disable_inherit(cin_pair.output);
        }
        else if (cin_option == PipeOption::specific)
        {
            pipe_set_inheritable(cin_pipe, true);
            siStartInfo.hStdInput = cin_pipe;
        }
        else if (cin_option == PipeOption::pipe)
        {
            cin_pair = pipe_create(true);
            siStartInfo.hStdInput = cin_pair.input;
            process.cin = cin_pair.output;
            disable_inherit(cin_pair.output);
        }

        if (cout_option == PipeOption::close)
        {
            cout_pair = pipe_create(true);
            siStartInfo.hStdOutput = cout_pair.output;
            disable_inherit(cout_pair.input);
        }
        else if (cout_option == PipeOption::pipe)
        {
            cout_pair = pipe_create(true);
            siStartInfo.hStdOutput = cout_pair.output;
            process.cout = cout_pair.input;
            disable_inherit(cout_pair.input);
        }
        else if (cout_option == PipeOption::cerr)
        {
            // Do this when stderr is setup below
        }
        else if (cout_option == PipeOption::specific)
        {
            pipe_set_inheritable(cout_pipe, true);
            siStartInfo.hStdOutput = cout_pipe;
        }

        if (cerr_option == PipeOption::close)
        {
            cerr_pair = pipe_create(true);
            siStartInfo.hStdError = cerr_pair.output;
            disable_inherit(cerr_pair.input);
        }
        else if (cerr_option == PipeOption::pipe)
        {
            cerr_pair = pipe_create(true);
            siStartInfo.hStdError = cerr_pair.output;
            process.cerr = cerr_pair.input;
            disable_inherit(cerr_pair.input);
        }
        else if (cerr_option == PipeOption::cout)
        {
            siStartInfo.hStdError = siStartInfo.hStdOutput;
        }
        else if (cerr_option == PipeOption::specific)
        {
            pipe_set_inheritable(cerr_pipe, true);
            siStartInfo.hStdError = cerr_pipe;
        }

        if (cout_option == PipeOption::cerr)
            siStartInfo.hStdOutput = siStartInfo.hStdError;

        std::string args = windows_args(command);

        void *env_ptr = nullptr;
        std::u16string envblock;
        if (!this->env.empty())
        {
            envblock = create_env_block(this->env);
            env_ptr = (void *)envblock.data();
        }
        DWORD process_flags = CREATE_UNICODE_ENVIRONMENT;
        if (this->new_process_group)
            process_flags |= CREATE_NEW_PROCESS_GROUP;

        process.cwd = this->cwd;
        std::u16string cmd_args {utf8_to_utf16(args)};
        cmd_args.reserve(MAX_PATH + 1);
        bSuccess = CreateProcessW((LPCWSTR)utf8_to_utf16(program).c_str(),
                                  (LPWSTR)cmd_args.data(),
                                  NULL,
                                  NULL,
                                  TRUE,
                                  process_flags,
                                  env_ptr,
                                  (LPCWSTR)(this->cwd.empty() ? nullptr : utf8_to_utf16(this->cwd).c_str()),
                                  &siStartInfo,
                                  &process.process_info);

        process.pid = process.process_info.dwProcessId;
        if (cin_pair)
            cin_pair.close_input();
        if (cout_pair)
            cout_pair.close_output();
        if (cerr_pair)
            cerr_pair.close_output();

        if (cin_option == PipeOption::close)
            cin_pair.close();
        if (cout_option == PipeOption::close)
            cout_pair.close();
        if (cerr_option == PipeOption::close)
            cerr_pair.close();

        cin_pair.disown();
        cout_pair.disown();
        cerr_pair.disown();

        process.args = command;
        if (!bSuccess)
            throw SpawnError("CreateProcess failed");
        return process;
    }

    // ==========================================================================
    // Platform-specific: POSIX
    // ==========================================================================
#else

    namespace
    {
        struct cstring_vector
        {
                typedef char *value_type;
                ~cstring_vector()
                {
                    clear();
                }
                void clear()
                {
                    for (char *ptr : m_list)
                        if (ptr)
                            free(ptr);
                    m_list.clear();
                }
                void reserve(std::size_t size)
                {
                    m_list.reserve(size);
                }
                void push_back(const std::string &str)
                {
                    push_back(str.c_str());
                }
                void push_back(std::nullptr_t)
                {
                    m_list.push_back(nullptr);
                }
                void push_back(const char *str)
                {
                    char *copy = str ? strdup(str) : nullptr;
                    m_list.push_back(copy);
                }
                value_type &operator[](std::size_t index)
                {
                    return m_list[index];
                }
                char **data()
                {
                    return &m_list[0];
                }
                std::vector<char *> m_list;
        };
    } // namespace

    struct FileActions
    {
            FileActions()
            {
                int result = posix_spawn_file_actions_init(&actions);
                throw_os_error("posix_spawn_file_actions_init", result);
            }
            ~FileActions()
            {
                int result = posix_spawn_file_actions_destroy(&actions);
                throw_os_error("posix_spawn_file_actions_destroy", result);
            }
            void adddup2(int fd, int newfd)
            {
                int result = posix_spawn_file_actions_adddup2(&actions, fd, newfd);
                throw_os_error("posix_spawn_file_actions_adddup2", result);
            }
            void addclose(int fd)
            {
                int result = posix_spawn_file_actions_addclose(&actions, fd);
                throw_os_error("posix_spawn_file_actions_addclose", result);
            }
            posix_spawn_file_actions_t *get()
            {
                return &actions;
            }
            posix_spawn_file_actions_t actions;
    };

    // Forward-declare; defined in :environ partition
    class CwdGuard;

    bool Popen::poll()
    {
        if (returncode != kBadReturnCode)
            return true;
        int exit_code;
        auto child = waitpid(pid, &exit_code, WNOHANG);
        if (child == 0)
            return false;
        if (child > 0)
        {
            if (WIFEXITED(exit_code))
                returncode = WEXITSTATUS(exit_code);
            else if (WIFSIGNALED(exit_code))
                returncode = -WTERMSIG(exit_code);
            else
                returncode = 1;
        }
        return child > 0;
    }

    int Popen::wait(double timeout)
    {
        if (returncode != kBadReturnCode)
            return returncode;
        if (timeout < 0)
        {
            int exit_code;
            while (true)
            {
                pid_t child = waitpid(pid, &exit_code, 0);
                if (child == -1 && errno == EINTR)
                    continue;
                if (child == -1)
                {
                    // TODO: throw oserror(errno)
                }
                break;
            }
            if (WIFEXITED(exit_code))
                returncode = WEXITSTATUS(exit_code);
            else if (WIFSIGNALED(exit_code))
                returncode = -WTERMSIG(exit_code);
            else
                returncode = 1;
            return returncode;
        }
        StopWatch watch;
        while (watch.seconds() < timeout)
        {
            if (poll())
                return returncode;
            sleep_seconds(0.00001);
        }
        throw TimeoutExpired("no time");
    }

    bool Popen::send_signal(int signum)
    {
        if (returncode != kBadReturnCode)
            return false;
        return ::kill(pid, signum) == 0;
    }

    Popen ProcessBuilder::run_command(const CommandLine &command)
    {
        if (command.empty())
            throw std::invalid_argument("command should not be empty");

        std::string program = find_program(command[0]);
        if (program.empty())
            throw CommandNotFoundError("command not found " + command[0]);

        Popen process;
        PipePair cin_pair;
        PipePair cout_pair;
        PipePair cerr_pair;

        FileActions actions;

        if (cin_option == PipeOption::close)
            actions.addclose(kStdInValue);
        else if (cin_option == PipeOption::specific)
        {
            if (this->cin_pipe == kBadPipeValue)
                throw std::invalid_argument("ProcessBuilder: bad pipe value for cin");
            pipe_set_inheritable(this->cin_pipe, true);
            actions.adddup2(this->cin_pipe, kStdInValue);
            actions.addclose(this->cin_pipe);
        }
        else if (cin_option == PipeOption::pipe)
        {
            cin_pair = pipe_create(true);
            actions.addclose(cin_pair.output);
            actions.adddup2(cin_pair.input, kStdInValue);
            actions.addclose(cin_pair.input);
            process.cin = cin_pair.output;
            pipe_set_inheritable(process.cin, false);
        }

        if (cout_option == PipeOption::close)
            actions.addclose(kStdOutValue);
        else if (cout_option == PipeOption::pipe)
        {
            cout_pair = pipe_create(true);
            actions.addclose(cout_pair.input);
            actions.adddup2(cout_pair.output, kStdOutValue);
            actions.addclose(cout_pair.output);
            process.cout = cout_pair.input;
            pipe_set_inheritable(process.cout, false);
        }
        else if (cout_option == PipeOption::cerr)
        {
            // we have to wait until stderr is setup first
        }
        else if (cout_option == PipeOption::specific)
        {
            if (this->cout_pipe == kBadPipeValue)
                throw std::invalid_argument("ProcessBuilder: bad pipe value for cout");
            pipe_set_inheritable(this->cout_pipe, true);
            actions.adddup2(this->cout_pipe, kStdOutValue);
            actions.addclose(this->cout_pipe);
        }

        if (cerr_option == PipeOption::close)
        {
            actions.addclose(kStdErrValue);
        }
        else if (cerr_option == PipeOption::pipe)
        {
            cerr_pair = pipe_create(true);
            actions.addclose(cerr_pair.input);
            actions.adddup2(cerr_pair.output, kStdErrValue);
            actions.addclose(cerr_pair.output);
            process.cerr = cerr_pair.input;
            pipe_set_inheritable(process.cerr, false);
        }
        else if (cerr_option == PipeOption::cout)
        {
            actions.adddup2(kStdOutValue, kStdErrValue);
        }
        else if (cerr_option == PipeOption::specific)
        {
            if (this->cerr_pipe == kBadPipeValue)
                throw std::invalid_argument("ProcessBuilder: bad pipe value for cerr");
            pipe_set_inheritable(this->cerr_pipe, true);
            actions.adddup2(this->cerr_pipe, kStdErrValue);
            actions.addclose(this->cerr_pipe);
        }

        if (cout_option == PipeOption::cerr)
            actions.adddup2(kStdErrValue, kStdOutValue);

        pid_t pid;
        cstring_vector args;
        args.reserve(command.size() + 1);
        args.push_back(program);
        for (size_t i = 1; i < command.size(); ++i)
            args.push_back(command[i]);
        args.push_back(nullptr);

        char **env = environ;
        cstring_vector env_store;
        if (!this->env.empty())
        {
            for (auto &pair : this->env)
            {
                std::string line = pair.first + "=" + pair.second;
                env_store.push_back(line);
            }
            env_store.push_back(nullptr);
            env = &env_store[0];
        }

        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        struct SpawnAttr
        {
                SpawnAttr(posix_spawnattr_t &attributes)
                {
                    this->attributes = &attributes;
                }
                ~SpawnAttr()
                {
                    int ret = posix_spawnattr_destroy(attributes);
                    throw_os_error("posix_spawnattr_destroy", ret);
                }
                void setflags(short flags)
                {
                    int ret = posix_spawnattr_setflags(attributes, flags);
                    throw_os_error("posix_spawnattr_setflags", ret);
                }
                posix_spawnattr_t *attributes;
        } attributes_raii(attributes);

        int flags = this->new_process_group ? POSIX_SPAWN_SETSIGMASK : 0;
#ifdef POSIX_SPAWN_USEVFORK
        flags |= POSIX_SPAWN_USEVFORK;
#endif
        attributes_raii.setflags(flags);
        {
            static std::mutex mutex;
            std::unique_lock<std::mutex> lock(mutex);
            CwdGuard cwdGuard;
            if (!this->cwd.empty())
                subprocess::setcwd(this->cwd);
            int ret = posix_spawn(&pid, args[0], actions.get(), &attributes, &args[0], env);
            if (ret != 0)
                throw SpawnError("posix_spawn failed with error: " + std::string(strerror(ret)));
        }
        args.clear();
        env_store.clear();
        if (cin_pair)
            cin_pair.close_input();
        if (cout_pair)
            cout_pair.close_output();
        if (cerr_pair)
            cerr_pair.close_output();

        cin_pair.disown();
        cout_pair.disown();
        cerr_pair.disown();
        process.pid = pid;
        process.args = command;
        return process;
    }

#endif

} // namespace subprocess
