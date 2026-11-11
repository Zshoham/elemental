# Examples built as part of Elemental's own build. The consume_* directories
# are separate projects: they show Elemental from the outside and are built
# by the integration tests (tests/integration).
if(EL_CONFIG_IO)
  add_subdirectory(backend_ring)
endif()
if(EL_CONFIG_ALLOC)
  add_subdirectory(backend_size_classes)
endif()
