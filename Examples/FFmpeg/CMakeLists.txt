cmake_minimum_required(VERSION 3.12)

if(NOT ANDROID)
    add_subdirectory(FFmpegEncode)
    add_dependencies(FFmpegEncode Utilities GLBase SceneGraph AudioVideo)

    add_subdirectory(FFmpegReencode)
    add_dependencies(FFmpegReencode Utilities GLBase SceneGraph AudioVideo)

    if (NOT BUILD_UNVUE_OSX_BUNDLE)
        add_subdirectory(FFmpegCameraInput)
        add_dependencies(FFmpegCameraInput Utilities GLBase SceneGraph AudioVideo)

        add_subdirectory(FFmpegMp3Player)
        add_dependencies(FFmpegMp3Player Utilities GLBase SceneGraph AudioVideo)

        add_subdirectory(FFmpegTest)
        add_dependencies(FFmpegTest Utilities GLBase SceneGraph AudioVideo)

        add_subdirectory(FFmpegAndroidDebug)

        add_subdirectory(FFmpegTest_UIApp)
        add_dependencies(FFmpegTest_UIApp Utilities GLBase SceneGraph AudioVideo)
    endif()
endif()