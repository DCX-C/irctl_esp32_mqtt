# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "E:/MyEmbeding/ESP_IDF/components/bootloader/subproject"
  "E:/MyEmbeding/esp_prj/mqtt_irctl/build/bootloader"
  "E:/MyEmbeding/esp_prj/mqtt_irctl/build/bootloader-prefix"
  "E:/MyEmbeding/esp_prj/mqtt_irctl/build/bootloader-prefix/tmp"
  "E:/MyEmbeding/esp_prj/mqtt_irctl/build/bootloader-prefix/src/bootloader-stamp"
  "E:/MyEmbeding/esp_prj/mqtt_irctl/build/bootloader-prefix/src"
  "E:/MyEmbeding/esp_prj/mqtt_irctl/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/MyEmbeding/esp_prj/mqtt_irctl/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/MyEmbeding/esp_prj/mqtt_irctl/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
