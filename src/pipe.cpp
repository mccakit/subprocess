module;

#ifdef _WIN32
#ifndef __MINGW32__
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <poll.h>
#include <sys/ioctl.h>
#endif

#include <cstring>
#include <cstdio>
export module subprocess:pipe;

import std;
import :basic_types;
import :utf8_to_utf16;

export namespace subprocess
{

    struct PipePair
    {
            PipePair() {};
            PipePair(PipeHandle input, PipeHandle output) : input(input), output(output)
            {
            }
            ~PipePair()
            {
                close();
            }
            PipePair(const PipePair &) = delete;
            PipePair &operator=(const PipePair &) = delete;
            PipePair(PipePair &&other)
            {
                *this = std::move(other);
            }
            PipePair &operator=(PipePair &&other);

            const PipeHandle input = kBadPipeValue;
            const PipeHandle output = kBadPipeValue;

            void disown()
            {
                const_cast<PipeHandle &>(input) = const_cast<PipeHandle &>(output) = kBadPipeValue;
            }
            void disown_input()
            {
                const_cast<PipeHandle &>(input) = kBadPipeValue;
            }
            void disown_output()
            {
                const_cast<PipeHandle &>(output) = kBadPipeValue;
            }

            void close();
            void close_input();
            void close_output();
            explicit operator bool() const noexcept
            {
                return input != output;
            }
    };

    [[nodiscard]] ssize_t pipe_peak_bytes(PipeHandle pipe);
    bool pipe_close(PipeHandle handle);
    [[nodiscard]] PipePair pipe_create(bool inheritable = false);
    void pipe_set_inheritable(PipeHandle handle, bool inheritable);
    [[nodiscard]] ssize_t pipe_read(PipeHandle, void *buffer, size_t size);
    [[nodiscard]] ssize_t pipe_write(PipeHandle, const void *buffer, size_t size);
    [[nodiscard]] ssize_t pipe_write_fully(PipeHandle, const void *buffer, size_t size);
    bool pipe_set_blocking(PipeHandle, bool should_block);
    void pipe_ignore_and_close(PipeHandle handle);
    [[nodiscard]] std::string pipe_read_all(PipeHandle handle);
    [[nodiscard]] int pipe_wait_for_read(PipeHandle pipe, double seconds);
    [[nodiscard]] ssize_t pipe_read_some(PipeHandle, void *buffer, size_t size);
    [[nodiscard]] PipeHandle pipe_file(const char *filename, const char *mode);

    bool pipe_is_blocking(PipeHandle handle);
    double monotonic_seconds();

} // namespace subprocess

// --- implementation ---

namespace subprocess
{

    using details::throw_os_error;

    PipePair &PipePair::operator=(PipePair &&other)
    {
        close();
        const_cast<PipeHandle &>(input) = other.input;
        const_cast<PipeHandle &>(output) = other.output;
        other.disown();
        return *this;
    }
    void PipePair::close()
    {
        if (input != kBadPipeValue)
            pipe_close(input);
        if (output != kBadPipeValue)
            pipe_close(output);
        disown();
    }
    void PipePair::close_input()
    {
        if (input != kBadPipeValue)
        {
            pipe_close(input);
            const_cast<PipeHandle &>(input) = kBadPipeValue;
        }
    }
    void PipePair::close_output()
    {
        if (output != kBadPipeValue)
        {
            pipe_close(output);
            const_cast<PipeHandle &>(output) = kBadPipeValue;
        }
    }

    bool pipe_is_blocking(PipeHandle handle)
    {
#ifdef _WIN32
        DWORD state = 0;
        bool success = !!GetNamedPipeHandleStateA(handle, &state, nullptr, nullptr, nullptr, nullptr, 0);
        if (!success)
            return false;
        return (state & PIPE_NOWAIT) != PIPE_NOWAIT;
#endif
        return false;
    }

