#!/bin/bash

if [ "$1" == "--log" ]; then
  shift
  $0 $* |& tee /var/log/raspistill.sh.log
  exit $?
fi


echo "COMMAND	: $0 # raspistill background launcher"
echo "ARGS	: $*"
echo "EUID	: $EUID"
echo "UID	: $UID"

if [ $EUID -ne 0 ]; then echo "ERROR	: script must be run as root"; exit -1; fi

echo "STATUS	: verifying raspistill availability"
PID=`ps -ef | grep raspistill | grep -v grep | grep -v 'raspistill.sh' | grep -o -E "[0-9]+" | head -1`
if [ "$PID" == "" ]; then 
  echo "STATUS	: launching raspistill for background image capture triggered by SIGUSR1"
  LASTPID=$!
  echo "LASTPID	: $LASTPID"

  echo "COMMAND	: /usr/bin/raspistill $*&"
  /usr/bin/raspistill $*&
  PID=$!
  RC=$?
  if [ $RC -ne 0 ]; then echo "ERROR	: raspistill failed with error $RC"; exit $RC; fi
  if [ "$LASTPID" == "$PID" ]; then echo "ERROR	: raspistill PID is unavailable"; exit -1; fi
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
