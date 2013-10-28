#! /bin/sh
### BEGIN INIT INFO
# Provides:          mountfirefuse
# Required-Start:    $local_fs
# Required-Stop:
# Should-Start:      
# Default-Start:     
# Default-Stop:
# Short-Description: Mounts firefuse
# Description:       Restores userspace file system
### END INIT INFO

case "$1" in
    start)
	sudo firefuse -o allow_other /dev/firefuse
        ;;
    restart|reload|force-reload)
        echo "Error: argument '$1' not supported" >&2
        exit 3
        ;;
    stop)
        ;;
    *)
        echo "Usage: $0 start" >&2
        exit 3
        ;;
esac

: exit 0
