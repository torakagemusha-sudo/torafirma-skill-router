cmake_minimum_required(VERSION 3.18)
project(skillrouter_python LANGUAGES C CXX)

set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)

add_library(skillrouter_sqlite STATIC
  ../../skill-router/third_party/sqlite3.c
)
target_compile_definitions(skillrouter_sqlite PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_ENABLE_FTS5
)
set_target_properties(skillrouter_sqlite PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

pybind11_add_module(skillrouter_native
  skillrouter_pybind.cpp
)
target_compile_features(skillrouter_native PRIVATE cxx_std_20)
target_include_directories(skillrouter_native PRIVATE
  ../../skill-router
)
target_link_libraries(skillrouter_native PRIVATE skillrouter_sqlite)

if(UNIX AND NOT APPLE)
  target_link_libraries(skillrouter_native PRIVATE pthread dl)
endif()

install(TARGETS skillrouter_native
  LIBRARY DESTINATION .
  RUNTIME DESTINATION .
)
