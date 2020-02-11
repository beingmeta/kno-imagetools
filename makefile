KNOCONFIG         = knoconfig
KNOBUILD          = knobuild

prefix		::= $(shell ${KNOCONFIG} prefix)
libsuffix	::= $(shell ${KNOCONFIG} libsuffix)
KNO_CFLAGS	::= -I. -fPIC $(shell ${KNOCONFIG} cflags)
KNO_LDFLAGS	::= -fPIC $(shell ${KNOCONFIG} ldflags)
PACKAGE_CFLAGS  ::= $(shell etc/pkc --cflags libexif) \
		    $(shell etc/pkc --cflags libqrencode) \
		    $(shell etc/pkc --cflags ImageMagick) \
		    $(shell etc/pkc --cflags MagickWand)
PACKAGE_LDFLAGS ::= $(shell etc/pkc --libs libexif) \
		    $(shell etc/pkc --libs libqrencode) \
		    $(shell etc/pkc --libs ImageMagick) \
		    $(shell etc/pkc --libs MagickWand)
CFLAGS		::= ${CFLAGS} ${PACKAGE_CFLAGS} ${KNO_CFLAGS} 
LDFLAGS		::= ${LDFLAGS} ${PACKAGE_LDFLAGS} ${KNO_LDFLAGS}
CMODULES	::= $(DESTDIR)$(shell ${KNOCONFIG} cmodules)
LIBS		::= $(shell ${KNOCONFIG} libs)
LIB		::= $(shell ${KNOCONFIG} lib)
INCLUDE		::= $(shell ${KNOCONFIG} include)
KNO_VERSION	::= $(shell ${KNOCONFIG} version)
KNO_MAJOR	::= $(shell ${KNOCONFIG} major)
KNO_MINOR	::= $(shell ${KNOCONFIG} minor)
PKG_RELEASE	::= $(cat ./etc/release)
DPKG_NAME	::= $(shell ./etc/dpkgname)
SUDO            ::= $(shell which sudo)

MKSO		  = $(CC) -shared $(CFLAGS) $(LDFLAGS) $(LIBS)
MSG		  = echo
SYSINSTALL        = /usr/bin/install -c

PKG_NAME	  = imagetools
GPGID             = FE1BC737F9F323D732AA26330620266BE5AFF294
PKG_VERSION	  = ${KNO_MAJOR}.${KNO_MINOR}.${PKG_RELEASE}
PKG_RELEASE     ::= $(shell cat etc/release)
CODENAME	::= $(shell ${KNOCONFIG} codename)
REL_BRANCH	::= $(shell ${KNOBUILD} getbuildopt REL_BRANCH current)
REL_STATUS	::= $(shell ${KNOBUILD} getbuildopt REL_STATUS stable)
REL_PRIORITY	::= $(shell ${KNOBUILD} getbuildopt REL_PRIORITY medium)
ARCH            ::= $(shell ${KNOBUILD} getbuildopt BUILD_ARCH || uname -m)
APKREPO         ::= $(shell ${KNOBUILD} getbuildopt APKREPO /srv/repo/kno/apk)
APK_ARCH_DIR      = ${APKREPO}/staging/${ARCH}

default build: qrcode.${libsuffix} exif.${libsuffix} imagick.${libsuffix}

%.o: %.c makefile
	@$(CC) $(CFLAGS) -o $@ -c $<
	@$(MSG) CC $@
%.so: %.o
	$(MKSO) $(LDFLAGS) -o $@ $^ ${LDFLAGS}
	@if test ! -z "${COPY_CMODS}"; then cp $@ ${COPY_CMODS}; fi;
	@$(MSG) MKSO  $@ $<
	@ln -sf $(@F) $(@D)/$(@F).${KNO_MAJOR}
%.dylib: %.o makefile
	@$(MACLIBTOOL) -install_name \
		`basename $(@F) .dylib`.${KNO_MAJOR}.dylib \
		${CFLAGS} ${LDFLAGS} -o $@ $(DYLIB_FLAGS) \
		$^
	@if test ! -z "${COPY_CMODS}"; then cp $@ ${COPY_CMODS}; fi;
	@$(MSG) MACLIBTOOL  $@ $<

