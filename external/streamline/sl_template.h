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

namespace sl
{

//! Each feature must have a unique id, please see sl.h Feature
//! 
constexpr uint32_t kFeatureTemplate = 0xffff;

//! If your plugin does not have any constants then the code below can be removed
//! 
enum class TemplateMode : uint32_t
{
    eOff,
    eOn
};

//! IMPORTANT: Each structure must have a unique GUID assigned, change this as needed
//!
// {29DF7FE0-273A-4D72-B481-2DC823D5B1AD}
SL_STRUCT_BEGIN(TemplateConstants, StructType({ 0x29df7fe0, 0x273a, 0x4d72, { 0xb4, 0x81, 0x2d, 0xc8, 0x23, 0xd5, 0xb1, 0xad } }), kStructVersion1)
    TemplateMode mode = TemplateMode::eOff;

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

//! IMPORTANT: Each structure must have a unique GUID assigned, change this as needed
//!
// {39DF7FE0-283A-4D72-B481-2DC823D5B1AD}
SL_STRUCT_BEGIN(TemplateSettings, StructType({ 0x39df7fe0, 0x283a, 0x4d72, { 0xb4, 0x81, 0x2d, 0xc8, 0x23, 0xd5, 0xb1, 0xad } }), kStructVersion1)
    
    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

}
