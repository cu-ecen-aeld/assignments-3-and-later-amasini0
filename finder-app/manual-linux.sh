#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

SRCDIR=$(dirname $(realpath $0))
OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # DONE: Add your kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j8 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} Image
    make -j8 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp $OUTDIR/linux-stable/arch/arm64/boot/Image $OUTDIR/

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# DONE: Create necessary base directories
mkdir rootfs
cd rootfs
mkdir bin dev etc home lib lib64 proc sbin sys usr var
mkdir usr/bin usr/lib usr/sbin
mkdir var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # DONE:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# DONE: Make and install busybox
make -j8 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} 
make -j8 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install

cd ${OUTDIR}/rootfs
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# DONE: Add library dependencies to rootfs
SYSROOT=$(realpath $(${CROSS_COMPILE}gcc -print-sysroot))
cp $SYSROOT/lib/ld-linux-aarch64.so.1 lib/
cp $SYSROOT/lib64/libm.so.6 lib64/
cp $SYSROOT/lib64/libresolv.so.2 lib64/
cp $SYSROOT/lib64/libc.so.6 lib64/

# DONE: Make device nodes
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# DONE: Clean and build the writer utility
cd $SRCDIR
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# DONE: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
mkdir -p $OUTDIR/rootfs/home/conf
cp writer finder.sh finder-test.sh autorun-qemu.sh $OUTDIR/rootfs/home/
cp conf/assignment.txt conf/username.txt $OUTDIR/rootfs/home/conf/

# DONE: Chown the root directory
# DONE: Create initramfs.cpio.gz
cd $OUTDIR/rootfs
find . | cpio -H newc -ov --owner root:root > $OUTDIR/initramfs.cpio
cd $OUTDIR
gzip -f initramfs.cpio

