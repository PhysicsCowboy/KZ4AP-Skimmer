# kz4ap-bench refuses the new decoder's options that it cannot run before it reads the recording: exit status 2 and
# one line on stderr that names what is wrong, nothing on stdout. RECORDING is a file that does not exist, so each
# refusal must come before the recording is opened, and an option that is accepted goes on to fail on the missing
# recording (exit status 2 and "cannot open"). Nothing is written.
#   cmake -DBENCH=<kz4ap-bench> -DRECORDING=<missing file> -P bench_refusal.cmake
if(EXISTS "${RECORDING}")
  message(FATAL_ERROR "RECORDING must not exist: ${RECORDING}")
endif()

# expect_stderr(<pattern> <bench arguments after the recording>...): exit status 2, one line on stderr matching pattern
function(expect_stderr pattern)
  execute_process(COMMAND "${BENCH}" "${RECORDING}" ${ARGN}
                  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REGEX MATCHALL "\n" newlines "${err}")
  list(LENGTH newlines lines)
  if(NOT status EQUAL 2 OR NOT lines EQUAL 1 OR NOT err MATCHES "${pattern}" OR NOT out STREQUAL "")
    message(FATAL_ERROR "kz4ap-bench ${ARGN}: exit status ${status} (2 expected), ${lines} line(s) on stderr "
                        "(1 expected, matching '${pattern}'):\n${err}\nstdout:\n${out}")
  endif()
endfunction()

# --genie takes its values from the labels of the oracle channels
expect_stderr("--genie needs --oracle" --decoder viterbi --genie fist)
expect_stderr("--genie needs --oracle" --genie frequency --decoder viterbi --genie fading)
expect_stderr("fist, frequency or fading" --decoder viterbi --oracle --labels no-such-labels.json --genie loud)
# --set and --genie belong to the new decoder
expect_stderr("--set belongs to --decoder viterbi" --set beam_nats=30)
expect_stderr("--genie belongs to --decoder viterbi" --oracle --labels no-such-labels.json --genie fist)
# --set names a field of ViterbiConfig, and the value must fit it
expect_stderr("unknown parameter 'no_such_field'" --decoder viterbi --set no_such_field=1)
expect_stderr("unknown parameter 'beam_nat'" --decoder viterbi --set beam_nats=30 --set beam_nat=30)
expect_stderr("beam_nats: a finite number is needed" --decoder viterbi --set beam_nats=thirty)
expect_stderr("fist_sigmas: a list of exactly 5" --decoder viterbi --set "fist_sigmas=[0.1,0.2]")
expect_stderr("NAME=VALUE" --decoder viterbi --set beam_nats)

# what is accepted goes on to the recording
expect_stderr("cannot open" --decoder viterbi --set beam_nats=30 --set ceiling=600
              --set "fist_sigmas=[0.1,0.1,0.1,0.1,0.1]")
expect_stderr("cannot open" --decoder viterbi --oracle --labels no-such-labels.json --genie fist --genie frequency
              --genie fading --set stats=true)