TAGS: exif.c qrcode.c imagick.c
	etags -o TAGS $<

${CMODULES}:
	install -d $@

install: build ${CMODULES}
	@for mod_name in qrcode exif imagick; do \
	  ${SUDO} ${SYSINSTALL} $${mod_name}.${libsuffix} \
				 ${CMODULES}/$${mod_name}.so.${PKG_VERSION} && \
	  echo === Installed ${CMODULES}/$${mod_name}.so.${PKG_VERSION} && \
	  ${SUDO} ln -sf $${mod_name}.so.${PKG_VERSION} \
			${CMODULES}/$${mod_name}.so.${KNO_MAJOR}.${KNO_MINOR} && \
	  echo === Linked ${CMODULES}/${m	od_name}.so.${KNO_MAJOR}.${KNO_MINOR} \
		to $${mod_name}.so.${PKG_VERSION} && \
	  ${SUDO} ln -sf $${mod_name}.so.${PKG_VERSION} \
			${CMODULES}/$${mod_name}.so.${KNO_MAJOR} && \
	  echo === Linked ${CMODULES}/$${mod_name}.so.${KNO_MAJOR} \
		to $${mod_name}.so.${PKG_VERSION} && \
	  ${SUDO} ln -sf $${mod_name}.so.${PKG_VERSION} ${CMODULES}/$${mod_name}.so && \
	  echo === Linked ${CMODULES}/$${mod_name}.so to $${mod_name}.so.${PKG_VERSION}; \
	done;

clean:
	rm -f *.o *.${libsuffix}
fresh:
	make clean
	make default

gitup gitup-trunk:
	git checkout trunk && git pull

# Debian packaging

DEBFILES=changelog.base compat control copyright dirs docs files install

debian: imagick.c qrcode.c exif.c makefile \
	dist/debian/rules dist/debian/control \
	dist/debian/changelog.base
	rm -rf debian
	cp -r dist/debian debian
	cd debian; chmod a-x ${DEBFILES}

debian/changelog: debian imagick.c qrcode.c exif.c makefile
	cat debian/changelog.base | \
		knobuild debchangelog kno-${PKG_NAME} ${CODENAME} \
			${REL_BRANCH} ${REL_STATUS} ${REL_PRIORITY} \
	    > $@.tmp
	if test ! -f debian/changelog; then \
	  mv debian/changelog.tmp debian/changelog; \
	elif diff debian/changelog debian/changelog.tmp 2>&1 > /dev/null; then \
	  mv debian/changelog.tmp debian/changelog; \
	else rm debian/changelog.tmp; fi

dist/debian.built: imagick.c qrcode.c exif.c makefile debian debian/changelog
	dpkg-buildpackage -sa -us -uc -b -rfakeroot && \
	touch $@

dist/debian.signed: dist/debian.built
	debsign --re-sign -k${GPGID} ../kno-imagetools_*.changes && \
	touch $@

deb debs dpkg dpkgs: dist/debian.signed

dist/debian.updated: dist/debian.signed
	dupload -c ./dist/dupload.conf --nomail --to bionic ../kno-imagetools_*.changes && touch $@

update-apt: dist/debian.updated

debclean: clean
	rm -rf ../kno-imagetools_* ../kno-imagetools-* debian dist/debian.*

debfresh:
	make debclean
	make dist/debian.signed

# Alpine packaging

staging/alpine:
	@install -d $@

staging/alpine/APKBUILD: dist/alpine/APKBUILD staging/alpine
	cp dist/alpine/APKBUILD staging/alpine

staging/alpine/kno-${PKG_NAME}.tar: staging/alpine
	git archive --prefix=kno-${PKG_NAME}/ -o staging/alpine/kno-${PKG_NAME}.tar HEAD

dist/alpine.done: staging/alpine/APKBUILD makefile \
	staging/alpine/kno-${PKG_NAME}.tar
	if [ ! -d ${APK_ARCH_DIR} ]; then mkdir -p ${APK_ARCH_DIR}; fi;
	cd staging/alpine; \
		abuild -P ${APKREPO} clean cleancache cleanpkg && \
		abuild checksum && \
		abuild -P ${APKREPO} && \
		touch ../../$@

alpine: dist/alpine.done

.PHONY: alpine

