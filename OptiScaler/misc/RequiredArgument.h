#pragma once

// [ProcessFilter] RequiredArgument: OptiScaler only hooks a process whose command line has this one argument, like a
// launch option that starts a game without its anti-cheat. Without it the process gets the pass-through mode of an
// excluded process (original dll loaded and forwarded, no hooks), so starting the game without that option doesn't
// start its protected mode with OptiScaler active.
//
// No Windows headers here, so it can be tested on its own.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RequiredArgument
{
enum class Verdict
{
    NotRequired, // unset (auto) or empty, the command line isn't read
    Present,
    Missing,
    Invalid // not one argument, treated as missing
};

// One argument as a command line can carry it: not empty, no blanks, quotes or control characters (a value that
// needs quoting could never equal a parsed argument)
inline bool IsValid(std::wstring_view token)
{
    if (token.empty() || token.size() > 256)
        return false;

    for (wchar_t c : token)
    {
        if (c <= L' ' || c == L'"' || (c >= 0x7f && c <= 0x9f) || c == 0x2028 || c == 0x2029)
            return false;
    }

    return true;
}

// The arguments after the program name, split as the Microsoft C runtime splits a command line: blanks (space,
// tab) separate arguments outside quotes; 2n backslashes before a quote give n backslashes and the quote opens or
// closes quoting, 2n+1 give n backslashes and a literal quote; other backslashes are literal; "" inside quotes is a
// literal quote. The program name ends at the first blank outside quotes and has no escapes.
inline std::vector<std::wstring> Arguments(std::wstring_view line)
{
    std::vector<std::wstring> out;
    size_t i = 0;
    bool quoted = false;

    while (i < line.size() && (quoted || (line[i] != L' ' && line[i] != L'\t')))
    {
        if (line[i] == L'"')
            quoted = !quoted;

        ++i;
    }

    while (true)
    {
        while (i < line.size() && (line[i] == L' ' || line[i] == L'\t'))
            ++i;

        if (i >= line.size())
            return out;

        std::wstring argument;
        quoted = false;

        while (i < line.size() && (quoted || (line[i] != L' ' && line[i] != L'\t')))
        {
            if (line[i] == L'\\')
            {
                size_t count = 0;
                for (; i < line.size() && line[i] == L'\\'; ++i)
                    ++count;

                if (i < line.size() && line[i] == L'"')
                {
                    argument.append(count / 2, L'\\');

                    if (count % 2 == 1)
                    {
                        argument += L'"';
                        ++i;
                    }
                }
                else
                {
                    argument.append(count, L'\\');
                }
            }
            else if (line[i] == L'"')
            {
                if (quoted && i + 1 < line.size() && line[i + 1] == L'"')
                {
                    argument += L'"';
                    i += 2;
                }
                else
                {
                    quoted = !quoted;
                    ++i;
                }
            }
            else
            {
                argument += line[i++];
            }
        }

        out.push_back(std::move(argument));
    }
}

// Whole arguments only, case-insensitive for ASCII letters
inline bool Present(std::wstring_view line, std::wstring_view token)
{
    auto lower = [](wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c; };

    for (const auto& argument : Arguments(line))
    {
        if (argument.size() != token.size())
            continue;

        size_t j = 0;
        while (j < token.size() && lower(argument[j]) == lower(token[j]))
            ++j;

        if (j == token.size())
            return true;
    }

    return false;
}

// readCommandLine() is only called when an argument is required and valid
template <typename ReadCommandLine>
inline Verdict Check(const std::optional<std::wstring>& required, ReadCommandLine&& readCommandLine)
{
    if (!required.has_value() || required->empty())
        return Verdict::NotRequired;

    if (!IsValid(*required))
        return Verdict::Invalid;

    return Present(readCommandLine(), *required) ? Verdict::Present : Verdict::Missing;
}
} // namespace RequiredArgument
