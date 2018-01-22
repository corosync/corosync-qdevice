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

qnetd_man		= corosync-qnetd-tool.8 \
                          corosync-qnetd-certutil.8 \
                          corosync-qnetd.8

qdevices_man		= corosync-qdevice-tool.8 \
                          corosync-qdevice-net-certutil.8 \
                          corosync-qdevice.8

EXTRA_DIST		= $(qnetd_man) \
			  $(qdevices_man)

dist_man_MANS		=

if BUILD_QNETD
dist_man_MANS		+= $(qnetd_man)
endif

if BUILD_QDEVICES
dist_man_MANS		+= $(qdevices_man)
endif

HTML_DOCS 		= $(dist_man_MANS:%=%.html) $(man_MANS:%=%.html)

# developer man page generation
%.3: %.3.in $(autogen_common)
	@echo Generating $@ man page && \
	rm -f $@-t-t $@-t $@ && \
	date="$$(LC_ALL=C date "+%F" $${SOURCE_DATE_EPOCH+-d @$$SOURCE_DATE_EPOCH})" && \
	awk "{print}(\$$1 ~ /@COMMONIPCERRORS@/){exit 0}" ${top_srcdir}/man/$@.in > $@-t-t && \
	cat ${top_srcdir}/man/$(autogen_common) >> $@-t-t && \
	awk -v p=0 "(\$$1 ~ /@COMMONIPCERRORS@/){p = 1} {if(p==1)print}" ${top_srcdir}/man/$@.in >> $@-t-t && \
	cat $@-t-t | \
		sed -e 's#@BUILDDATE@#'$$date'#g' \
		    -e 's#@COMMONIPCERRORS@##g' \
		    > $@-t && \
	rm -f $@-t-t && \
	mv $@-t $@

clean-local:
	rm -rf $(HTML_DOCS) $(autogen_man)

if BUILD_HTML_DOCS
%.html: %
	$(GROFF) -mandoc -Thtml $^ > $@

install-data-local:
	$(INSTALL) -d $(DESTDIR)/${docdir}/html
	$(INSTALL) -m 644 $(HTML_DOCS) $(DESTDIR)/${docdir}/html/

uninstall-local:
	cd $(DESTDIR)/${docdir}/html && rm -f $(HTML_DOCS)
	rmdir $(DESTDIR)/${docdir}/html 2> /dev/null || :

all-local: $(HTML_DOCS)
endif
