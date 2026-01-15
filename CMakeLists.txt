# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(toilet-timer)

add_custom_command(TARGET app POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/toilet-timer.bin ${CMAKE_SOURCE_DIR}/local_ota_server/toilet-timer.bin
                   COMMENT "Copying toilet-timer.bin to local OTA server after build...")

# Uncomment the following line to enable auto-uploading of toilet-timer.bin to Firebase Storage
# add_custom_command(TARGET app POST_BUILD
#                    COMMAND gcloud storage cp ${CMAKE_BINARY_DIR}/toilet-timer.bin gs://dongle-updater.appspot.com/protected_files/toilet-timer.bin
#                    COMMENT "Uploading toilet-timer.bin to Firebase Storage after build...")