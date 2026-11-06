cmake_minimum_required(VERSION 3.20)
project(StudioYuE2 C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# CRT statique : un seul .exe autonome, sans redistribuable Visual C++.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(IMGUI ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(YYJSON ${CMAKE_CURRENT_SOURCE_DIR}/../yue2.cpp/vendor/yyjson)
set(ORT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime)
# 1.0.45 : sources dans music/_developpement/native ; le Studio installé est à la racine de music.
set(APP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(StudioYuE2 WIN32
    src/main.cpp
    src/studio.cpp
    src/player.cpp
    src/suno.cpp
    src/media.cpp
    src/score.cpp
    src/soundfont.cpp
    src/atelier.cpp
    src/clip.cpp
    src/fiches.cpp
    src/setup.cpp
    src/download.cpp
    src/gpumem.cpp
    src/gpuinfo.cpp
    src/lyrics_web.cpp
    src/net.cpp
    src/update_check.cpp
    src/sysinfo.cpp
    src/translate.cpp
    src/ai_writer.cpp
    src/i18n.cpp
    src/analysis.cpp
    src/app.rc
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/imgui_demo.cpp
    ${IMGUI}/backends/imgui_impl_win32.cpp
    ${IMGUI}/backends/imgui_impl_dx11.cpp
    ${IMGUI}/misc/cpp/imgui_stdlib.cpp
    ${YYJSON}/yyjson.c
)
target_include_directories(StudioYuE2 PRIVATE ${IMGUI} ${IMGUI}/backends ${YYJSON} ${ORT}/include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/include)
target_compile_definitions(StudioYuE2 PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN IMGUI_DISABLE_DEMO_WINDOWS)
target_compile_options(StudioYuE2 PRIVATE /utf-8 /W3 $<$<CONFIG:Release>:/O2 /GL>)
target_link_options(StudioYuE2 PRIVATE $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
target_link_libraries(StudioYuE2 PRIVATE d3d11 dxgi d3dcompiler winmm dwmapi comdlg32 shell32 advapi32 gdiplus ole32 winhttp comctl32 pdh crypt32 ${ORT}/lib/onnxruntime.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/x64/WebView2LoaderStatic.lib version)  # 1.0.91 : Atelier partition
# 1.0.40 : exe « Studio Audio AI.exe » (la cible garde son nom interne StudioYuE2)
set_target_properties(StudioYuE2 PROPERTIES OUTPUT_NAME "Studio Audio AI")

# Copie à la racine du projet, à côté de models/ et yue2.cpp/.
add_custom_command(TARGET StudioYuE2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:StudioYuE2> ${APP_ROOT}/$<TARGET_FILE_NAME:StudioYuE2>)

# ONNX Runtime (analyse audio) : DLL à côté de l'exécutable.
add_custom_command(TARGET StudioYuE2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${ORT}/lib/onnxruntime.dll ${APP_ROOT}/onnxruntime.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${ORT}/lib/onnxruntime_providers_shared.dll ${APP_ROOT}/onnxruntime_providers_shared.dll)


# Créateur de styles (fenêtre dans le style du Studio, réservé à l'éditeur) : analyse des chansons d'inspiration
# (même code que le Studio : analysis.cpp + ONNX Runtime) et création de packs .yue2pack. Copié dans le dossier
# « createur » du projet avec les DLL d'ONNX Runtime, jamais dans la version distribuée.
add_executable(CreateurStyles WIN32
    createur/createur_styles.cpp
    src/analysis.cpp
    src/lyrics_web.cpp
    src/net.cpp
    src/studio.cpp
    src/fiches.cpp
    src/score.cpp
    src/gpuinfo.cpp
    src/suno.cpp
    src/i18n.cpp
    createur/createur.rc
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/imgui_demo.cpp
    ${IMGUI}/backends/imgui_impl_win32.cpp
    ${IMGUI}/backends/imgui_impl_dx11.cpp
    ${IMGUI}/misc/cpp/imgui_stdlib.cpp
    ${YYJSON}/yyjson.c
)
target_include_directories(CreateurStyles PRIVATE src ${IMGUI} ${IMGUI}/backends ${YYJSON} ${ORT}/include)
target_compile_definitions(CreateurStyles PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN IMGUI_DISABLE_DEMO_WINDOWS)
target_compile_options(CreateurStyles PRIVATE /utf-8 /W3)
target_link_libraries(CreateurStyles PRIVATE d3d11 dxgi d3dcompiler winmm dwmapi comdlg32 shell32 advapi32 gdiplus ole32 user32 winhttp comctl32 pdh ${ORT}/lib/onnxruntime.lib)
set_target_properties(CreateurStyles PROPERTIES OUTPUT_NAME "Createur de styles")
add_custom_command(TARGET CreateurStyles POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_ROOT}/Admin_tools/Createur_styles
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:CreateurStyles> ${APP_ROOT}/Admin_tools/Createur_styles/$<TARGET_FILE_NAME:CreateurStyles>
    COMMAND ${CMAKE_COMMAND} -E copy ${ORT}/lib/onnxruntime.dll ${APP_ROOT}/Admin_tools/Createur_styles/onnxruntime.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${ORT}/lib/onnxruntime_providers_shared.dll ${APP_ROOT}/Admin_tools/Createur_styles/onnxruntime_providers_shared.dll)
