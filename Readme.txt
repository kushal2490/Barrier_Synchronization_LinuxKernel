/////////////////////////////////////////////////////////////
Aakanksha Budhiraja, ASU ID:- 1211210335
Kushal Anil Parmar, ASU ID:- 1207686255
/////////////////////////////////////////////////////////////
Assignment 4 - A Barrier Mechanism in Linux Kernel
==================================================================================================================

It containes 2 files:
user_barrier_test.c
team27.patch
userSample.txt
kernelSample.txt
------------------------

user_barrier_test.c 	-> This is a test program to test to syscalls. The main thread creates 2 child process. Each
						child process has 2 barriers on which threads operate. We have a Barrier broken flag which signals the pending threads to stop calling barrier_wait() and returns with an error. At each round of synchronization we wait for all the threads to arrive back from the barrier_wait syscall or the barrier_reset syscall and then send them for next round of synchronization.

team27.patch			-> This contains diff of the changes needed to make in the kernel source code.

barrier_syscall.c 		-> This implements the syscall functions:
							barrier_init()
							barrier_wait()
							barrier_destroy()
							barrier_reset()

							barrier_init() - Initializes each barrier in a particular process address space with number
											of threads that wait on the barrier. It returns the generated barrier_id to the user process thread group.

							barrier_wait)  - Implements synchronization functionality for the threads of a particular thread 
											group. Each barrier has a timeout associated with it, which returns an timer expire error code if the timer expires and all the threads have not arrived at the barrier.

							barrier_destroy() - Removes the barrier with a particular barrier id from the global barrier list.

							barrier_reset() - Resets the per barrier flags and the count values, once a barrier is broken and 					synchronization fails.

userSample.txt			-> Sample output logs on user side for 1 round of synhronization
kernelSample.txt		-> Sample output logs on kernel side for 1 round of synhronization
==================================================================================================
NOTE:
1) Reboot

2) Export your SDK's toolchain path to your current directory to avoid unrecognized command.
export PATH="/opt/iot-devkit/1.7.2/sysroots/x86_64-pokysdk-linux/usr/bin/i586-poky-linux/:$PATH"

3) TIMEOUT values for the 2 barriers can be changed at line numbers 140 and 144 of user program.
=================================================================================================

Method:-

1) Change the following KDIR path to your source kernel path and the SROOT path to your sysroots path in the Makefile
KDIR:=/opt/iot-devkit/1.7.2/sysroots/i586-poky-linux/usr/src/kernel
SROOT=/opt/iot-devkit/1.7.2/sysroots/i586-poky-linux/

2) Apply the patch changes onto a fresh kernel source code after extracting it from linux3.19-r0.tar.gz
cd kernel/
git apply team27.patch

3) Follow the steps to compile the kernel to generate the bzImage (same as given in assignment doc)
ARCH=x86 LOCALVERSION= CROSS_COMPILE=i586-poky-linux- make -j4 
ARCH=x86 LOCALVERSION= INSTALL_MOD_PATH=../galileo-install CROSS_COMPILE=i586-pokylinux- make modules_install
cp arch/x86/boot/bzImage ../galileo-install/

4) Navigate to the ../galileo-install/ to find the bzImage and copy it to the sdcard
cd ../galileo-install/
//Copy bzImage to sdcard

5) Turn off Galileo and load the sdcard to boot the patched kernel

6) Navigate to project directory and build the executable app
make

7) copy 1 file from project dir to board dir using the below command:
	i) user_barrier_test

sudo scp <filename> root@<inet_address>:/home/root

8) Run the exe using the below command
dmesg -c
./user_barrier_test > user.txt

9) After this the program waits for input of average sleep time.

Enter a value from 100-200 range.

10) Check kernel logs using below command
dmesg > dmesg.txt
