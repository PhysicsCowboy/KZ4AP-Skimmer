# kz4ap-bank-replay refuses a --set rekey_after_s or rekey_timeout_s that is not positive (a 0 once selected Plan B's
# removed re-key variants) once, before it reads the suite: exit status 1 and one line on stderr. SUITE is a folder
# that does not exist, so the check must come before the manifest is read; a positive value is not refused and then
# fails on the missing manifest (exit status 2). Nothing is written.
#   cmake -DREPLAY=<kz4ap-bank-replay> -DSUITE=<missing folder> -P replay_refusal.cmake
if(EXISTS "${SUITE}")
  message(FATAL_ERROR "SUITE must not exist: ${SUITE}")
endif()
foreach(setting rekey_after_s=0 rekey_timeout_s=0 rekey_after_s=-0.8 rekey_timeout_s=0.0)
  execute_process(COMMAND "${REPLAY}" --out "${SUITE}" --name refused --set ${setting}
                  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REGEX MATCHALL "\n" newlines "${err}")
  list(LENGTH newlines lines)
  if(NOT status EQUAL 1 OR NOT lines EQUAL 1 OR NOT err MATCHES "must be positive" OR NOT out STREQUAL "")
    message(FATAL_ERROR "--set ${setting}: exit status ${status}, ${lines} line(s) on stderr:\n${err}\nstdout:\n${out}")
  endif()
endforeach()
execute_process(COMMAND "${REPLAY}" --out "${SUITE}" --name accepted --set rekey_after_s=0.4
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 2 OR NOT err MATCHES "manifest.json")
  message(FATAL_ERROR "--set rekey_after_s=0.4: exit status ${status} (2 expected, the manifest missing):\n${err}")
endif()
if(EXISTS "${SUITE}")
  message(FATAL_ERROR "the replay created ${SUITE}")
endif()
