
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
add_library(bar empty.cpp)
set_property(TARGET bar PROPERTY CXX_STANDARD 98)
