#pragma once
#include <pch.h>

#include <Unknwnbase.h>

namespace Util
{
namespace Detail
{

// Traits class to determine the pointer type of an std::out_ptr_t or std::inout_ptr_t
// created via std::out_ptr() or std::inout_ptr()
template<typename T>
struct OutInPtrTraits;

template<
    template<typename Smart_, typename Pointer_, typename...> class OutInPtr,
    typename Smart_,
    typename Pointer_,
    typename... Args
>
struct OutInPtrTraits<OutInPtr<Smart_, Pointer_, Args...>>
{
    using Smart = Smart_;
    using Pointer = Pointer_;
};


// Compare combaseapi.h IID_PPV_ARGS_Helper
template<class OutInPtr>
OutInPtr&& IID_OUTPTR_ARGS_Helper(OutInPtr&& outInPtr)
{
    // do compile-time checks on pointer type
    using Pointer = typename OutInPtrTraits<OutInPtr>::Pointer;
    (void) IID_PPV_ARGS_Helper(static_cast<Pointer*>(nullptr));

    // return the void**-like (std::out_ptr_t / std::inout_ptr_t does that for us)
    return std::forward<OutInPtr>(outInPtr);
}


struct IUnknownDeleter
{
    inline void operator()(IUnknown* ptr) const noexcept
    {
        ptr->Release();
    }
};

} // namespace Detail

// Smart pointer that holds ownership of the COM object
// Will Release() the COM object when going out of scope or on re-assignment
// To relinquish ownership and return to manual lifetime management, call .release()
template<typename T>
using ComPtr = std::unique_ptr<T, Detail::IUnknownDeleter>;

} // namespace Util

// Compare combaseapi.h IID_PPV_ARGS
#define IID_OUTPTR_ARGS(outInPtr) \
    __uuidof(typename Util::Detail::OutInPtrTraits<decltype(outInPtr)>::Pointer), \
    Util::Detail::IID_OUTPTR_ARGS_Helper(outInPtr)
