# Simple Proxy Block Device Driver
Implementation of Linux kernel 6.8.X simple proxy block device.

## Build
```bash
make
```

## Clean
```bash
make clean
```

## Install SBDD Module
```bash
sudo insmod sbdd.ko backing_dev=/dev/sdX
```

## Check module
```bash
lsmod | grep sbdd
```

## Remove module
```bash
sudo rmmod sbdd
```


## Tests with dd(warning!)
```bash
sudo dd if=/dev/zero of=/dev/sdX bs=4k count=1 oflag=direct
```

```bash
sudo dd if=/dev/sdX bs=4k count=1 status=none | hexdump -C
```

### output:
```bash
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
```

### next:

```bash
echo 0xdead | sudo dd of=/dev/sbdd bs=4k count=1 oflag=direct conv=notrunc,sync
```

```bash
sudo dd if=/dev/sbdd bs=4k count=1 status=none oflag=direct | hexdump -C
```

### output: 

```bash
00000000  30 78 64 65 61 64 0a 00  00 00 00 00 00 00 00 00  |0xdead..........|
```

# FIO tests

```bash
sudo fio --name=write_phase --rw=write --filename=/dev/sdbb --size=1G     --verify=pattern --verify_pattern=0xdeadbeef --do_verify=1
```

```bash
fio --name=read_phase --rw=read --filename=/dev/sdbb --size=1G --verify=pattern --verify_pattern=0xdeadbeef --do_verify=1
```

# RAID1 implementation:
advanced_functionality branch


## References
- [Linux Device Drivers](https://lwn.net/Kernel/LDD3/)
- [Linux Kernel Development](https://rlove.org)
- [Linux Kernel Teaching](https://linux-kernel-labs.github.io/refs/heads/master/labs/block_device_drivers.html)
- [Linux Kernel Sources](https://github.com/torvalds/linux)
