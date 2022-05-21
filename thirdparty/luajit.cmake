set(src "${luajit_SOURCE_DIR}/src")
if (UNIX)
  find_program(MAKE make REQUIRED)

  set(lib "${src}/libluajit.a")
  add_custom_target(luajit-build
    COMMAND
      ${MAKE} -j BUILDMODE=static CFLAGS=-fPIC

    WORKING_DIRECTORY "${luajit_SOURCE_DIR}"
    VERBATIM
  )

elseif (MINGW)
  find_program(MAKE mingw32-make REQUIRED)

  set(lib "${src}/libluajit.a")
  add_custom_target(luajit-build
    COMMAND ${MAKE} -j BUILDMODE=static CFLAGS=-fPIC

    WORKING_DIRECTORY "${luajit_SOURCE_DIR}/src"
    VERBATIM
  )

elseif (MSVC)
  set(lib "${src}/lua51.lib")
  add_custom_target(luajit-build
    COMMAND msvcbuild.bat static

    WORKING_DIRECTORY "${luajit_SOURCE_DIR}/src"
    VERBATIM
  )

else()
  message(ERROR "unknown environment")
endif()

add_library(luajit-imported STATIC IMPORTED)
set_target_properties(luajit-imported PROPERTIES
  IMPORTED_LOCATION "${lib}"
)
add_dependencies(luajit-imported luajit-build)

add_library(luajit INTERFACE)
target_link_libraries(luajit
  INTERFACE luajit-imported $<$<PLATFORM_ID:Linux>:m>
)
target_include_directories(luajit SYSTEM BEFORE
  INTERFACE "${src}"
)
