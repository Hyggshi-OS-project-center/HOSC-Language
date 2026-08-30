set(FRAMEWORK_SOURCES
  src/hosc_framework.c
  src/hosc_runtime.c
  src/hosc_modules.c
  ${CMAKE_SOURCE_DIR}/services/src/runtime_gui_backend.c
  ${CMAKE_SOURCE_DIR}/services/src/runtime_gui_backend_console.c
)

if(NOT WIN32)
  list(APPEND FRAMEWORK_SOURCES ${CMAKE_SOURCE_DIR}/services/src/runtime_gui_backend_x11.c)
endif()

add_executable(hosc_framework ${FRAMEWORK_SOURCES})

target_include_directories(hosc_framework
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/services/include
)

if(WIN32)
  target_link_libraries(hosc_framework
    PRIVATE
      user32
      gdi32
      kernel32
      gdiplus
      ole32
      comdlg32
      mfplay
      mfplat
      mf
      mfuuid
  )
else()
  find_package(X11 QUIET)
  find_package(PkgConfig QUIET)
  set(HAVE_XFT FALSE)

  if(PKG_CONFIG_FOUND)
    pkg_check_modules(XFT QUIET xft)
    if(XFT_FOUND)
      set(HAVE_XFT TRUE)
    endif()
  endif()

  if(X11_FOUND)
    target_compile_definitions(hosc_framework PRIVATE HOSC_HAS_X11=1)
    target_include_directories(hosc_framework PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(hosc_framework PRIVATE ${X11_LIBRARIES})
    if(HAVE_XFT)
      target_compile_definitions(hosc_framework PRIVATE HOSC_HAS_XFT=1)
      target_include_directories(hosc_framework PRIVATE ${XFT_INCLUDE_DIRS})
      target_link_libraries(hosc_framework PRIVATE ${XFT_LIBRARIES} fontconfig Xrender)
    endif()
  elseif(PKG_CONFIG_FOUND)
    pkg_check_modules(PKG_X11 QUIET x11)
    if(PKG_X11_FOUND)
      target_compile_definitions(hosc_framework PRIVATE HOSC_HAS_X11=1)
      target_include_directories(hosc_framework PRIVATE ${PKG_X11_INCLUDE_DIRS})
      target_link_libraries(hosc_framework PRIVATE ${PKG_X11_LIBRARIES})
      if(HAVE_XFT)
        target_compile_definitions(hosc_framework PRIVATE HOSC_HAS_XFT=1)
        target_include_directories(hosc_framework PRIVATE ${XFT_INCLUDE_DIRS})
        target_link_libraries(hosc_framework PRIVATE ${XFT_LIBRARIES} fontconfig Xrender)
      endif()
    endif()
  endif()

  target_link_libraries(hosc_framework PRIVATE m)
endif()
