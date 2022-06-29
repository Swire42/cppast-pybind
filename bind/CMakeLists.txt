# Copyright (C) 2017-2022 Jonathan Müller and cppast contributors
# SPDX-License-Identifier: MIT
# found in the top-level directory of this distribution.

add_executable(cppast_bind main.cpp pb.cpp pb.hpp)
target_link_libraries(cppast_bind PUBLIC cppast cxxopts)
set_target_properties(cppast_bind PROPERTIES CXX_STANDARD 11 OUTPUT_NAME bind)
