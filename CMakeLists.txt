cmake_minimum_required(VERSION 3.21)

project(NICResidency
        VERSION 1.0.1
        DESCRIPTION "Vendor-neutral runtime for NIC / SmartNIC / DPU resource residency and locality"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(NICRESIDENCY_BUILD_TESTS "Build NIC Residency tests" ON)
option(NICRESIDENCY_BUILD_CLI "Build the nic_residency_inspect CLI" ON)
option(NICRESIDENCY_BUILD_EXAMPLES "Build NIC Residency examples" ON)
option(NICRESIDENCY_BUILD_BENCHMARKS "Build NIC Residency benchmarks" ON)
option(NICRESIDENCY_BUILD_SHARED "Build a shared library instead of static" OFF)

set(NICRESIDENCY_PUBLIC_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(NICRESIDENCY_BUILD_SHARED)
  add_library(nicresidency SHARED)
else()
  add_library(nicresidency STATIC)
endif()
add_library(NICResidency::nicresidency ALIAS nicresidency)

target_sources(nicresidency PRIVATE
  src/registry.cpp
  src/policy.cpp
  src/synthetic_backend.cpp
  src/system_backend.cpp
  src/nvidia_backend.cpp
  src/engine.cpp
  src/persistence.cpp
  src/authority.cpp
)

target_include_directories(nicresidency PUBLIC
  $<BUILD_INTERFACE:${NICRESIDENCY_PUBLIC_INCLUDE}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(nicresidency PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 1
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME nicresidency
)

if(MSVC)
  target_compile_options(nicresidency PRIVATE /W4 /WX)
  target_compile_definitions(nicresidency PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(nicresidency PUBLIC setupapi iphlpapi)
else()
  target_compile_options(nicresidency PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(nicresidency_configure_targets tgt)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W4 /WX)
    target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endfunction()

if(NICRESIDENCY_BUILD_CLI)
  add_executable(nic_residency_inspect cli/nic_residency_inspect.cpp)
  target_link_libraries(nic_residency_inspect PRIVATE nicresidency)
  nicresidency_configure_targets(nic_residency_inspect)
endif()

if(NICRESIDENCY_BUILD_EXAMPLES)
  set(NICRESIDENCY_EXAMPLES
    example_basic_discovery
    example_numa_selection
    example_accelerator_affinity
    example_deterministic_tiebreak
    example_stale_generation
    example_attachment_invalidation
    example_worker_incarnation
    example_synthetic_smartnic_dpu
    example_real_system_backend
    example_unsupported_smartnic
  )
  foreach(ex ${NICRESIDENCY_EXAMPLES})
    add_executable(${ex} examples/${ex}.cpp)
    target_link_libraries(${ex} PRIVATE nicresidency)
    nicresidency_configure_targets(${ex})
  endforeach()
endif()

if(NICRESIDENCY_BUILD_BENCHMARKS)
  set(NICRESIDENCY_BENCH
    bench_registry
    bench_topology
    bench_selection
    bench_queue
    bench_persistence
  )
  foreach(b ${NICRESIDENCY_BENCH})
    add_executable(${b} benchmarks/${b}.cpp)
    target_link_libraries(${b} PRIVATE nicresidency)
    nicresidency_configure_targets(${b})
  endforeach()
endif()

if(NICRESIDENCY_BUILD_TESTS)
  enable_testing()
  add_executable(nicresidency_tests tests/test_core.cpp)
  target_link_libraries(nicresidency_tests PRIVATE nicresidency)
  nicresidency_configure_targets(nicresidency_tests)
  add_test(NAME nicresidency_tests COMMAND nicresidency_tests)

  add_executable(nic_authority_worker tools/nic_authority_worker.cpp)
  target_link_libraries(nic_authority_worker PRIVATE nicresidency)
  nicresidency_configure_targets(nic_authority_worker)

  add_executable(nic_residency_coordinator tools/nic_residency_coordinator.cpp)
  target_link_libraries(nic_residency_coordinator PRIVATE nicresidency)
  nicresidency_configure_targets(nic_residency_coordinator)

  add_executable(nicresidency_authority_tests tests/test_authority.cpp)
  target_link_libraries(nicresidency_authority_tests PRIVATE nicresidency)
  nicresidency_configure_targets(nicresidency_authority_tests)
  add_test(NAME nicresidency_authority_tests COMMAND nicresidency_authority_tests)

  add_executable(nicresidency_invariant_tests tests/test_invariants.cpp)
  target_link_libraries(nicresidency_invariant_tests PRIVATE nicresidency)
  nicresidency_configure_targets(nicresidency_invariant_tests)
  add_test(NAME nicresidency_invariant_tests COMMAND nicresidency_invariant_tests)
endif()

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
install(TARGETS nicresidency EXPORT NICResidencyTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${NICRESIDENCY_PUBLIC_INCLUDE}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT NICResidencyTargets
  FILE NICResidencyTargets.cmake
  NAMESPACE NICResidency::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NICResidency
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NICResidencyConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/NICResidencyConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NICResidency
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/NICResidencyConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/NICResidencyConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/NICResidencyConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NICResidency
)
