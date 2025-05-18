# Dual Backing Block Device Driver
Implementation of Linux kernel 6.8.X dual Backing Block Device Driver.

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
sudo insmod sbdd.ko backing_dev1=/dev/sdX backing_dev2=/dev/sdY
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
sudo dd if=/dev/zero of=/dev/sdY bs=4k count=1 oflag=direct
```

```bash
sudo dd if=/dev/sd(X or Y) bs=4k count=1 status=none | hexdump -C
```

### output:
```bash
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
```

### next:

```bash
echo "0xTEST" | sudo dd of=/dev/sbdd bs=4k count=1 oflag=direct conv=notrunc,sync
```

```bash
sudo rmmod sbdd
```
```bash
sudo insmod sbdd.ko backing_dev1=/dev/sdX backing_dev2=/dev/sdY
````

```bash
sudo dd if=/dev/sda(X or Y) bs=4k count=1 status=none oflag=direct | hexdump -C
```

### output: 

```bash
00000000  30 78 54 45 53 54 0a 00  00 00 00 00 00 00 00 00  |0xTEST..........|
```
# FIO tests

```bash
sudo fio --name=write_phase --rw=write --filename=/dev/sdbb --size=1G     --verify=pattern --verify_pattern=0xdeadbeef --do_verify=1
````

```bash
fio --name=read_phase --rw=read --filename=/dev/sdbb --size=1G --verify=pattern --verify_pattern=0xdeadbeef --do_verify=1
```

## check: 

```bash
sudo dd if=/dev/sda(X or Y) bs=4k count=1 status=none oflag=direct | hexdump -C
```

### output:

```bash
00000000  de ad be ef de ad be ef  de ad be ef de ad be ef  |................|
```

## References
- [Linux Device Drivers](https://lwn.net/Kernel/LDD3/)
- [Linux Kernel Development](https://rlove.org)
- [Linux Kernel Teaching](https://linux-kernel-labs.github.io/refs/heads/master/labs/block_device_drivers.html)
- [Linux Kernel Sources](https://github.com/torvalds/linux)
