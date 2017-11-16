%global _enable_debug_package 0
%global debug_package %{nil}
%global __os_install_post /usr/lib/rpm/brp-compress %{nil}

Summary:     mdadm is used for controlling Linux md devices (aka RAID arrays)
Name:        blockbridge-mdadm
Version:     4.0
Release:     %{BB_BUILD_NUMBER}
Source:      blockbridge-mdadm-%{version}.tar.gz
License:     GPL
Group:       Utilities/System
BuildRoot:   %{_tmppath}/%{name}-root
Requires:    bb-libcurl >= 7.29.0

%description
mdadm is a program that can be used to create, manage, and monitor
Linux MD (Software RAID) devices.

%prep
%setup -q
# we want to install in /sbin, not /usr/sbin...
%define _exec_prefix %{nil}

%build
# This is a debatable issue. The author of this RPM spec file feels that
# people who install RPMs (especially given that the default RPM options
# will strip the binary) are not going to be running gdb against the
# program.
make CXFLAGS="$RPM_OPT_FLAGS" BINDIR=%{_sbindir} SYSCONFDIR="%{_sysconfdir}"

%install
make DESTDIR=$RPM_BUILD_ROOT MANDIR=%{_mandir} BINDIR=%{_sbindir} install
install -D -m644 mdadm.conf-example $RPM_BUILD_ROOT/%{_sysconfdir}/mdadm.conf
install -D -m644 systemd/mdmon@.service $RPM_BUILD_ROOT/etc/systemd/system/mdmon@.service

# cleanup unpackages files
cd $RPM_BUILD_ROOT
rm -f TODO ChangeLog mdadm.conf-example COPYING
rm -rf $RPM_BUILD_ROOT/usr/lib/udev/rules.d/
rm -rf $RPM_BUILD_ROOT/%{_sysconfdir}/mdadm.conf
rm -rf $RPM_BUILD_ROOT/%{_mandir}/man*

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%{_sbindir}/mdadm
%{_sbindir}/mdmon
/etc/systemd/system/mdmon@.service

%changelog
