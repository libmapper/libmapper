cmake_minimum_required(VERSION 3.19)

set(Libmapper_LIB ${BUILD_DIR}/Debug/libmapper.lib)
set(LIBMAPPER_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../src)
mark_as_advanced(Libmapper_LIB)

# Add file sources
include_directories( 
  "${LIBLO_INCLUDES}"
  "${ZLIB_DIR}/include/zlib"
  "${LIBMAPPER_INCLUDES}"
)

link_directories( 
  "${LIBLO_INCLUDES}"
  "${ZLIB_DIR}/include/zlib"
  "${LIBMAPPER_INCLUDES}"
)

#add_executable (testparams testparams.c)
add_executable (testprops testprops.c)
#add_executable (testgraph testgraph.c)
#add_executable (testparser testparser.c ${LIBMAPPER_SRCS}/mapper_internal.h ${LIBMAPPER_SRCS}/time.c)
add_executable (testnetwork testnetwork.c)
#add_executable (testmany testmany.c ${LIBMAPPER_SRCS}/mapper_internal.h ${LIBMAPPER_SRCS}/time.c)
add_executable (test test.c)
add_executable (testlinear testlinear.c)
add_executable (testexpression testexpression.c)
add_executable (testrate testrate.c)
add_executable (testbundle testbundle.c)
add_executable (testinstance testinstance.c)
add_executable (testreverse testreverse.c)
add_executable (testvector testvector.c)
add_executable (testcustomtransport testcustomtransport.c)
add_executable (testspeed testspeed.c ${LIBMAPPER_SRCS}/mapper_internal.h ${LIBMAPPER_SRCS}/time.c)
#add_executable (testcpp testcpp.cpp)
add_executable (testmapinput testmapinput.c)
add_executable (testconvergent testconvergent.c)
add_executable (testunmap testunmap.c)
add_executable (testmapfail testmapfail.c)
add_executable (testmapprotocol testmapprotocol.c)
add_executable (testcalibrate testcalibrate.c)
add_executable (testlocalmap testlocalmap.c)
add_executable (testsignalhierarchy testsignalhierarchy.c ${LIBMAPPER_SRCS}/mapper_internal.h ${LIBMAPPER_SRCS}/time.c)

#target_link_libraries(testparams PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testprops PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
#target_link_libraries(testgraph PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
#target_link_libraries(testparser PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testnetwork PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
#target_link_libraries(testmany PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(test PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testlinear PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testexpression PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testrate PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testbundle PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testinstance PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testreverse PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testvector PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testcustomtransport PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testspeed PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
#target_link_libraries(testcpp PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testmapinput PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testconvergent PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testunmap PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testmapfail PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testmapprotocol PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testcalibrate PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testlocalmap PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
target_link_libraries(testsignalhierarchy PUBLIC ${Liblo_LIB} ${Zlib_LIB} ${Libmapper_LIB} wsock32.lib ws2_32.lib iphlpapi.lib)
