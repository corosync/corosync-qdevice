#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# bootstrap / init
AC_PREREQ([2.61])

AC_INIT([corosync-qdevice],
	m4_esyscmd([build-aux/git-version-gen .tarball-version .gitarchivever]),
	[users@clusterlabs.org])

AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([foreign 1.11])

LT_PREREQ([2.2.6])
LT_INIT

AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([qdevices/corosync-qdevice.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST

AC_LANG([C])

dnl Fix default variables - "prefix" variable if not specified
systemddir=${prefix}/lib/systemd/system

if test "$prefix" = "NONE"; then
	prefix="/usr"

	dnl Fix "localstatedir" variable if not specified
	if test "$localstatedir" = "\${prefix}/var"; then
		localstatedir="/var"
	fi
	dnl Fix "sysconfdir" variable if not specified
	if test "$sysconfdir" = "\${prefix}/etc"; then
		sysconfdir="/etc"
	fi
	if test "$systemddir" = "NONE/lib/systemd/system"; then
		systemddir=/lib/systemd/system
	fi
	dnl Fix "libdir" variable if not specified
	if test "$libdir" = "\${exec_prefix}/lib"; then
		if test -e /usr/lib64; then
			libdir="/usr/lib64"
		else
			libdir="/usr/lib"
		fi
	fi
fi

if test "$srcdir" = "."; then
	AC_MSG_NOTICE([building in place srcdir:$srcdir])
	AC_DEFINE([BUILDING_IN_PLACE], 1, [building in place])
else
	AC_MSG_NOTICE([building out of tree srcdir:$srcdir])
fi

# Checks for programs.

# check stolen from gnulib/m4/gnu-make.m4
if ! ${MAKE-make} --version /cannot/make/this >/dev/null 2>&1; then
	AC_MSG_ERROR([you don't seem to have GNU make; it is required])
fi

AC_PROG_AWK
AC_PROG_GREP
AC_PROG_SED
AC_PROG_CPP
AC_PROG_CC
AC_PROG_CC_C99
if test "x$ac_cv_prog_cc_c99" = "xno"; then
	AC_MSG_ERROR(["C99 support is required"])
fi
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET
PKG_PROG_PKG_CONFIG
AC_PATH_PROG([BASHPATH], [bash])
AC_CHECK_PROGS([GROFF], [groff])

# Checks for typedefs.
AC_TYPE_UID_T
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for libraries.
PKG_CHECK_MODULES([nss],[nss])
PKG_CHECK_MODULES([corosync_common], [libcorosync_common])
PKG_CHECK_MODULES([cmap], [libcmap])
PKG_CHECK_MODULES([votequorum], [libvotequorum])

AC_CONFIG_FILES([Makefile
		 qdevices/Makefile
		 man/Makefile
		 init/Makefile
		 corosync-qdevice.pc
		 ])

# ===============================================
# Helpers
# ===============================================

## helper for CC stuff
cc_supports_flag() {
	BACKUP="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $@ $unknown_warnings_as_errors"
	AC_MSG_CHECKING([whether $CC supports "$@"])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
			  [RC=0; AC_MSG_RESULT([yes])],
			  [RC=1; AC_MSG_RESULT([no])])
	CPPFLAGS="$BACKUP"
	return $RC
}

## local defines
PACKAGE_FEATURES=""

# local options
AC_ARG_ENABLE([fatal-warnings],
	[  --enable-fatal-warnings         : enable fatal warnings. ],
	[ default="no" ])

AC_ARG_ENABLE([debug],
	[  --enable-debug                  : enable debug build. ],
	[ default="no" ])

AC_ARG_ENABLE([secure-build],
	[  --enable-secure-build           : enable PIE/RELRO build. ],
	[],
	[enable_secure_build="yes"])

AC_ARG_ENABLE([user-flags],
	[  --enable-user-flags             : rely on user environment. ],
	[ default="no" ])
AM_CONDITIONAL(BUILD_USERFLAGS, test x$enable_user_flags = xyes)

AC_ARG_ENABLE([systemd],
	      [  --enable-systemd                : Build with libsystemd and install systemd service files],,
	[ enable_systemd="no" ])
AM_CONDITIONAL(INSTALL_SYSTEMD, test x$enable_systemd = xyes)

AC_ARG_WITH([initconfigdir],
	[AS_HELP_STRING([--with-initconfigdir=DIR],
		[configuration directory @<:@SYSCONFDIR/sysconfig@:>@])],
	[INITCONFIGDIR="$withval"],
	[INITCONFIGDIR='${sysconfdir}/sysconfig'])
AC_SUBST([INITCONFIGDIR])

AC_ARG_WITH([initddir],
	[  --with-initddir=DIR     : path to init script directory. ],
	[ INITDDIR="$withval" ],
	[ INITDDIR="$sysconfdir/init.d" ])

AC_ARG_WITH([systemddir],
	[  --with-systemddir=DIR   : path to systemd unit files directory. ],
	[ SYSTEMDDIR="$withval" ],
	[ SYSTEMDDIR="$systemddir" ])

AC_ARG_ENABLE([qdevices],
	[  --disable-qdevices              : Quorum devices support ],,
	[ enable_qdevices="yes" ])
AM_CONDITIONAL(BUILD_QDEVICES, test x$enable_qdevices = xyes)
AC_ARG_ENABLE([qnetd],
	[  --disable-qnetd                 : Quorum Net Daemon support ],,
	[ enable_qnetd="yes" ])
AM_CONDITIONAL(BUILD_QNETD, test x$enable_qnetd = xyes)
AC_ARG_ENABLE([runautogen],
	[  --enable-runautogen             : run autogen.sh (RPM specfile)],,
	[ enable_runautogen="no" ])
AM_CONDITIONAL([BUILD_RUNAUTOGEN], [test x$enable_runautogen = xyes])

# *FLAGS handling goes here

ENV_CFLAGS="$CFLAGS"
ENV_CPPFLAGS="$CPPFLAGS"
ENV_LDFLAGS="$LDFLAGS"

# debug build stuff
if test "x${enable_debug}" = xyes; then
	AC_DEFINE_UNQUOTED([DEBUG], [1], [Compiling Debugging code])
	OPT_CFLAGS="-O0"
	PACKAGE_FEATURES="$PACKAGE_FEATURES debug"
else
	OPT_CFLAGS="-O3"
fi

# gdb flags
if test "x${GCC}" = xyes; then
	GDB_FLAGS="-ggdb3"
else
	GDB_FLAGS="-g"
fi

if test "x${enable_systemd}" = xyes; then
	PKG_CHECK_MODULES([libsystemd], [libsystemd])
	AC_DEFINE([HAVE_LIBSYSTEMD], [1], [have systemd interface library])
	PACKAGE_FEATURES="$PACKAGE_FEATURES systemd"
fi
if test "x${enable_qdevices}" = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES qdevices"
fi
if test "x${enable_qnetd}" = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES qnetd"
fi

# extra warnings
EXTRA_WARNINGS=""

WARNLIST="
	all
	shadow
	missing-prototypes
	missing-declarations
	strict-prototypes
	declaration-after-statement
	pointer-arith
	write-strings
	cast-align
	bad-function-cast
	missing-format-attribute
	format=2
	format-security
	format-nonliteral
	no-long-long
	unsigned-char
	gnu89-inline
	no-strict-aliasing
	"

for j in $WARNLIST; do
	if cc_supports_flag -W$j; then
		EXTRA_WARNINGS="$EXTRA_WARNINGS -W$j";
	fi
done

if test "x${enable_fatal_warnings}" = xyes && \
		cc_supports_flag -Werror ; then
	AC_MSG_NOTICE([Enabling Fatal Warnings (-Werror)])
	WERROR_CFLAGS="-Werror"
	PACKAGE_FEATURES="$PACKAGE_FEATURES fatal-warnings"
else
	WERROR_CFLAGS=""
fi

# don't add addtional cflags
if test "x${enable_user_flags}" = xyes; then
	OPT_CFLAGS=""
	GDB_FLAGS=""
	EXTRA_WARNINGS=""
fi

if test "x${enable_secure_build}" = xyes; then
  # stolen from apache configure snippet
  AC_CACHE_CHECK([whether $CC accepts PIE flags], [ap_cv_cc_pie], [
    save_CFLAGS=$CFLAGS
    save_LDFLAGS=$LDFLAGS
    CFLAGS="$CFLAGS -fPIE"
    LDFLAGS="$LDFLAGS -pie"
    AC_TRY_RUN([static int foo[30000]; int main () { return 0; }],
      [ap_cv_cc_pie=yes], [ap_cv_cc_pie=no], [ap_cv_cc_pie=yes])
    CFLAGS=$save_CFLAGS
    LDFLAGS=$save_LDFLAGS
  ])
  if test "$ap_cv_cc_pie" = "yes"; then
    SEC_FLAGS="$SEC_FLAGS -fPIE"
    SEC_LDFLAGS="$SEC_LDFLAGS -pie"
    PACKAGE_FEATURES="$PACKAGE_FEATURES pie"
  fi

  # similar to above
  AC_CACHE_CHECK([whether $CC accepts RELRO flags], [ap_cv_cc_relro], [
    save_LDFLAGS=$LDFLAGS
    LDFLAGS="$LDFLAGS -Wl,-z,relro"
    AC_TRY_RUN([static int foo[30000]; int main () { return 0; }],
      [ap_cv_cc_relro=yes], [ap_cv_cc_relro=no], [ap_cv_cc_relro=yes])
    LDFLAGS=$save_LDFLAGS
  ])
  if test "$ap_cv_cc_relro" = "yes"; then
    SEC_LDFLAGS="$SEC_LDFLAGS -Wl,-z,relro"
    PACKAGE_FEATURES="$PACKAGE_FEATURES relro"
  fi

  AC_CACHE_CHECK([whether $CC accepts BINDNOW flags], [ap_cv_cc_bindnow], [
    save_LDFLAGS=$LDFLAGS
    LDFLAGS="$LDFLAGS -Wl,-z,now"
    AC_TRY_RUN([static int foo[30000]; int main () { return 0; }],
      [ap_cv_cc_bindnow=yes], [ap_cv_cc_bindnow=no], [ap_cv_cc_bindnow=yes])
    LDFLAGS=$save_LDFLAGS
  ])
  if test "$ap_cv_cc_bindnow" = "yes"; then
    SEC_LDFLAGS="$SEC_LDFLAGS -Wl,-z,now"
    PACKAGE_FEATURES="$PACKAGE_FEATURES bindnow"
  fi
fi

AC_CACHE_CHECK([whether $CC accepts "--as-needed"], [ap_cv_cc_as_needed], [
  save_LDFLAGS=$LDFLAGS
  LDFLAGS="$LDFLAGS -Wl,--as-needed"
  AC_TRY_RUN([static int foo[30000]; int main () { return 0; }],
    [ap_cv_cc_as_needed=yes], [ap_cv_cc_as_needed=no], [ap_cv_cc_as_needed=yes])
  LDFLAGS=$save_LDFLAGS
])

# define global include dirs
INCLUDE_DIRS="$INCLUDE_DIRS -I\$(top_builddir)/include -I\$(top_srcdir)/include"

# final build of *FLAGS
CFLAGS="$ENV_CFLAGS $lt_prog_compiler_pic $SEC_FLAGS $OPT_CFLAGS $GDB_FLAGS \
	$EXTRA_WARNINGS \
	$WERROR_CFLAGS"
CPPFLAGS="$ENV_CPPFLAGS $INCLUDE_DIRS"
LDFLAGS="$ENV_LDFLAGS $lt_prog_compiler_pic $SEC_LDFLAGS"

if test "$ap_cv_cc_as_needed" = "yes"; then
  LDFLAGS="$LDFLAGS -Wl,--as-needed"
fi

# substitute what we need:
AC_SUBST([BASHPATH])
AC_SUBST([INITDDIR])
AC_SUBST([SYSTEMDDIR])
AC_SUBST([LOGDIR])
AC_SUBST([LOGROTATEDIR])

AC_SUBST([SOMAJOR])
AC_SUBST([SOMINOR])
AC_SUBST([SOMICRO])
AC_SUBST([SONAME])

AC_SUBST([NSS_LDFLAGS])

AM_CONDITIONAL(BUILD_HTML_DOCS, test -n "${GROFF}")

AC_DEFINE_UNQUOTED([LOCALSTATEDIR], "$(eval echo ${localstatedir})", [localstate directory])

COROSYSCONFDIR=${sysconfdir}/corosync
AC_SUBST([COROSYSCONFDIR])
AC_DEFINE_UNQUOTED([COROSYSCONFDIR], "$(eval echo ${COROSYSCONFDIR})", [corosync-qdevice config directory])

AC_DEFINE_UNQUOTED([PACKAGE_FEATURES], "${PACKAGE_FEATURES}", [corosync-qdevice built-in features])

AC_OUTPUT

AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE configuration:])
AC_MSG_RESULT([  Version                  = ${VERSION}])
AC_MSG_RESULT([  Prefix                   = ${prefix}])
AC_MSG_RESULT([  Executables              = ${sbindir}])
AC_MSG_RESULT([  Man pages                = ${mandir}])
AC_MSG_RESULT([  Doc dir                  = ${docdir}])
AC_MSG_RESULT([  Libraries                = ${libdir}])
AC_MSG_RESULT([  Header files             = ${includedir}])
AC_MSG_RESULT([  Arch-independent files   = ${datadir}])
AC_MSG_RESULT([  State information        = ${localstatedir}])
AC_MSG_RESULT([  System configuration     = ${sysconfdir}])
AC_MSG_RESULT([  System init.d directory  = ${INITDDIR}])
AC_MSG_RESULT([  System systemd directory = ${SYSTEMDDIR}])
AC_MSG_RESULT([  Log directory            = ${LOGDIR}])
AC_MSG_RESULT([  Log rotate directory     = ${LOGROTATEDIR}])
AC_MSG_RESULT([  corosync config dir      = ${COROSYSCONFDIR}])
AC_MSG_RESULT([  init config directory    = ${INITCONFIGDIR}])
AC_MSG_RESULT([  Features                 = ${PACKAGE_FEATURES}])
AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE build info:])
AC_MSG_RESULT([  Default optimization     = ${OPT_CFLAGS}])
AC_MSG_RESULT([  Default debug options    = ${GDB_FLAGS}])
AC_MSG_RESULT([  Extra compiler warnings  = ${EXTRA_WARNING}])
AC_MSG_RESULT([  Env. defined CFLAG       = ${ENV_CFLAGS}])
AC_MSG_RESULT([  Env. defined CPPFLAGS    = ${ENV_CPPFLAGS}])
AC_MSG_RESULT([  Env. defined LDFLAGS     = ${ENV_LDFLAGS}])
AC_MSG_RESULT([  Fatal War.   CFLAGS      = ${WERROR_CFLAGS}])
AC_MSG_RESULT([  Final        CFLAGS      = ${CFLAGS}])
AC_MSG_RESULT([  Final        CPPFLAGS    = ${CPPFLAGS}])
AC_MSG_RESULT([  Final        LDFLAGS     = ${LDFLAGS}])
