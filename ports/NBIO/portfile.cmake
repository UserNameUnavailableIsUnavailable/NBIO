vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        io-uring NBIO_ENABLE_IO_URING
        rdma NBIO_ENABLE_RDMA
        custom-memory-pooling NBIO_ENABLE_CUSTOM_MEMORY_POOLING
)

set(NBIO_SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../..")

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
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/NBIO)
vcpkg_copy_pdbs()

file(INSTALL "${NBIO_SOURCE_PATH}/README.md" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage.txt" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" COPYONLY)
vcpkg_install_copyright(FILE_LIST "${NBIO_SOURCE_PATH}/LICENSE")
