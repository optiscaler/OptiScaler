#include "scanner.h"

#include <proxies/KernelBase_Proxy.h>

struct SectionRange
{
    BYTE *start, *end;
};

std::vector<SectionRange> GetExecSections(HMODULE hMod)
{
    std::vector<SectionRange> secs;

    if (hMod == nullptr)
        return secs;

    BYTE* base = reinterpret_cast<BYTE*>(hMod);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto first = IMAGE_FIRST_SECTION(nt);

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        auto& s = first[i];

        // filter for executable
        if (s.Characteristics & IMAGE_SCN_MEM_EXECUTE)
        {
            BYTE* start = base + s.VirtualAddress;
            BYTE* end = start + s.Misc.VirtualSize;
            secs.push_back({ start, end });
        }
    }

    return secs;
}

uintptr_t FindPattern(uintptr_t startAddress, uintptr_t maxSize, const char* mask)
{
    std::vector<std::pair<uint8_t, bool>> pattern;

    for (size_t i = 0; i < strlen(mask);)
    {
        if (mask[i] != '?')
        {
            pattern.emplace_back(static_cast<uint8_t>(strtoul(&mask[i], nullptr, 16)), false);
            i += 3;
        }
        else
        {
            pattern.emplace_back(0x00, true);
            i += 2;
        }
    }

    const auto dataStart = reinterpret_cast<const uint8_t*>(startAddress);
    const auto dataEnd = dataStart + maxSize + 1;

    auto sig = std::search(dataStart, dataEnd, pattern.begin(), pattern.end(),
                           [](uint8_t currentByte, std::pair<uint8_t, bool> Pattern)
                           { return Pattern.second || (currentByte == Pattern.first); });

    if (sig == dataEnd)
        return NULL;

    return std::distance(dataStart, sig) + startAddress;
}

uintptr_t scanner::GetAddress(const std::wstring_view moduleName, const std::string_view pattern, ptrdiff_t offset,
                              uintptr_t startAddress)
{
    uintptr_t address = NULL;
    auto module = KernelBaseProxy::GetModuleHandleW_()(moduleName.data());

    if (module == nullptr)
        return NULL;

    auto sections = GetExecSections(module);

    if (startAddress != 0)
    {
        for (size_t i = 0; i < sections.size(); i++)
        {
            auto section = &sections[i];

            if (((uintptr_t) section->start < startAddress && (uintptr_t) section->end > startAddress))
            {
                address = FindPattern(startAddress, (uintptr_t) section->end - startAddress, pattern.data());

                if (address != NULL)
                    break;
            }
            else if ((uintptr_t) section->start > startAddress)
            {
                address = FindPattern((uintptr_t) section->start, (uintptr_t) section->end - (uintptr_t) section->start,
                                      pattern.data());

                if (address != NULL)
                    break;
            }
        }
    }
    else
    {
        for (size_t i = 0; i < sections.size(); i++)
        {
            auto section = &sections[i];
            address = FindPattern((uintptr_t) section->start, (uintptr_t) section->end - (uintptr_t) section->start,
                                  pattern.data());

            if (address != NULL)
                break;
        }
    }

    if (address != NULL)
    {
        return (address + offset);
    }
    else
    {
        return NULL;
    }
}

uintptr_t scanner::GetOffsetFromInstruction(const std::wstring_view moduleName, const std::string_view pattern,
                                            ptrdiff_t offset)
{
    auto module = KernelBaseProxy::GetModuleHandleW_()(moduleName.data());

    if (module == nullptr)
        return NULL;

    uintptr_t address = NULL;

    auto sections = GetExecSections(module);

    for (size_t i = 0; i < sections.size(); i++)
    {
        auto section = &sections[i];
        address = FindPattern((uintptr_t) section->start, (uintptr_t) section->end - (uintptr_t) section->start,
                              pattern.data());

        if (address != NULL)
            break;
    }

    if (address != NULL)
    {
        auto reloffset = *reinterpret_cast<int32_t*>(address + offset) + sizeof(int32_t);
        return (address + offset + reloffset);
    }
    else
    {
        return NULL;
    }
}
