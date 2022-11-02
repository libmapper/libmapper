cmake_minimum_required(VERSION 3.19)

# Replace with your own global paths before building and modify build directories if different from below
set(BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build")
set(LIBMAPPER_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(LIBLO_DIR "${BUILD_DIR}/liblo/liblo-master")
set(LIBLO_BUILD_DIR "${LIBLO_DIR}/cmake/build/Release")
set(ZLIB_DIR "${BUILD_DIR}/zlib/msvc2017_64")
set(ZLIB_BUILD_DIR "${ZLIB_DIR}/lib/zlib")
set(LIBLO_INCLUDES "${LIBLO_DIR}/cmake/build;${LIBLO_DIR}")

project(libmapper)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${BUILD_DIR}/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BUILD_DIR}/Release")

if (WIN32)
  set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb/$<CONFIG>")

  set(Liblo_LIB ${LIBLO_BUILD_DIR}/liblo.lib)
  set(Zlib_LIB ${ZLIB_BUILD_DIR}/zlib.lib)

  mark_as_advanced(Liblo_LIB)
  mark_as_advanced(Zlib_LIB)

  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

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
  "${ZLIB_DIR}/include/zlib"
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

add_subdirectory(test)