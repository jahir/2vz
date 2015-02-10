# 2vz

## Synopsis

Various tools to read and send data to a volkszaehler.org middleware

## The tools

* vzspool is used as a spooling relay by the other 2vz tools. Right now it's just a perl script, but I'l re-implement it in C (like the others)
* d0vz reads D0 meters 
* ev2vzs uses Linux' input event subsystem to get S0 impules with a proper time resolution
* thz2vzs reads operational data from (some) Stiebel Eltron and Tecalor heat pumps (THZ/LWZ 304 and 404)

## Installation

* create an unprivileged user "vz"

	\# useradd -d /var/spool/vz -r vz

* create the log and spool directories

	\# mkdir -p /var/log/vz /var/spool/vz
	\# chown vz: /var/log/vz /var/spool/vz

* customize configuration

	\# mkdir /etc/vz
	\# cp vzspool/vzspool.conf /etc/vz
	\# vi /etc/vz/vzspool.conf

* build and install the tools you want

	\# cp vzspool/vzspool /usr/local/bin

	\# cd d0
	\# make

	\# (t.b.c.)

* setup startup. I provided service files for systemd (I use Fedora and that's just how you do it there), but SysV init scripts should not be too hard to create, though.

	\# cp vzspool/vzspool.service /etc/systemd/system/

* start the services you want

	\# systemctl start vzspool
	\# systemctl start d0vz
	\# ...

* check logfile 

	\# less /var/log/vz/vz.log

* (optional) set up logrote. The log may grow quite fast (esp. with S0 input), so daily rotation is reasonable

	\# cp vz.logrotate /etc/logrotate.d/vz

