
set(_oldestSupported "(__GNUC__ * 100 + __GNUC_MINOR__) >= 407")

set(_cmake_compiler_test_macro __GNUC__)

set(GNU46_C11 "${_oldestSupported} && __STDC_VERSION__ >= 201112L")
set(_cmake_feature_test_c_static_assert "${GNU46_C11}")
# Since 4.4 at least:
set(GNU44_C99 "${_oldestSupported} && __STDC_VERSION__ >= 199901L")
set(_cmake_feature_test_c_restrict "${GNU44_C99}")
set(_cmake_feature_test_c_variadic_macros "${GNU44_C99}")

set(GNU_C90 "${_oldestSupported} && defined(__STDC__)")
set(_cmake_feature_test_c_function_prototypes "${GNU_C90}")
