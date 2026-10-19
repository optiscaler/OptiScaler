#include "pch.h"
#include "rtti.h"

namespace
{
// x64 MSVC RTTI structures, all references inside are RVAs
struct TypeDescriptor
{
    void* vtable;
    void* spare;
    char name[1]; // ".?AV<className>@@"
};

struct CompleteObjectLocator
{
    uint32_t signature; // 1 on x64
    uint32_t offset;    // Offset of this vtable in the complete object, 0 for the most derived one
    uint32_t constructorDisplacementOffset;
    uint32_t typeDescriptor;
    uint32_t classHierarchyDescriptor;
    uint32_t self;
};

enum class SectionKind
{
    Code,
    ReadOnlyData,
    WritableData,
};

std::vector<std::span<const uint8_t>> GetSections(HMODULE module, SectionKind kind)
{
    std::vector<std::span<const uint8_t>> sections;

    auto base = reinterpret_cast<const uint8_t*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
    {
        const bool code = section->Characteristics & IMAGE_SCN_MEM_EXECUTE;
        const bool writable = section->Characteristics & IMAGE_SCN_MEM_WRITE;

        const auto sectionKind =
            code ? SectionKind::Code : (writable ? SectionKind::WritableData : SectionKind::ReadOnlyData);

        if (sectionKind == kind)
            sections.emplace_back(base + section->VirtualAddress, section->Misc.VirtualSize);
    }

    return sections;
}

const TypeDescriptor* FindTypeDescriptor(HMODULE module, std::string_view className)
{
    // Classes and structs only differ in the prefix
    for (auto prefix : { ".?AV", ".?AU" })
    {
        // The terminator keeps "Foo" from matching "Foo_Bar"
        auto mangledName = std::format("{}{}@@", prefix, className);
        std::string_view needle(mangledName.c_str(), mangledName.size() + 1);

        for (auto section : GetSections(module, SectionKind::WritableData))
        {
            std::string_view data(reinterpret_cast<const char*>(section.data()), section.size());

            if (auto position = data.find(needle); position != std::string_view::npos)
            {
                auto name = data.data() + position;
                return reinterpret_cast<const TypeDescriptor*>(name - offsetof(TypeDescriptor, name));
            }
        }
    }

    return nullptr;
}

const CompleteObjectLocator* FindCompleteObjectLocator(HMODULE module, const TypeDescriptor* typeDescriptor)
{
    auto base = reinterpret_cast<const uint8_t*>(module);
    auto typeDescriptorRva = static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(typeDescriptor) - base);

    for (auto section : GetSections(module, SectionKind::ReadOnlyData))
    {
        for (size_t offset = 0; offset + sizeof(CompleteObjectLocator) <= section.size(); offset += alignof(uint32_t))
        {
            auto locator = reinterpret_cast<const CompleteObjectLocator*>(section.data() + offset);
            auto locatorRva = static_cast<uint32_t>(section.data() + offset - base);

            if (locator->signature == 1 && locator->offset == 0 && locator->typeDescriptor == typeDescriptorRva &&
                locator->self == locatorRva)
            {
                return locator;
            }
        }
    }

    return nullptr;
}

} // namespace

void** rtti::FindVtable(HMODULE module, std::string_view className)
{
    if (module == nullptr)
        return nullptr;

    auto typeDescriptor = FindTypeDescriptor(module, className);
    if (typeDescriptor == nullptr)
        return nullptr;

    auto locator = FindCompleteObjectLocator(module, typeDescriptor);
    if (locator == nullptr)
        return nullptr;

    // The entry right before the first virtual function points to the locator
    for (auto section : GetSections(module, SectionKind::ReadOnlyData))
    {
        auto entries = reinterpret_cast<const uintptr_t*>(section.data());
        auto count = section.size() / sizeof(uintptr_t);

        for (size_t i = 0; i + 1 < count; i++)
        {
            if (entries[i] == reinterpret_cast<uintptr_t>(locator))
                return reinterpret_cast<void**>(const_cast<uintptr_t*>(&entries[i + 1]));
        }
    }

    return nullptr;
}

size_t rtti::CountVirtualFunctions(HMODULE module, void** vtable)
{
    if (module == nullptr || vtable == nullptr)
        return 0;

    auto codeSections = GetSections(module, SectionKind::Code);
    auto isCode = [&codeSections](const void* address)
    {
        auto byte = static_cast<const uint8_t*>(address);
        return std::ranges::any_of(codeSections, [byte](auto section)
                                   { return byte >= section.data() && byte < section.data() + section.size(); });
    };

    size_t count = 0;
    while (isCode(vtable[count]))
        count++;

    return count;
}
