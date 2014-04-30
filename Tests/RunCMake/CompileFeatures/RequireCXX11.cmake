
foreach(standard 98 11)
  unset(CMAKE_CXX${standard}_STANDARD_COMPILE_OPTION)
  unset(CMAKE_CXX${standard}_EXTENSION_COMPILE_OPTION)
endforeach()

add_library(foo empty.cpp)
set_property(TARGET foo PROPERTY CXX_STANDARD 11)
set_property(TARGET foo PROPERTY CXX_STANDARD_REQUIRED TRUE)
