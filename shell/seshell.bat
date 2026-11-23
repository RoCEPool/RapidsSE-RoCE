@echo off
java -XX:+UseSerialGC -XX:TieredStopAtLevel=1 -cp .;* com.rapidsdata.shell.RapidsShell %*