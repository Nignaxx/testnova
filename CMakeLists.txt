cmake_minimum_required(VERSION 3.5)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Definis la variable VITASDK")
  endif()
endif()
project(NovaDeck C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "NovaDeck")
set(VITA_TITLEID  "NOVADECK1")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -O2 -std=gnu99")
add_executable(${PROJECT_NAME} src/main.c)

target_link_libraries(${PROJECT_NAME}
  vita2d freetype png jpeg z m c
  SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub
  ScePgf_stub ScePvf_stub SceCommonDialog_stub SceAppMgr_stub
  SceNet_stub SceNetCtl_stub SceHttp_stub SceLibKernel_stub
)

vita_create_self(eboot.bin ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
