vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        io-uring NBIO_ENABLE_IO_URING
        rdma NBIO_ENABLE_RDMA
        custom-memory-pooling NBIO_ENABLE_CUSTOM_MEMORY_POOLING
)

vcpkg_from_github(
    OUT_SOURCE_PATH NBIO_SOURCE_PATH
    REPO UserNameUnavailableIsUnavailable/NBIO
    REF 73bbcac6884e03640a875f9f1bdb4485f6c54951
    SHA512 8162ae5dbd9c7e71e9a84a2f3593a092c60ad28ab8114c10002dafdece612528669781c612e7881c0ee57214ef53c05665692126c7ffbb99582f268f89eadb03
    HEAD_REF main
)

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
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage.txt" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" COPYONLY)
vcpkg_install_copyright(FILE_LIST "${NBIO_SOURCE_PATH}/LICENSE")
