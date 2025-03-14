.HTML "The Various Ports
.TL
The Various Ports
.PP
This document collects comments about the various
architectures supported by Plan 9.
The system tries to hide most of the differences between machines,
so the machines as seen by a Plan 9
user look different from how they are perceived through commercial software.
Also, because we are a small group, we couldn't do everything:
exploit every optimization, support every model,
drive every device.
This document records what we
.I have
done.
The first section discusses the compiler/assembler/loader suite for each machine.
The second talks about
the operating system implemented on each of the various
machines.
.
.SH
The MIPS compiler
.PP
This compiler generates code for the R2000, R3000, and R4000 machines configured
to be big-endians.  The compiler generates no R4000-specific instructions
although the assembler and loader support the new user-mode instructions.
There are options to generate code for little-endian machines.
Considering its speed, the Plan 9 compiler generates good code,
but the commercial
MIPS compiler with all the stops pulled out consistently beats it
by 20% or so, sometimes more.  Since ours compiles about 10 times
faster and we spend most of our time compiling anyway,
we are content with the tradeoff.
.PP
The compiler is solid: we've used it for several big projects and, of course,
all our applications run under it.
The behavior of floating-point programs is much like on the 68040:
the operating system emulates where necessary to get past non-trapping
underflow and overflow, but does not handle gradual underflow or
denormalized numbers or not-a-numbers.
.SH
The SPARC compiler
.PP
The SPARC compiler is also solid and fast, although we haven't
used it for a few years, due to a lack of current hardware.  We have seen it do
much better than GCC with all the optimizations, but on average
it is probably about the same.
.PP
We used to run some old SPARC machines with no multiply or divide instructions,
so the compiler
does not produce them by default.
Instead it calls internal subroutines.
A loader flag,
.CW -M ,
causes the instructions to be emitted.  The operating system has
trap code to emulate them if necessary, but the traps are slower than
emulating them in user mode.
In any modern lab, in which SPARCS have the instructions, it would be worth enabling the
.CW -M
flag by default.
.PP
The floating point story is the same as on the MIPS.
.SH
The Intel i386 compiler
.PP
This is really an
.I x 86
compiler, for
.I x >2.
It works only
if the machine is in 32-bit protected mode.
It is solid and generates tolerable code; it is our main compiler these days.
.PP
Floating point is well-behaved, but the compiler assumes i387-compatible
hardware to execute
the instructions.  With 387 hardware,
the system does the full IEEE 754 job, just like
the MC68881.  By default, the libraries don't use the 387 built-ins for
transcendentals.
If you want them,
build the code in
.CW /sys/src/libc/386/387 .
.
.SH
The AMD64 compiler
.PP
The AMD64 compiler has been used to build 64-bit variants of Plan 9.
It seems to be reasonably solid.
.
.SH
The PowerPC compiler
.PP
The PowerPC compiler supports the 32-bit PowerPC architecture only;
it does not support either the 64-bit extensions or the POWER compatibility instructions.
It has been used for production operating system work on the 603, 603e, 604e, 821, 823, and 860,
and experimental work on the 405, 440 and 450.
On the 8xx floating-point instructions must be emulated.
Instruction scheduling is not implemented; otherwise the code generated
is similar to that for the other load-store architectures.
The compiler makes little or no use of unusual PowerPC features such as the
counter register, several condition code registers, and multiply-accumulate
instructions, but they are sometimes
used by assembly language routines in the libraries.
.
.SH
The PowerPC64 compiler
.PP
The PowerPC64 compiler supports the 64-bit PowerPC architecture only.
It has been lightly used on IBM's Blue Gene machines.
.
.SH
The ARM compiler
.PP
The ARM compiler is fairly solid; it has been used for some production
operating system work including Inferno and the Plan 9 kernel
for the iPAQ, which uses a StrongArm SA1, and the Sheevaplug,
Guruplug, Dreamplug, Gumstix Overo, Compulab Trimslice and others.
The compiler supports the ARMv4 and later 32-bit architectures;
it does not support the Thumb instruction sets.
It has been used on ARM7500FE, ARM926 and Cortex-A8 and -A9 processors
and the Strongarm SA1 core machines.
The compiler generates instructions for
ARM 7500 FPA floating-point coprocessor 1 by default,
but
.CW 5l
.CW -f
instead generates VFP instructions for coprocessors 10 and 11.
.
.SH
The IBM PC operating system
.PP
The PC version of Plan 9 can boot via PXE
or directly from a disk created by the
.CW format
command; see
.I prep (8).
Plan 9 runs in 32-bit mode\(emwhich requires a 386 or later model x86 processor\(emand
has an interrupt-driven I/O system, so it does not
use the BIOS (except for a small portion of the boot program and floppy boot block).
This helps performance but limits the set of I/O devices that it can support without
special code.
.PP
Plan 9 supports the ISA, EISA, and PCI buses as well as PCMCIA and PC card devices.
It is infeasible to list all the supported machines, because
the PC-clone marketplace is too volatile and there is
no guarantee that the machine you buy today will contain the
same components as the one you bought yesterday.
(For our lab, we buy components and assemble the machines
ourselves in an attempt to lessen this effect.)
IDE/ATA, SATA and SCSI disks are supported.
CD-ROMs are supported two ways, either on the SCSI bus, or as ATA(PI) devices.
The SCSI adapter must be a member of the Mylex Multimaster (old Buslogic BT-*) series
or the Symbios 53C8XX series.
.PP
Supported Ethernet cards include the
AMD79C790,
3COM Etherlink III and 3C589 series,
Lucent Wavelan and compatibles,
NE2000,
WD8003,
WD8013,
Realtek 8139,
SMC Elite and Elite Ultra,
Linksys Combo EthernetCard and EtherFast 10/100,
and a variety of controllers based on the
Intel i8255[789] and Digital (now Intel) 21114x chips.
We support Gigabit Ethernet via
Realtek 8110S/8169S,
and
Intel 8254[013467], 8256[36], and 8257[1-79] controllers.
We support 10-Gigabit Ethernet via
Intel's 8259[89],
and
Myricom's 10g-pcie-8a.
We mostly use Intel and Realtek gigabit controllers,
so those drivers may be more robust.
.PP
There must be an explicit Plan 9 driver for peripherals;
it cannot use DOS or Windows drivers.
Plan 9 cannot exploit special hardware-related features that fall outside of the
IBM PC model,
such as power management,
unless architecture-dependent code is added to the kernel.
For more details see
.I plan9.ini (8).
.PP
Over the years,
Plan 9 has run on a number of VGA cards.
Recent changes to the graphics system have not been
tested on most of the older cards; some effort may be needed to get them working again.
In our lab, most of our machines use the ATI or Nvidia chips,
so such devices are probably
the most reliable.
The system requires a hardware cursor.
For more details see
.I vgadb (6)
and
.I vga (8).
The wiki
.CW http://plan9.bell-labs.com/wiki/plan9 ) (
contains the definitive list of cards that are known to work; see the ``supported PC hardware''
page.
.PP
For audio, Plan 9 supports the Sound Blaster 16 and compatibles.
(Note that audio doesn't work under Plan 9 with 8-bit Sound Blasters.)
There is also user-level support for USB audio devices; see
.I usb (4).
.PP
Finally, it's important to have a three-button mouse with Plan 9.
The system currently works only with mice on the PS/2 port or USB.
Serial mouse support should return before long.
.PP
Once you have Plan 9 installed (see the wiki's installation document),
use PXE or a boot disk to load the system.  See
.I booting (8),
.I 9boot (8),
and
.I prep (8)
for more information.
.
.SH
The Routerboard 450G operating system
.PP
This is a CPU kernel that runs on the Mikrotik Routerboard RB450G,
which contains a MIPS 24K CPU
(the Atheros 7161), which implements the MIPS32R2 architecture.
It has 256MB of RAM and a serial port.
The CPU lacks the 64-bit instructions of previous MIPS systems (e.g.,
SGI Challenge and Carrera).
There is no hardware floating-point, so we emulate the instructions.
Only the first of the five Gigabit Ethernet ports is currently supported;
the other four are connected via an internal switch.
To avoid a bug in the CPU (erratum 48), we run the caches write-through,
rather than write-back, and compiled
.CW /mips
with a
.I vl
modified to emit enough NOPs to avoid three consecutive store instructions
(see
.CW /sys/src/cmd/vl/noop.c
to enable this).
.
.SH
The PowerPC operating system
.PP
We have a version of the system that runs on the PowerPC
on a home-grown machine called Viaduct.
The Viaduct minibrick is a small (12x9x3 cm) low-cost embedded
computer consisting of a 50Mhz MPC850, 16MB sdram, 2MB flash,
and two 10Mb Ethernet ports.  It is designed for home/SOHO
networking applications such as VPN, firewalls, NAT, etc.
.PP
The kernel has also been ported to the Motorola MTX embedded motherboard;
that port is included in the distribution.
The port only works with a 604e processor (the 603e is substantially different)
and at present only a single CPU is permitted.
.PP
We have ports to the Xilinx Virtex 4 and 5 FPGAs
which use PowerPC 405 and 440 processors, respectively.
.
.SH
The Marvell Kirkwood operating system
.PP
This is an ARM kernel for the ARM926EJ-S processor
and it emulates ARM 7500 floating-point and
CAS (compare-and-swap) instructions.
It is known to run on the Sheevaplug, Guruplug, Dreamplug
and Openrd-client boards.
It is derived from a port of native Inferno to the Sheevaplug
by Salva Peir\f(Jpó\fP and Mechiel Lukkien.
There are many features of the Kirkwood system-on-a-chip
that it does not exploit.
There are currently drivers for up to two
Gigabit Ethernet interfaces,
USB and the console serial port;
we hope to add crypto acceleration, and a video driver for the Openrd-client.
.SH
The Marvell PXA168 operating system
.PP
This is an ARM kernel for the ARM-v5-architecture processor in the
Marvell PXA168 system-on-a-chip
and it emulates ARM 7500 floating-point and
CAS (compare-and-swap) instructions.
It is known to run on the Guruplug Display.
There are many features of the system-on-a-chip
that it does not exploit.
There are currently drivers for
a Fast Ethernet interface,
and the console serial port;
we hope to add crypto acceleration, and a video driver.
.SH
The TI OMAP35 operating system
.PP
This is an ARM kernel for the Cortex-A8 processor
and it emulates ARM 7500 floating-point and
CAS (compare-and-swap) instructions.
It is known to run on the IGEPv2 board and the Gumstix Overo,
and might eventually run on the Beagleboard, once USB is working.
There are many features of the OMAP system-on-a-chip that it does not exploit.
Initially, there are drivers for the SMSC 9221 100Mb/s Ethernet
interface in the IGEPv2 and Overo,
and the console serial port;
we hope to add USB, flash memory and video drivers.
.SH
The Nvidia Tegra2 operating system
.PP
This is an ARM kernel for the dual Cortex-A9 processors
in the Nvidia Tegra2 system-on-a-chip
and it emulates ARM 7500 floating-point and
CAS (compare-and-swap) instructions, but
the hardware includes VFP3 floating-point.
It runs on the Compulab Trimslice.
There are many features of the system-on-a-chip that it does not exploit.
Initially, there are drivers for the Ethernet interface
and the console serial port;
we hope to add USB, flash memory and video drivers.
.SH
The Broadcom 2835 operating system
.PP
This consists of terminal and CPU kernels for the ARM1176 processor
in the Broadcom 2835 system-on-a-chip.
The hardware includes VFP2 floating-point.
It runs on the Raspberry Pi Models A and B.
Since it relies upon USB Ethernet and the Plan 9 USB Ethernet
driver doesn't implement multicast, this port can't speak IPv6.
.
.ig
.SH
The file server
.PP
The file server runs on only a handful of distinct machines.
It is a stand-alone program, distantly related to the CPU server
code, that runs no user code: all it does is serve files on
network connections.
It supports only SCSI disks, which can be interleaved for
faster throughput.
A DOS file on
an IDE drive can hold the configuration information.
See
.I fsconfig (8)
for an explanation of how
to configure a file server.
.PP
To boot a file server, follow the directions for booting a CPU server
using the file name
.CW 9\f2machtype\fPfs
where
.I machtype
is
.CW pc ,
etc. as appropriate.
We are releasing only the PC version.
.SH
The IBM PC file server
.PP
Except for the restriction to SCSI disks,
the PC file server has the same hardware requirements as
the regular PC operating system.
However, only a subset of the supported SCSI (Adaptec 1542, Mylex Multimaster,
and Symbios 53C8XX) and Ethernet (Digital 2114x,
Intel 8255x, and 3Com) controllers
may be
used.
Any of the boot methods described in
.I 9load (8)
will work.
.PP
To boot any PC, the file
.CW 9load
must reside on a MS-DOS formatted floppy, IDE disk,
or SCSI disk.
However, PCs have no non-volatile RAM in which the
file server can store its configuration information, so the system
stores it in a file on an MS-DOS file system instead.
This file, however, cannot live on a SCSI disk, only a floppy or IDE.
(This restriction avoids a lot of duplicated interfaces in the
system.)
Thus the file server cannot be all-SCSI.
See
.I plan9.ini (8)
for details about the
.I nvr
variable and specifying the console device.
.SH
Backup
.PP
Our main file server is unlikely to be much like yours.
It is a PC with 128 megabytes
of cache memory, 56 gigabytes of SCSI magnetic
disk, and a Hewlett-Packard SureStore Optical 1200ex
magneto-optical jukebox, with 1.2 terabytes of storage.
This driver runs the SCSI standard jukebox protocol.
We also have a driver for a (non-standard)
SONY WDA-610
Writable Disk Auto Changer (WORM),
which stores almost 350 gigabytes of data.
.PP
The WORM is actually the prime storage; the SCSI disk is just
a cache to improve performance.
Early each morning the system constructs on WORM an image of
the entire system as it appears that day.  Our backup system
is therefore just a file server that lets
you look at yesterday's (or last year's) file system.
.PP
If you don't have a magneto-optical jukebox,
you might consider attaching a CD-R jukebox or even just
using a single WORM drive and managing the dumps a little less
automatically.  This is just a long way of saying that the
system as distributed has no explicit method of backup other
than through the WORM jukebox.
.PP
Not everyone can invest in such expensive hardware, however.
Although it wouldn't be as luxurious,
it would be possible to use
.I mkfs (8)
to build regular file system archives and use
.I scuzz (8)
to stream them to a SCSI 8mm tape drive.
.CW Mkext
could then extract them.
Another alternative is to use
.I dump9660
(see
.I mk9660 (8)),
which stores incremental backups on CD images
in the form of a dump hierarchy.
.PP
It is also possible to treat a regular disk, or even a part of a disk,
as a fake WORM, which can then be streamed to tape when it fills.
This is a bad idea for a production system but a good way to
learn about the WORM software.
Again, see
.I fsconfig (8)
for details.
..
