#! /bin/bash

chmod a+rw /dev/exanic0

ifconfig eth4 promisc 
ifconfig eth5 promisc 
ifconfig eth6 promisc 
ifconfig eth7 promisc 

exanic-config exanic0:0 up
exanic-config exanic0:1 up
exanic-config exanic0:2 up
exanic-config exanic0:3 up

exanic-config exanic0:0 bypass-only on
exanic-config exanic0:1 bypass-only on
exanic-config exanic0:2 bypass-only on
exanic-config exanic0:3 bypass-only on

exanic-clock-sync exanic0:host

