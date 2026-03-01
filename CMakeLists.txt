cmake_minimum_required(VERSION 3.13)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "Please define VITASDK to point to your VitaSDK toolchain.")
endif()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain")
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
project(vita_wifi_scope C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(VITA_APP_NAME "Vita Wi-Fi Scope")
set(VITA_TITLEID  "VWSC00001")
set(VITA_VERSION  "01.48")

add_executable(${PROJECT_NAME}
  src/main.c
  src/latency_probe.c
  src/lan_scanner.c
  src/discovery.c
  src/alerts.c
  src/export_json.c
  src/export_viewer.c
  src/ui_audio.c
  src/host_tools.c
  src/scan_history.c
  src/alert_rules.c
  src/bt_store.c
  src/ui_fx.c
  src/proxy_client.c
  src/net_monitor.c
  src/bt_monitor.c
  src/render.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -O2
)

target_link_libraries(${PROJECT_NAME}
  vita2d
  SceAppMgr_stub
  SceCommonDialog_stub
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  SceKernelThreadMgr_stub
  ScePgf_stub
  SceSysmodule_stub
  SceNet_stub
  SceNetCtl_stub
  SceIofilemgr_stub
  SceAudio_stub
  SceBt_stub
  m
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
vita_create_vpk(
  ${PROJECT_NAME}.vpk
  ${VITA_TITLEID}
  ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
