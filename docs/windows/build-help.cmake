file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(COPY
  "${SOURCE_DIR}/CppDatasetUtil.hhp"
  "${SOURCE_DIR}/index.html"
  "${SOURCE_DIR}/en.html"
  "${SOURCE_DIR}/ja.html"
  "${SOURCE_DIR}/de.html"
  "${SOURCE_DIR}/fr.html"
  "${SOURCE_DIR}/it.html"
  "${SOURCE_DIR}/ko.html"
  "${SOURCE_DIR}/zh_CN.html"
  DESTINATION "${OUTPUT_DIR}")
file(REMOVE "${OUTPUT_DIR}/CppDatasetUtil.chm")
execute_process(
  COMMAND "${HHC}" CppDatasetUtil.hhp
  WORKING_DIRECTORY "${OUTPUT_DIR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)
# HTML Help Workshop returns 1 on success, so check the output as well.
if(NOT result STREQUAL "1" OR NOT EXISTS "${OUTPUT_DIR}/CppDatasetUtil.chm")
  message(FATAL_ERROR "HTML Help compilation failed (${result}):\n${output}\n${errors}")
endif()
