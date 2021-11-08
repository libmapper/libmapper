cmake_minimum_required(VERSION 3.19)

# Replace with your own global paths before building and modify build directories if different from below
set(BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build")
set(LIBMAPPER_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(LIBLO_DIR "<your_liblo_root>")
set(LIBLO_BUILD_DIR "${LIBLO_DIR}/cmake/build/Debug")
set(ZLIB_DIR "<your_zlib_root>")
set(ZLIB_BUILD_DIR "${ZLIB_DIR}/build/native/lib_debug")
set(LIBLO_INCLUDES "${LIBLO_DIR}/cmake/build;${LIBLO_DIR}")

if (WIN32)
  # These must be prior to the "project" command
  # https://stackoverflow.com/questions/14172856/compile-with-mt-instead-of-md-using-cmake

  if (CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_C_FLAGS_DEBUG            "/D_DEBUG /MTd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_C_FLAGS_MINSIZEREL       "/MT /O1 /Ob1 /D NDEBUG")
    set(CMAKE_C_FLAGS_RELEASE          "/MT /O2 /Ob2 /D NDEBUG")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO   "/MT /Zi /O2 /Ob1 /D NDEBUG")

    set(CMAKE_CXX_FLAGS_DEBUG          "/D_DEBUG /MTd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_CXX_FLAGS_MINSIZEREL     "/MT /O1 /Ob1 /D NDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE        "/MT /O2 /Ob2 /D NDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MT /Zi /O2 /Ob1 /D NDEBUG")

    add_compile_options(
      $<$<CONFIG:>:/MT>
      $<$<CONFIG:Debug>:/MTd>
      $<$<CONFIG:Release>:/MT>
      $<$<CONFIG:MinSizeRel>:/MT>
      $<$<CONFIG:RelWithDebInfo>:/MT>
    )   
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
  endif ()
endif ()

project(libmapper)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${BUILD_DIR}/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BUILD_DIR}/Release")

if (WIN32)
  set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb/$<CONFIG>")

  set(Liblo_LIB ${LIBLO_BUILD_DIR}/liblo.lib)
  set(Zlib_LIB ${ZLIB_BUILD_DIR}/zlibd.lib)

  mark_as_advanced(Liblo_LIB)
  mark_as_advanced(Zlib_LIB)

  add_definitions(
    -DWIN_VERSION
    -D_USE_MATH_DEFINES
    -D_WINSOCK_DEPRECATED_NO_WARNINGS
    -DHAVE_WINSOCK2_H
    -DNODEFAULTLIB
    -DHAVE_CONFIG_H
    -D_CRT_SECURE_NO_WARNINGS
    -D_CRT_SECURE_NO_DEPRECATE
    -D_CRT_NONSTDC_NO_DEPRECATE
  )
else ()
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
  set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
endif ()

# Add file sources
include_directories( 
  "${LIBLO_INCLUDES}"
  "${ZLIB_DIR}/include"
  "${LIBMAPPER_INCLUDES}"
)

file(GLOB PROJECT_SRC
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.def"
)
add_library( 
  ${PROJECT_NAME} 
  MODULE
  ${PROJECT_SRC}
)

# Post-target configuration

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "libmapper")

if (WIN32)
  target_link_libraries(${PROJECT_NAME} PUBLIC ${Liblo_LIB})
  target_link_libraries(${PROJECT_NAME} PUBLIC ${Zlib_LIB})
  target_link_libraries(${PROJECT_NAME} PUBLIC wsock32.lib ws2_32.lib iphlpapi.lib)
  
  set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".dll")

  if (CMAKE_GENERATOR MATCHES "Visual Studio")
    # warning about constexpr not being const in c++14
    set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/wd4814")

    # do not generate ILK files
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/INCREMENTAL:NO")

    # allow parallel builds
    set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/MP")
  endif ()

  if (EXCLUDE_FROM_COLLECTIVES STREQUAL "yes")
    target_compile_definitions(${PROJECT_NAME} PRIVATE "-DEXCLUDE_FROM_COLLECTIVES")
  endif()

  if (ADD_VERINFO)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/verinfo.rc)
  endif()
endif ()


