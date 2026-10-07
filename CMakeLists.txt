cmake_minimum_required(VERSION 3.16)
project(WebCamTVSmartVision VERSION 1.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET
  gtk+-3.0>=3.22
  gstreamer-1.0>=1.16
  gstreamer-video-1.0
  gstreamer-audio-1.0
  gstreamer-pbutils-1.0)

set(APP_ID org.smartvision.WebCamTV)
set(APP_BIN webcam-tv-smart-vision)

add_executable(${APP_BIN}
  src/main.cpp
  src/app_window.cpp
  src/pipeline.cpp
  src/device_manager.cpp
  src/v4l2_device.cpp
  src/dvb.cpp
  src/settings.cpp)

target_compile_options(${APP_BIN} PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
target_compile_definitions(${APP_BIN} PRIVATE
  APP_ID="${APP_ID}"
  APP_VERSION="${PROJECT_VERSION}"
  APP_DATADIR="${CMAKE_INSTALL_PREFIX}/share/${APP_BIN}")
target_link_libraries(${APP_BIN} PRIVATE PkgConfig::DEPS)

include(GNUInstallDirs)
install(TARGETS ${APP_BIN} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/${APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/${APP_ID}.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/${APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(PROGRAMS data/c920-defaults.sh DESTINATION ${CMAKE_INSTALL_DATADIR}/${APP_BIN})
configure_file(data/99-logitech-c920-smartvision.rules.in ${CMAKE_BINARY_DIR}/99-logitech-c920-smartvision.rules @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/99-logitech-c920-smartvision.rules DESTINATION ${CMAKE_INSTALL_DATADIR}/${APP_BIN})
set(CMAKE_INSTALL_DOCDIR ${CMAKE_INSTALL_DATADIR}/doc/${APP_BIN})
install(FILES README.md data/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(EXISTS ${CMAKE_SOURCE_DIR}/docs/Guida-WebCam-TV-Smart-Vision.pdf)
  install(FILES docs/Guida-WebCam-TV-Smart-Vision.pdf DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
# changelog e manuale compressi (richiesti dalla policy Debian)
find_program(GZIP gzip REQUIRED)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/changelog.gz
  COMMAND ${GZIP} -9 -n -c ${CMAKE_SOURCE_DIR}/data/changelog > ${CMAKE_BINARY_DIR}/changelog.gz
  DEPENDS data/changelog)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${APP_BIN}.1.gz
  COMMAND ${GZIP} -9 -n -c ${CMAKE_SOURCE_DIR}/data/${APP_BIN}.1 > ${CMAKE_BINARY_DIR}/${APP_BIN}.1.gz
  DEPENDS data/${APP_BIN}.1)
add_custom_target(docs ALL DEPENDS ${CMAKE_BINARY_DIR}/changelog.gz ${CMAKE_BINARY_DIR}/${APP_BIN}.1.gz)
install(FILES ${CMAKE_BINARY_DIR}/changelog.gz DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES ${CMAKE_BINARY_DIR}/${APP_BIN}.1.gz DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

# ---------------------------------------------------------------- pacchetto .deb
#   cmake -S . -B build-deb -DCMAKE_INSTALL_PREFIX=/usr && cmake --build build-deb && (cd build-deb && cpack -G DEB)
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME ${APP_BIN})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Fabio Avallone <avallone.fabio@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Webcam, TV e satellite: anteprima, registrazione MP4/OGG e ponte del segnale")
set(CPACK_PACKAGE_DESCRIPTION "Applicazione GNOME (GTK3 + GStreamer) per webcam UVC come Logitech
 C920/Brio, schede TV digitali terrestri e satellitari (DVB) e flussi di
 rete. Registra audio e video in MP4 (H.264/AAC) e OGG (Theora/Vorbis),
 scatta istantanee, espone i controlli V4L2 della webcam e inoltra il
 segnale verso dispositivi di uscita (v4l2loopback, uscite audio, secondo
 schermo).")
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(CPACK_STRIP_FILES TRUE)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
set(CPACK_DEBIAN_PACKAGE_SECTION video)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/fabionet/webcam-tv-smart-vision")
# Nome file con la distribuzione di compilazione: il .deb dipende dalle librerie dell'host
# (glibc, GTK, GStreamer), quindi se ne produce uno per ogni distro Debian/Ubuntu supportata.
execute_process(COMMAND sh -c ". /etc/os-release && echo $ID$VERSION_ID" OUTPUT_VARIABLE DISTRO_TAG OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT DISTRO_TAG)
  set(DISTRO_TAG linux)
endif()
set(CPACK_DEBIAN_FILE_NAME "${APP_BIN}_${PROJECT_VERSION}_${DISTRO_TAG}_amd64.deb")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)          # dipendenze dalle librerie rilevate con dpkg-shlibdeps
set(CPACK_DEBIAN_PACKAGE_DEPENDS
  "gstreamer1.0-plugins-base, gstreamer1.0-x, gstreamer1.0-plugins-good, gstreamer1.0-gtk3, gstreamer1.0-plugins-bad, gstreamer1.0-plugins-ugly, gstreamer1.0-libav")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gstreamer1.0-pipewire, dvb-tools, v4l-utils, pkexec")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "v4l2loopback-dkms, gstreamer1.0-vaapi")
include(CPack)
