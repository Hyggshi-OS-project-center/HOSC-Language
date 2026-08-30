set(FRAMEWORK_SOURCES
  src/hosc_framework.c
  src/hosc_runtime.c
  src/hosc_modules.c
  ${CMAKE_SOURCE_DIR}/services/src/runtime_gui_backend.c
  ${CMAKE_SOURCE_DIR}/services/src/runtime_gui_backend_console.c
)

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
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(X11 QUIET x11)
    pkg_check_modules(XFT QUIET xft)
  endif()

  if(X11_FOUND)
    target_sources(hosc_framework PRIVATE ${CMAKE_SOURCE_DIR}/services/src/runtime_gui_backend_x11.c)
    target_include_directories(hosc_framework PRIVATE ${X11_INCLUDE_DIRS})
    target_link_libraries(hosc_framework PRIVATE ${X11_LIBRARIES})
  endif()

  if(XFT_FOUND)
    target_include_directories(hosc_framework PRIVATE ${XFT_INCLUDE_DIRS})
    target_link_libraries(hosc_framework PRIVATE ${XFT_LIBRARIES} fontconfig Xrender)
  endif()

  target_link_libraries(hosc_framework PRIVATE m)
endif()
