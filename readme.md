# Simple Proxy Block Device Driver
Implementation of Linux kernel 6.8.X simple proxy block device.

## Build
`make`

## Clean
`make clean`

## Install SBDD Module
`sudo insmod sbdd.ko backing_dev=/dev/sdX`

## Check module
`lsmod | grep sbdd`

## Remove module
`sudo rmmod sbdd`


## Tests with dd(warning!)
`sudo dd if=/dev/zero of=/dev/sdX bs=4k count=1 oflag=direct`

`sudo dd if=/dev/sdX bs=4k count=1 status=none | hexdump -C`

### output:
`00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|`

### next:

`echo 0xdead | sudo dd of=/dev/sbdd bs=4k count=1 oflag=direct conv=notrunc,sync`

`sudo dd if=/dev/sbdd bs=4k count=1 status=none oflag=direct | hexdump -C`

### output: 

`00000000  30 78 64 65 61 64 0a 00  00 00 00 00 00 00 00 00  |0xdead..........|`

# FIO tests

`sudo fio --name=write_phase --rw=write --filename=/dev/sdbb --size=1G     --verify=pattern --verify_pattern=0xdeadbeef --do_verify=1`

`fio --name=read_phase --rw=read --filename=/dev/sdbb --size=1G --verify=pattern --verify_pattern=0xdeadbeef --do_verify=1`



# TODO: RAID1 implementation


## References
- [Linux Device Drivers](https://lwn.net/Kernel/LDD3/)
- [Linux Kernel Development](https://rlove.org)
- [Linux Kernel Teaching](https://linux-kernel-labs.github.io/refs/heads/master/labs/block_device_drivers.html)
- [Linux Kernel Sources](https://github.com/torvalds/linux)
