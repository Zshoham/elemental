# Install rules and the CMake package.
#
# An installed Elemental is one configuration, frozen. Besides the libraries
# and headers, the package carries that configuration in three forms so a
# consumer never has to run Kconfig:
#
#   include/elemental_config.h                 for C sources that want it
#   lib/cmake/Elemental/ElementalConfiguration.cmake   EL_CONFIG_* for CMake
#   lib/cmake/Elemental/elemental.config       the .config it was built from

include(CMakePackageConfigHelpers)

set(_elemental_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/Elemental")
get_property(ELEMENTAL_MODULES GLOBAL PROPERTY ELEMENTAL_MODULES)
get_property(ELEMENTAL_CONTRACT_SUITES GLOBAL PROPERTY ELEMENTAL_CONTRACT_SUITES)

install(EXPORT ElementalTargets
  NAMESPACE elemental::
  DESTINATION "${_elemental_package_dir}")

install(FILES "${ELEMENTAL_CONFIG_HEADER}"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${ELEMENTAL_CONFIG_CMAKE_FILE}"
  DESTINATION "${_elemental_package_dir}"
  RENAME ElementalConfiguration.cmake)
install(FILES "${ELEMENTAL_CONFIG_FILE}"
  DESTINATION "${_elemental_package_dir}"
  RENAME elemental.config)

configure_package_config_file(
  "${CMAKE_CURRENT_LIST_DIR}/ElementalConfig.cmake.in"
  "${PROJECT_BINARY_DIR}/ElementalConfig.cmake"
  INSTALL_DESTINATION "${_elemental_package_dir}")

# Before 1.0 a minor version may break the API, so only the same minor
# version is offered as compatible. The version file also rejects a package
# built for a different pointer size.
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/ElementalConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMinorVersion)

install(FILES
  "${PROJECT_BINARY_DIR}/ElementalConfig.cmake"
  "${PROJECT_BINARY_DIR}/ElementalConfigVersion.cmake"
  DESTINATION "${_elemental_package_dir}")
