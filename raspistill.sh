#!/bin/bash

sudo rm -f /var/firefuse/raspistill.PID

if [ "$1" == "--launch" ]; then
  shift
  exec $0 $* > /var/log/raspistill.sh.log 2>&1
else
  echo "COMMAND	: $0 # raspistill background launcher"
  echo "ARGS	: $*"
  echo "EUID	: $EUID"
  echo "UID	: $UID"

  if [ $EUID -ne 0 ]; then echo "ERROR	: script must be run as root"; exit -1; fi

  echo "STATUS	: verifying raspistill availability"
  ps -eo pid,comm | grep -E "raspistill$"
  PID=`ps -eo pid,comm | grep -E "raspistill$" | grep -o -E "[0-9]+"`
  if [ "$PID" == "" ]; then 
    echo "STATUS	: launching raspistill for SIGUSR1 image capture"
    echo "COMMAND	: /usr/bin/raspistill $* > /var/log/raspistill.log 2>&1 &"
    /usr/bin/raspistill $* > /var/log/raspistill.log 2>&1 &
    RC=$?
    if [ $RC -ne 0 ]; then echo "ERROR	: raspistill failed with error $RC"; exit $RC; fi
    PID=`ps -eo pid,comm | grep -E "raspistill$" | grep -o -E "[0-9]+"`
    if [ "" == "$PID" ]; then echo "ERROR	: raspistill PID is unavailable"; exit -1; fi
  else
    echo "STATUS	: raspistill is already running"; 
  fi
  echo "PID	: $PID"

  mkdir -p /var/firefuse
  echo "COMMAND	: echo $PID > /var/firefuse/raspistill.PID"
  echo $PID > /var/firefuse/raspistill.PID
  RC=$?
  if [ $RC -ne 0 ]; then echo "ERROR	: $RC"; exit $RC; fi
  echo "SUCCESS	: $PID saved to /var/firefuse/raspistill.PID"
fi