    ssize_t pipe_peak_bytes(PipeHandle pipe)
    {
#ifdef _WIN32
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
            return -1;
        return available;
#else
        int bytes_available = 0;
        if (ioctl(pipe, FIONREAD, &bytes_available) == -1)
        {
            perror("ioctl");
            return -1;
        }
        return bytes_available;
#endif
    }

#ifdef _WIN32
    void pipe_set_inheritable(PipeHandle handle, bool inheritable)
    {
        if (handle == kBadPipeValue)
            throw std::invalid_argument("pipe_set_inheritable: handle is invalid");
        bool success = !!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, inheritable ? HANDLE_FLAG_INHERIT : 0);
        if (!success)
            throw OSError("SetHandleInformation failed");
    }
    bool pipe_close(PipeHandle handle)
    {
        if (handle == kBadPipeValue)
            return false;
        return !!CloseHandle(handle);
    }
    PipePair pipe_create(bool inheritable)
    {
        SECURITY_ATTRIBUTES security = {0};
        security.nLength = sizeof(security);
        security.bInheritHandle = inheritable;
        PipeHandle input, output;
        bool result = CreatePipe(&input, &output, &security, 0);
        if (!result)
        {
            input = output = kBadPipeValue;
            throw OSError("could not create pipe");
        }
        return {input, output};
    }
    ssize_t pipe_read(PipeHandle handle, void *buffer, std::size_t size)
    {
        DWORD bread = 0;
        bool result = ReadFile(handle, buffer, (DWORD)size, &bread, nullptr);
        DWORD error = GetLastError();
        if (!result && error == ERROR_NO_DATA)
            return 0;
        if (result)
            return bread;
        return -1;
    }
    ssize_t pipe_write(PipeHandle handle, const void *buffer, size_t size)
    {
        DWORD written = 0;
        bool result = WriteFile(handle, buffer, (DWORD)size, &written, nullptr);
        if (result)
            return written;
        return -1;
    }
    bool pipe_set_blocking(PipeHandle handle, bool should_block)
    {
        DWORD state = 0;
        bool success = !!GetNamedPipeHandleStateA(handle, &state, nullptr, nullptr, nullptr, nullptr, 0);
        if (!success)
            return false;
        if (should_block)
            state &= ~PIPE_NOWAIT;
        else
            state |= PIPE_NOWAIT;
        success = !!SetNamedPipeHandleState(handle, &state, nullptr, nullptr);
        return success;
    }
    int pipe_wait_for_read(PipeHandle pipe, double seconds)
    {
        struct TmpBlock
        {
                TmpBlock(PipeHandle pipe) : pipe(pipe)
                {
                    blocking = pipe_is_blocking(pipe);
                    pipe_set_blocking(pipe, true);
                }
                ~TmpBlock()
                {
                    pipe_set_blocking(pipe, blocking);
                }
                PipeHandle pipe;
                bool blocking = true;
        };
        TmpBlock tmp_block(pipe);
        int iterations = 0;
        do
        {
            ++iterations;
            double start = monotonic_seconds();
            DWORD dw_timeout = (seconds < 0) ? INFINITE : seconds * 1000.0;
            DWORD result = WaitForSingleObject(pipe, dw_timeout);
            if (result == WAIT_OBJECT_0)
            {
                DWORD available = 0;
                if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
                    return -1;
                if (available > 0)
                    return 1;
                if (seconds < 0)
                    continue;
                double waited = monotonic_seconds() - start;
                seconds -= waited;
                if (seconds <= 0)
                    return 0;
            }
            else if (result == WAIT_TIMEOUT)
            {
                return 0;
            }
            else
            {
                break;
            }
        } while (true);
        return -1;
    }
#else
    void pipe_set_inheritable(PipeHandle handle, bool inherits)
    {
        if (handle == kBadPipeValue)
            throw std::invalid_argument("pipe_set_inheritable: handle is invalid");
        int flags = fcntl(handle, F_GETFD);
        if (flags < 0)
            throw_os_error("fcntl", errno);
        if (inherits)
            flags &= ~FD_CLOEXEC;
        else
            flags |= FD_CLOEXEC;
        int result = fcntl(handle, F_SETFD, flags);
        if (result < -1)
            throw_os_error("fcntl", errno);
    }
    bool pipe_close(PipeHandle handle)
    {
        if (handle == kBadPipeValue)
            return false;
        return ::close(handle) == 0;
    }
    PipePair pipe_create(bool inheritable)
    {
        int fd[2];
        bool success = !::pipe(fd);
        if (!success)
        {
            throw_os_error("pipe", errno);
            return {};
        }
        if (!inheritable)
        {
            pipe_set_inheritable(fd[0], false);
            pipe_set_inheritable(fd[1], false);
        }
        return {fd[0], fd[1]};
    }
    ssize_t pipe_read(PipeHandle handle, void *buffer, size_t size)
    {
        ssize_t transferred = ::read(handle, buffer, size);
        if (transferred < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
        }
        return transferred;
    }
    ssize_t pipe_write(PipeHandle handle, const void *buffer, size_t size)
    {
        ssize_t transferred = ::write(handle, buffer, size);
        if (transferred < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
        }
        return transferred;
    }
    bool pipe_set_blocking(PipeHandle handle, bool should_block)
    {
        int state = fcntl(handle, F_GETFL);
        if (should_block)
            state &= ~O_NONBLOCK;
        else
            state |= O_NONBLOCK;
        return fcntl(handle, F_SETFL, state) == 0;
    }
    int pipe_wait_for_read(PipeHandle pipe, double seconds)
    {
        pollfd pfd = {};
        pfd.fd = pipe;
        pfd.events = POLLIN;
        int ms = (seconds < 0) ? -1 : seconds * 1000.0;
        int ret = poll(&pfd, 1, ms);
        if (ret > 0)
        {
            if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
                return -1;
            return 1;
        }
        if (ret == 0)
            return 0;
        return -1;
    }
