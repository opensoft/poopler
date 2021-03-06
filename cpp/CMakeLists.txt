include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${ICONV_INCLUDE_DIR}
)

configure_file(poppler-version.h.in ${CMAKE_CURRENT_BINARY_DIR}/poppler-version.h @ONLY)

add_subdirectory(tests)

set(poppler_cpp_HEADERS
  poppler-destination.h
  poppler-document.h
  poppler-embedded-file.h
  poppler-font.h
  poppler-global.h
  poppler-image.h
  poppler-page.h
  poppler-page-renderer.h
  poppler-page-transition.h
  poppler-rectangle.h
  poppler-toc.h
  poppler-extern-draw.h
  ${CMAKE_CURRENT_BINARY_DIR}/poppler-version.h
)

set(poppler_cpp_Private_HEADERS
  poppler-document-private.h
  poppler-embedded-file-private.h
  poppler-image-private.h
  poppler-page-private.h
  poppler-private.h
  poppler-toc-private.h
  ExternDrawOutputDevice.h
)

set(poppler_cpp_SRCS
  poppler-destination.cpp
  poppler-document.cpp
  poppler-embedded-file.cpp
  poppler-font.cpp
  poppler-global.cpp
  poppler-image.cpp
  poppler-page.cpp
  poppler-page-renderer.cpp
  poppler-page-transition.cpp
  poppler-private.cpp
  poppler-rectangle.cpp
  poppler-toc.cpp
  poppler-extern-draw.cpp
  poppler-version.cpp
  ExternDrawOutputDevice.cpp
)

add_library(poppler-cpp ${poppler_cpp_SRCS} ${poppler_cpp_HEADERS} ${poppler_cpp_Private_HEADERS})
set_target_properties(poppler-cpp PROPERTIES VERSION 0.7.0 SOVERSION 0)
if(MINGW AND BUILD_SHARED_LIBS)
    get_target_property(POPPLER_CPP_SOVERSION poppler-cpp SOVERSION)
    set_target_properties(poppler-cpp PROPERTIES SUFFIX "-${POPPLER_CPP_SOVERSION}${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
target_link_libraries(poppler-cpp poppler ${ICONV_LIBRARIES})

install(TARGETS poppler-cpp EXPORT poppler-cpp-config
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} include/poppler/cpp)

install(FILES
  ${poppler_cpp_HEADERS}
  DESTINATION include/poppler/cpp)

install(EXPORT poppler-cpp-config DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/poppler-cpp)
