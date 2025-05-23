# Copyright (c) 2004 MontaVista Software, Inc.
# Copyright (c) 2009 - 2018 Red Hat, Inc.
#
# Authors: Jan Friesse (jfriesse@redhat.com)
#          Steven Dake (sdake@redhat.com)
#          Fabio M. Di Nitto (fdinitto@redhat.com)
#
# All rights reserved.
#
# This software licensed under BSD license, the text of which follows:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the Red Hat, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

MAINTAINERCLEANFILES	= Makefile.in

EXTRA_DIST		= corosync-qdevice.sysconfig.example corosync-qdevice.in \
                          corosync-qdevice.service.in \
                          corosync-qnetd.sysconfig.example corosync-qnetd.in \
                          corosync-qnetd.service.in \
                          corosync-qnetd.sysusers.conf.example

if INSTALL_SYSTEMD
systemdconfdir     = $(SYSTEMDDIR)
systemdconf_DATA	=
else
initscriptdir      = $(INITDDIR)
initscript_SCRIPTS	=
endif

if BUILD_QDEVICES
if INSTALL_SYSTEMD
systemdconf_DATA   += corosync-qdevice.service
else
initscript_SCRIPTS  += corosync-qdevice
endif
endif

if BUILD_QNETD
if INSTALL_SYSTEMD
systemdconf_DATA   += corosync-qnetd.service
else
initscript_SCRIPTS  += corosync-qnetd
endif
endif

if BUILD_INSTALL_QNETD_IN_SBIN
qnetddir=$(sbindir)
else
qnetddir=$(bindir)
endif

%: %.in Makefile
	rm -f $@-t $@
	cat $< | sed \
		-e 's#@''SBINDIR@#$(sbindir)#g' \
		-e 's#@''BINDIR@#$(bindir)#g' \
		-e 's#@''SYSCONFDIR@#$(sysconfdir)#g' \
		-e 's#@''INITCONFIGDIR@#$(INITCONFIGDIR)#g' \
		-e 's#@''INITDDIR@#$(INITDDIR)#g' \
		-e 's#@''LOCALSTATEDIR@#$(localstatedir)#g' \
		-e 's#@''BASHPATH@#${BASHPATH}#g' \
		-e 's#@''QNETDDIR@#$(qnetddir)#g' \
	    > $@-t
	mv $@-t $@

all-local: $(initscript_SCRIPTS) $(systemdconf_DATA)

clean-local:
	rm -rf $(initscript_SCRIPTS) $(systemdconf_DATA)