#endif

    std::string pipe_read_all(PipeHandle handle)
    {
        if (handle == kBadPipeValue)
            return {};
        constexpr int buf_size = 2048;
        std::uint8_t buf[buf_size];
        std::string result;
        while (true)
        {
            ssize_t transfered = pipe_read(handle, buf, buf_size);
            if (transfered > 0)
                result.insert(result.end(), &buf[0], &buf[transfered]);
            else
                break;
        }
        return result;
    }

    void pipe_ignore_and_close(PipeHandle handle)
    {
        if (handle == kBadPipeValue)
            return;
        std::thread thread([handle]() {
            std::vector<std::uint8_t> buffer(1024);
            while (pipe_read(handle, &buffer[0], buffer.size()) >= 0)
            {
            }
            pipe_close(handle);
        });
        thread.detach();
    }

    ssize_t pipe_read_some(PipeHandle pipe, void *buffer, size_t size)
    {
        ssize_t transferred = pipe_read(pipe, buffer, 1);
        if (transferred <= 0)
            return transferred;
        int available = pipe_peak_bytes(pipe);
        --size;
        buffer = reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(buffer) + 1);
        size = std::min<ssize_t>(size, available);
        ssize_t second = pipe_read(pipe, buffer, size);
        if (second < 0)
            return second;
        return second + 1;
    }

    ssize_t pipe_write_fully(PipeHandle handle, const void *buffer, size_t size)
    {
        ssize_t transferred = 0;
        ssize_t total = 0;
        const std::uint8_t *cursor = reinterpret_cast<const std::uint8_t *>(buffer);
        while (total < size)
        {
            ssize_t transferred = pipe_write(handle, cursor, size - total);
            if (transferred < 0)
                return -total - 1;
            if (transferred == 0)
                break;
            cursor += transferred;
            total += transferred;
        }
        return total;
    }

    PipeHandle pipe_file(const char *filename, const char *mode)
    {
        using std::strchr;
#ifdef _WIN32
        static_assert(sizeof(wchar_t) == 2, "wchar_t must be of size 2");
        static_assert(sizeof(wchar_t) == sizeof(char16_t), "wchar_t must be of size 2");
        DWORD dwDesiredAccess = 0;
        DWORD disposition = 0;
        if (strchr(mode, 'r'))
        {
            dwDesiredAccess |= GENERIC_READ;
            disposition = OPEN_EXISTING;
        }
        if (strchr(mode, 'w'))
        {
            dwDesiredAccess |= GENERIC_WRITE;
            disposition = CREATE_ALWAYS;
        }
        if (strchr(mode, '+'))
            dwDesiredAccess |= GENERIC_READ | GENERIC_WRITE;
        std::u16string str = utf8_to_utf16(filename);
        HANDLE hFile = CreateFileW(
            reinterpret_cast<LPCWSTR>(str.c_str()),
            dwDesiredAccess, 0, NULL, disposition,
            FILE_ATTRIBUTE_NORMAL, NULL
        );
        HANDLE hFile = CreateFileA(filename, dwDesiredAccess, 0, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return kBadPipeValue;
        return hFile;
#else
        int flags = 0;
        if (strchr(mode, 'r'))
            flags = O_RDONLY;
        if (strchr(mode, 'w'))
            flags |= O_WRONLY | O_CREAT | O_TRUNC;
        if (strchr(mode, '+'))
            flags |= O_RDWR;
        auto fd = open(filename, flags, 0666);
        if (fd == -1)
            return kBadPipeValue;
        return fd;
#endif
    }

} // namespace subprocess
