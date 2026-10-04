vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        io-uring NBIO_ENABLE_IO_URING
        rdma NBIO_ENABLE_RDMA
        custom-memory-pooling NBIO_ENABLE_CUSTOM_MEMORY_POOLING
)

get_filename_component(NBIO_SOURCE_PATH "${CURRENT_PORT_DIR}/../.." ABSOLUTE)

vcpkg_cmake_configure(
    SOURCE_PATH "${NBIO_SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DNBIO_BUILD_EXAMPLES=OFF
        -DBUILD_TESTING=OFF
        -DBUILD_BENCHMARKS=OFF
)

vcpkg_cmake_install()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/nbio)
vcpkg_copy_pdbs()

file(INSTALL "${NBIO_SOURCE_PATH}/README.md" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
