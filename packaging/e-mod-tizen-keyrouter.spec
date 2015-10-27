%bcond_with x
%bcond_with wayland

Name: e-mod-tizen-keyrouter
Version: 0.1.5
Release: 1
Summary: The Enlightenment Keyrouter Module for Tizen
URL: http://www.enlightenment.org
Group: Graphics & UI Framework/Other
Source0: %{name}-%{version}.tar.gz
License: BSD-2-Clause
BuildRequires: pkgconfig(enlightenment)
BuildRequires:  gettext
%if %{with x}
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xi)
BuildRequires:  pkgconfig(xtst)
BuildRequires:  pkgconfig(xrandr)
BuildRequires:  pkgconfig(utilX)
%endif
%if %{with wayland}
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(tizen-extension-server)
%endif
BuildRequires:  pkgconfig(dlog)
%if "%{?profile}" == "common"
%else
BuildRequires:  e-tizen-data
%endif
%description
This package is a the Enlightenment Keyrouter Module for Tizen.

%prep
%setup -q

%build

export GC_SECTIONS_FLAGS="-fdata-sections -ffunction-sections -Wl,--gc-sections"
export CFLAGS+=" -Wall -g -fPIC -rdynamic ${GC_SECTIONS_FLAGS}"
export LDFLAGS+=" -Wl,--hash-style=both -Wl,--as-needed -Wl,--rpath=/usr/lib"

%autogen
%if %{with wayland}
%configure --prefix=/usr \
           --enable-wayland-only \
           --with-tizen-keylayout-file=/usr/share/X11/xkb/tizen_key_layout.txt
%else
%configure --prefix=/usr \
           --with-tizen-keylayout-file=/usr/share/X11/xkb/tizen_key_layout.txt
%endif

make

%install
rm -rf %{buildroot}

# for license notification
mkdir -p %{buildroot}/usr/share/license
cp -a %{_builddir}/%{buildsubdir}/COPYING %{buildroot}/usr/share/license/%{name}

# install
make install DESTDIR=%{buildroot}

# clear useless textual files
find  %{buildroot}%{_libdir}/enlightenment/modules/%{name} -name *.la | xargs rm

%files
%defattr(-,root,root,-)
%{_libdir}/enlightenment/modules/e-mod-tizen-keyrouter
/usr/share/license/%{name}
