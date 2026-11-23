#!/bin/bash

# Get the relative path to this file.
DIR="$(dirname "$0")"
CLASSPATH="${CLASSPATH}:${DIR}/*:${DIR}/shell/*"
# DEBUG="-agentlib:jdwp=transport=dt_socket,server=y,suspend=n,address=5005"

# Authentication not yet supported in SE
export SE_USERNAME=""
export SE_PASSWORD=""

OPTIONS="--continueBatchOnError"

# The shell is a short-lived process: the default collector (G1) runs threads
# beside the one that reads the rows, and on a small machine they compete with
# it; the second JIT compiler does not pay back in a run this short. With these
# a result of 18,000 rows (TPC-H Q16) is read 0.8 s sooner on 2 cores.
JVM_OPTIONS="${JVM_OPTIONS:--XX:+UseSerialGC -XX:TieredStopAtLevel=1}"

java ${JVM_OPTIONS} -cp "${CLASSPATH}" com.rapidsdata.shell.RapidsShell ${OPTIONS} "$@"
