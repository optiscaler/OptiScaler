/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/ 

#pragma once

#include <stdint.h>
#include <string.h>

namespace sl
{

//! GUID
struct StructType
{
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];

    inline bool operator==(const StructType& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
    inline bool operator!=(const StructType& rhs) const { return memcmp(this, &rhs, sizeof(*this)) != 0; }
};

//! SL is using typed and versioned structures which can be chained or not.
//! 
//! --- OPTION 1 ---
//! 
//! New members must be added at the end and version needs to be increased:
//! 
//! SL_STRUCT_BEGIN(S1, GUID1, kStructVersion1)
//!     A
//!     B
//!     C
//! SL_STRUCT_END()
//! 
//! SL_STRUCT_BEGIN(S1, GUID1, kStructVersion2) // Note that version is bumped
//!     // V1
//!     A
//!     B
//!     C
//! 
//!     //! V2 - new members always go at the end!
//!     D
//!     E
//! SL_STRUCT_END()
//! 
//! Here is one example on how to check for version and handle backwards compatibility:
//! 
//! void func(const S1* input)
//! {
//!     // Access A, B, C as needed
//!     ...
//!     if (input->structVersion >= kStructVersion2)
//!     {
//!         // Safe to access D, E
//!     }
//! }


//! --- OPTION 2 ---
//! 
//! New members are optional and added to a new struct which is then chained as needed:
//! 
//! SL_STRUCT_BEGIN(S1, GUID1, kStructVersion1)
//!     A
//!     B
//!     C
//! SL_STRUCT_END()
//! 
//! SL_STRUCT_BEGIN(S2, GUID2, kStructVersion1) // Note that this is a different struct with new GUID
//!     D
//!     E
//! SL_STRUCT_END()
//! 
//! S1 s1;
//! S2 s2
//! s1.next = &s2; // optional parameters in S2

//! IMPORTANT: New members in the structure always go at the end!
//!
constexpr uint32_t kStructVersion1 = 1;
constexpr uint32_t kStructVersion2 = 2;
constexpr uint32_t kStructVersion3 = 3;
constexpr uint32_t kStructVersion4 = 4;

struct BaseStructure
{
    BaseStructure() = delete;
    BaseStructure(StructType t, uint32_t v) : structType(t), structVersion(v) {};
    BaseStructure* next{};
    StructType structType{};
    size_t structVersion;
};

#define SL_STRUCT_BEGIN(name, guid, version)                                \
struct name : public sl::BaseStructure                                      \
{                                                                           \
    name() : sl::BaseStructure(guid, version){}                             \
    constexpr static sl::StructType s_structType = guid;

#define SL_STRUCT_END() };

#define SL_STRUCT_PROTECTED_BEGIN(name, guid, version)                      \
struct name : public sl::BaseStructure                                      \
{                                                                           \
protected:                                                                  \
    name() : sl::BaseStructure(guid, version){}                             \
public:                                                                     \
    constexpr static sl::StructType s_structType = guid;                    \


// Deprecated: please use SL_STRUCT_BEGIN/SL_STRUCT_END instead
#define SL_STRUCT(name, guid, version)                                      \
SL_STRUCT_BEGIN(name, guid, version)

// Deprecated: please use SL_STRUCT_PROTECTED_BEGIN/SL_STRUCT_END instead
#define SL_STRUCT_PROTECTED(name, guid, version)                            \
SL_STRUCT_PROTECTED_BEGIN(name, guid, version)

} // namespace sl
