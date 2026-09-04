cmake_minimum_required(VERSION 3.20)
project(dlssnr_forwarder CXX)
set(CMAKE_CXX_STANDARD 17)

# The name is the whole point. The Neural Rendering snippet resolves the module that owns its caller's
# return address and refuses anything whose path does not contain "nvngx.dll", so the calls have to
# originate from a module named like this one. Nothing else about it is special.
add_library(dlssnr_forwarder SHARED dlssnr_forwarder.cpp)
target_link_libraries(dlssnr_forwarder PRIVATE d3d12)
set_target_properties(dlssnr_forwarder PROPERTIES
    OUTPUT_NAME "nvngx.dll_dlssnr" SUFFIX ".dll" PREFIX "")
