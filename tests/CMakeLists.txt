# Repository-level checks. Module tests live with their modules.

if(CMAKE_CROSSCOMPILING)
  # Hosted tests cannot run on the target; see cross/CMakeLists.txt for what
  # is checked instead.
  add_subdirectory(cross)
  return()
endif()

add_subdirectory(headers)
add_subdirectory(interop)
add_subdirectory(integration)
