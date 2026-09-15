# Try to configure the Irrlicht 3D client (gui-irrlicht)

dnl FC_IRRLICHT_CLIENT
dnl Test for the Irrlicht Engine (built out-of-tree) and the X11/GL libs the
dnl gui-irrlicht backend links against. The Irrlicht static library is expected
dnl at <prefix>/lib/libIrrlicht.a with headers in <prefix>/include (see
dnl PHASE0_NOTES.md for how it is built).

AC_DEFUN([FC_IRRLICHT_CLIENT],
[
  if test "x$gui_irrlicht" = "xyes" || test "x$client" = "xall" ||
     test "x$client" = "xauto" ; then

    found_irrlicht_client=no

    dnl Default prefix; overridable via --with-irrlicht=DIR (defined in
    dnl configure.ac). Falls back to the build location used on this box.
    if test -z "$irrlicht_prefix" ; then
      irrlicht_prefix="$HOME/opt/irrlicht"
    fi

    if test -f "$irrlicht_prefix/include/irrlicht.h" && \
       test -f "$irrlicht_prefix/lib/libIrrlicht.a" ; then
      gui_irrlicht_cppflags="-I$irrlicht_prefix/include"
      dnl The (2012-era) Irrlicht headers need a modern C++ mode and are noisy.
      gui_irrlicht_cxxflags="-std=gnu++17 -w"
      gui_irrlicht_ldflags="-L$irrlicht_prefix/lib"
      gui_irrlicht_libs="-lIrrlicht -lGL -lGLU -lX11 -lXext -lXcursor \
        -lXinerama -lXrandr -lXxf86vm -lz -lpng -ljpeg -lpthread -lm"
      found_irrlicht_client=yes
    fi

    if test "$found_irrlicht_client" = yes; then
      gui_irrlicht=yes
      if test "x$client" = "xauto" ; then
        client=yes
      fi
      AC_DEFINE([HAVE_IRRLICHT], [1], [Irrlicht 3D client enabled])
      AC_MSG_RESULT([Irrlicht client: yes ($irrlicht_prefix)])
    elif test "x$gui_irrlicht" = "xyes"; then
      AC_MSG_ERROR([specified client 'irrlicht' not configurable (Irrlicht Engine not found at $irrlicht_prefix; build libIrrlicht.a there or pass --with-irrlicht=DIR)])
    fi
  fi
])
