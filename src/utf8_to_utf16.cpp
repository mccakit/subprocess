module;

#ifdef _WIN32
#ifndef __MINGW32__
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cassert>

export module subprocess:utf8_to_utf16;

import std;

export namespace subprocess
{
#ifdef _WIN32
    std::u16string utf8_to_utf16(const std::string &str);
    std::string utf16_to_utf8(const std::u16string &str);
    std::string utf16_to_utf8(const std::wstring &str);
    size_t strlen16(char16_t *str);
    size_t strlen16(wchar_t *str);
    std::string lptstr_to_string(LPTSTR str);
#endif
} // namespace subprocess

#ifdef _WIN32
namespace subprocess
{

    std::u16string utf8_to_utf16(const std::string &string)
    {
        static_assert(sizeof(wchar_t) == 2, "wchar_t must be of size 2");
        static_assert(sizeof(wchar_t) == sizeof(char16_t), "wchar_t must be of size 2");
        int size = (int)string.size() + 1;
        int r = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, string.c_str(), size, NULL, 0);
        if (r == 0)
            return {};
        assert(r > 0);

        wchar_t *wstring = new wchar_t[r];
        if (wstring == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return {};
        }

        r = MultiByteToWideChar(CP_UTF8, 0, string.c_str(), size, wstring, r);
        if (r == 0)
        {
            delete[] wstring;
            return {};
        }
        std::u16string result((char16_t *)wstring, r - 1);
        delete[] wstring;
        return result;
    }

#ifndef WC_ERR_INVALID_CHARS
    constexpr int WC_ERR_INVALID_CHARS = 0;
#endif

    std::string utf16_to_utf8(const std::u16string &wstring)
    {
        int size = (int)wstring.size() + 1;
        int r =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, (wchar_t *)wstring.c_str(), size, NULL, 0, NULL, NULL);
        if (r == 0)
            return {};
        assert(r > 0);

        char *string = new char[r];
        if (string == nullptr)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return {};
        }

        r = WideCharToMultiByte(CP_UTF8, 0, (wchar_t *)wstring.c_str(), size, string, r, NULL, NULL);
        if (r == 0)
        {
            delete[] string;
            return {};
        }
        std::string result(string, r - 1);
        delete[] string;
        return result;
    }

    std::string utf16_to_utf8(const std::wstring &wstring)
    {
        int size = (int)wstring.size() + 1;
        int r =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, (wchar_t *)wstring.c_str(), size, NULL, 0, NULL, NULL);
        if (r == 0)
            return {};
        assert(r > 0);

        char *string = new char[r];
        if (string == nullptr)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return {};
        }

        r = WideCharToMultiByte(CP_UTF8, 0, (wchar_t *)wstring.c_str(), size, string, r, NULL, NULL);
        if (r == 0)
        {
            delete[] string;
            return {};
        }
        std::string result(string, r - 1);
        delete[] string;
        return result;
    }

    size_t strlen16(char16_t *str)
    {
        size_t size = 0;
        for (; *str; ++str)
            ++size;
        return size;
    }

    size_t strlen16(wchar_t *str)
    {
        size_t size = 0;
        for (; *str; ++str)
            ++size;
        return size;
    }

    template <typename T> std::string lptstr_to_string_t(T str)
    {
        if (str == nullptr)
            return "";
        if constexpr (sizeof(*str) == 1)
        {
            static_assert(sizeof(*str) == 1);
            return (const char *)str;
        }
        else
        {
            static_assert(sizeof(*str) == 2);
            return utf16_to_utf8((const wchar_t *)str);
        }
    }

    std::string lptstr_to_string(LPTSTR str)
    {
        return lptstr_to_string_t(str);
    }

} // namespace subprocess
#endif
