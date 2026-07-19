cmake_minimum_required(VERSION 3.19)
project(psbfile)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    main.cpp
    PSBRawFile.cpp
    PSBMedia.cpp
    PSBMediaRegistry.cpp
)

# Compatibility-only eager decoder retained for offline tools/tests.
# It is not part of Android PSBFile.dll's raw holder/dispatch/media module.
add_library(psbfile_decoded_compat STATIC EXCLUDE_FROM_ALL)

target_sources(psbfile_decoded_compat PRIVATE
    PSBValue.cpp
    PSBFile.cpp
    types/PimgType.cpp
    types/MotionType.cpp
    types/ScnType.cpp
    types/ImageType.cpp
    types/MmoType.cpp
    types/ArchiveType.cpp
    types/SoundArchiveType.cpp
    resources/ImageMetadata.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE krkr2core)
target_link_libraries(psbfile_decoded_compat PRIVATE krkr2core)
