# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "E:/MY_LEARNING/ESP_IDF/components/bootloader/subproject"
  "E:/MY_LEARNING/esp_prj/mqtt_tcp/tcp/build/bootloader"
  "E:/MY_LEARNING/esp_prj/mqtt_tcp/tcp/build/bootloader-prefix"
  "E:/MY_LEARNING/esp_prj/mqtt_tcp/tcp/build/bootloader-prefix/tmp"
  "E:/MY_LEARNING/esp_prj/mqtt_tcp/tcp/build/bootloader-prefix/src/bootloader-stamp"
  "E:/MY_LEARNING/esp_prj/mqtt_tcp/tcp/build/bootloader-prefix/src"
  "E:/MY_LEARNING/esp_prj/mqtt_tcp/tcp/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/MY_LEARNING/esp_prj/mqtt_tcp/tcp/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/MY_LEARNING/esp_prj/mqtt_tcp/tcp/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
