# SocketCAN CanGuru Lite driver

## Install module

Add file can.conf to /usr/lib/modules-load.d
~~~
can
can-dev
~~~
or execute commands
~~~
modprobe can
modprobe can-dev
~~~

~~~
insmod guru_drv.ko
~~~

## Init network

~~~
ip link set can1 type can bitrate 500000 dbitrate 2000000 termination 120 fd on
ip link set up can1
~~~

~~~
ip link set down can1
~~~

~~~
ip -details -statistics link show can1
~~~
