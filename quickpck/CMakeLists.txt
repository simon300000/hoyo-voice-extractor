cmake_minimum_required(VERSION 3.20)

project(hoyo_pck_extract LANGUAGES CXX)

add_executable(hoyo-pck-extract src/main.cpp)
target_compile_features(hoyo-pck-extract PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(hoyo-pck-extract PRIVATE /W4 /O2)
else()
  target_compile_options(hoyo-pck-extract PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

install(TARGETS hoyo-pck-extract RUNTIME DESTINATION bin)
