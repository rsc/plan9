*U9fs* serves the Plan 9 protocol 9P from user-space on other operating systems.

It runs on many POSIX-compatible systems, including Linux and MacOS X.
Currently, it must first be compiled. See the comments at the top of the makefile
for a few special instructions. Ordinarily, you should be able to type:

	make
to create an executable called u9fs.

See the manual page u9fs.man for details of options and arguments.

Unfortunately, installing the program to run automatically under inetd, xinetd or equivalent
is rather system-dependent. (MacOS X is an extreme case.) The rest of this file will list
recipes known so far.

* **Ubuntu 11.04** (and earlier, and later), with xinetd and authrhosts
	I keep u9fs in a new directory /bin/9, but it could easily be in /usr/local/bin.
	It is not setuid. I use the following in /etc/xinetd.d/u9fs:
		service u9fs
		{
			socket_type	= stream
			user		= root
			instances	= UNLIMITED
			wait		= no
			server		= /bin/9/u9fs
			port = 564
		}
	It keeps the default log file in /tmp/u9fs.log.
	It's an internal machine, and I use rhosts authentication (which is the default):
	I list acceptable machines in /etc/hosts.equiv, and the server trusts what they send.
	-- charles.forsyth@gmail.com, May 2011

* **Debian 7** (and earlier), with inetd, and authp9any
	I use this configuration on several virtual servers.
	I keep u9fs in a new directory /bin/9. It is not setuid. I use the following in /etc/inetd.conf:
		u9fs stream tcp nowait root /bin/9/u9fs u9fs -a p9any
	I had to add the following to /etc/services:
		# Local services
		u9fs	564/tcp
	The machine is not an internal machine, and I use p9any authentication (usual Plan 9 variant).
	It takes the secrets from /etc/u9fs.key, which had better be well-protected.
	There are three lines: the secret; the authentication user ("bootes"); the authentication domain.
		-- charles.forsyth@gmail.com, May 2015

* **OpenBSD 4.3**, with inetd, and authrhosts; same on FreeBSD 4.8(!)
	I use this configuration on an internal gateway.
	I keep u9fs in directory /bin/9. /etc/inetd.conf has the following line:
		p9fs        stream  tcp     nowait  root    /bin/9/u9fs u9fs
	The protocol name "p9fs" is already in /etc/services.
		-- charles.forsyth@gmail.com, May 2011

* **MacOS X** (last tested on OS X Yosemete (10.10.5)
	U9fs can be started via ssh using *srvssh*(4) on Plan 9, or more conventionally by MacOS X's *launchd*(8).
	Launchd needs a configuration file. A sample is included here in the file **p9fs.plist**.
	To make the service available globally, it should be installed as **/Library/LaunchDaemons/9pfs.plist**.
	If instead it is installed in **/Library/LaunchAgents**, it will run only when a user is logged in;
	if installed in **$HOME/Library/LaunchAgents** it will run only when that particular user is logged in.

	In order to start the listener it must first be loaded into *launchd*:

	$ **sudo launchctl load /path/to/9pfs.plist**

	If you are running the Mac OS X firewall you will need to add an entry to pass the *9pfs* protocol in:
	**SystemPreferences->Sharing->Firewall**

	The example **9pfs.plist** uses 9p authentication, described in detail in *u9fs.man*, and serves the root of the  MacOS X file system.
	It also assumes the executable lives in **/bin/9/u9fs**. Edit the configuration file to change those settings.
		-- charles.forsyth@gmail.com, September 2015, based on an entry in the Plan 9 wiki
