cmake_minimum_required(VERSION 3.19)
project(motionplayer)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PUBLIC
    main.cpp
    EmotePlayer.cpp
    PlayerCore.cpp
    PlayerFrameProgress.cpp
    PlayerResource.cpp
    PlayerRender.cpp
    PlayerRenderPrepare.cpp
    PlayerUpdateLayers.cpp
    PlayerQuery.cpp
    ResourceManager.cpp
    SourceCache.cpp
    RuntimeSupport.cpp
    NodeTree.cpp
    MotionNodeBridge.cpp
    MotionTraceWeb.cpp
    SeparateLayerAdaptor.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE krkr2plugin)
