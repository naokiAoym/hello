#!/bin/sh

virt-install \
	--name=ubuntu18.04-1 \
	--ram 6144 \
	--disk path=/var/lib/libvirt/images/ubuntu18.04-1.img,size=64 \
	--location 'http://jp.archive.ubuntu.com/ubuntu/dists/bionic/main/installer-amd64/' \
	--vcpus 4 \
	--os-type linux \
	--os-variant ubuntu18.04 \
	--graphics none \
	--network bridge=virbr0 \
	--console pty,target_type=serial \
	--extra-args 'console=ttyS0,115200n8 serial'

