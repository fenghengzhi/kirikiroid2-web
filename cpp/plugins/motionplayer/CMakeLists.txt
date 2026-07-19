cmake_minimum_required(VERSION 3.19)
project(motionplayer)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    main.cpp
)

target_sources(${PROJECT_NAME} PRIVATE
    EmotePlayer.cpp
    EmoteEngine.cpp
    EmoteSpring.cpp
    EmoteVarController.cpp
    EmoteAngleController.cpp
    EmoteMeshResolver.cpp
    EmoteBlinkController.cpp
    EmoteEyebrowController.cpp
    EmoteMouthController.cpp
    EmoteSelectorController.cpp
    EmoteLoopController.cpp
    EmoteBlinkRng.cpp
    EmoteWindEmitter.cpp
    PlayerCore.cpp
    PlayerFrameProgress.cpp
    PlayerResource.cpp
    PlayerMotionLoad.cpp
    PlayerVariable.cpp
    PlayerTimeline.cpp
    PlayerLayerQuery.cpp
    PlayerDrawDispatch.cpp
    PlayerRender.cpp
    PlayerRenderInternal.cpp
    PlayerRenderItems.cpp
    PlayerRenderExecute.cpp
    PlayerRenderTargets.cpp
    PlayerUpdateLayers.cpp
    PlayerUpdateLayerEval.cpp
    PlayerUpdateGeometry.cpp
    PlayerUpdateChildMotion.cpp
    PlayerUpdateParticles.cpp
    PlayerUpdateAnchor.cpp
    D3DAdaptor.cpp
    ResourceManager.cpp
    SourceCache.cpp
    RuntimeSupport.cpp
    NodeTree.cpp
    MotionNodeBridge.cpp
    MotionTraceWeb.cpp
    PrivateMotionGLL.cpp
    SeparateLayerAdaptor.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    krkr2plugin
    krkr2_binary_accessor
)

# Offline-only eager decoder and legacy logical frame models. None of these
# owners or containers exists in Android Player ctor/dtor/load
# (0x6CED30/0x6CFADC/0x6B0F10), so keep them out of the production plugin and
# out of the default Web build graph.
add_library(motionplayer_offline STATIC EXCLUDE_FROM_ALL
    OfflineMotionSnapshot.cpp
    PlayerFrameStep.cpp
    PlayerFrameStepping.cpp
)
target_include_directories(motionplayer_offline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(motionplayer_offline PRIVATE
    motionplayer
    psbfile_decoded_compat
    krkr2core
)
