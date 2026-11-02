#pragma once

#include <type_traits>

// For getting function signature of methods, example of usage:
// using PFN_CheckFeatureSupport = rewrite_signature<decltype(&ID3D12Device::CheckFeatureSupport)>::type;
template <typename T> struct rewrite_signature;
template <typename Ret, typename Class, typename... Args> struct rewrite_signature<Ret (Class::*)(Args...)>
{
    using type = Ret(WINAPI*)(Class*, Args...);
};

// Same as VALIDATE_HOOK but for class members, put after function declaration
#define VALIDATE_MEMBER_HOOK(HookName, PfnType)                                                                        \
    static_assert(std::is_same_v<decltype(&HookName), PfnType>,                                                        \
                  "Signature mismatch: " #HookName " does not match " #PfnType);

// For checking that the hooked function's signature matches the original
// Place just above function definition, example of usage:
// VALIDATE_HOOK(hkCheckFeatureSupport, PFN_CheckFeatureSupport)
#define VALIDATE_HOOK(HookName, PfnType)                                                                               \
    extern std::remove_pointer_t<PfnType> HookName;                                                                    \
    VALIDATE_MEMBER_HOOK(HookName, PfnType)
