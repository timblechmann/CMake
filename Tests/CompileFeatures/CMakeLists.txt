
cmake_minimum_required(VERSION 3.0)

project(CompileFeatures)

if (NOT CMAKE_CXX_COMPILE_FEATURES AND NOT CMAKE_C_COMPILE_FEATURES)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp"
    "int main(int,char**) { return 0; }\n"
  )
  add_executable(CompileFeatures "${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp")
  return()
endif()

macro(run_test feature lang)
  if (";${CMAKE_${lang}_COMPILE_FEATURES};" MATCHES ${feature})
    add_library(test_${feature} OBJECT ${feature})
    set_property(TARGET test_${feature}
      PROPERTY COMPILE_FEATURES "${feature}"
    )
  else()
    list(APPEND non_features_${lang} ${feature})
  endif()
endmacro()

macro(run_cxx_test)
  run_test(${ARGN} CXX)
endmacro()
macro(run_c_test)
  run_test(${ARGN} C)
endmacro()

get_property(features GLOBAL PROPERTY CMAKE_CXX_KNOWN_FEATURES)
foreach(feature ${features})
  run_cxx_test(${feature})
endforeach()
get_property(features GLOBAL PROPERTY CMAKE_C_KNOWN_FEATURES)
foreach(feature ${features})
  run_c_test(${feature})
endforeach()

if (CMAKE_CXX_COMPILE_FEATURES)
  include(CheckCXXSourceCompiles)
  foreach(feature ${non_features_CXX})
    check_cxx_source_compiles("#include \"${CMAKE_CURRENT_SOURCE_DIR}/${feature}.cpp\"\nint main() { return 0; }\n" ${feature}_works)
    if (${feature}_works)
      message(SEND_ERROR
        "Feature ${feature} expected not to work for ${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}.  Update the supported features or blacklist it.")
    endif()
  endforeach()
endif()

if (CMAKE_C_COMPILE_FEATURES)
  include(CheckCSourceCompiles)
  foreach(feature ${non_features_C})
    check_c_source_compiles("#include \"${CMAKE_CURRENT_SOURCE_DIR}/${feature}.c\"\nint main() { return 0; }\n" ${feature}_works)
    if (${feature}_works)
      message(SEND_ERROR
        "Feature ${feature} expected not to work for ${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}.  Update the supported features or blacklist it.")
    endif()
  endforeach()
endif()

add_executable(CompileFeatures main.cpp)
set_property(TARGET CompileFeatures
  PROPERTY COMPILE_FEATURES "cxx_auto_type"
)
set_property(TARGET CompileFeatures
  PROPERTY CXX_STANDARD_REQUIRED TRUE
)

add_executable(GenexCompileFeatures main.cpp)
set_property(TARGET GenexCompileFeatures
  PROPERTY COMPILE_FEATURES "$<1:cxx_auto_type>;$<0:not_a_feature>"
)

add_library(iface INTERFACE)
set_property(TARGET iface
  PROPERTY INTERFACE_COMPILE_FEATURES "cxx_auto_type"
)
add_executable(IfaceCompileFeatures main.cpp)
target_link_libraries(IfaceCompileFeatures iface)
