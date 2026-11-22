Name:           cppdatasetutil
Version:        %{package_version}
Release:        1%{?dist}
Summary:        C++ dataset serialization library
License:        MIT
Source0:        cppdatasetutil-%{version}.tar.gz
BuildRequires:  gcc, gcc-c++, make

%global debug_package %{nil}

%description
C++17 library for nested data with CSV, JSON, YAML and TOML serialization.
Includes the shared library, development header, and man page.

%prep
%setup -q -n cppdatasetutil-%{version}

%build
make %{?_smp_mflags} CXXFLAGS="%{optflags}"

%install
make install DESTDIR="%{buildroot}" PREFIX="%{_prefix}" LIBDIR="%{_libdir}" MANDIR="%{_mandir}"

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%license %{_datadir}/licenses/cppdatasetutil/*
%doc README.md README_ja.md README_de.md README_fr.md README_it.md README_ko.md README_zh.md
%{_includedir}/Dataset.h
%{_libdir}/libcppdatasetutil.so
%{_libdir}/libcppdatasetutil.so.0
%{_mandir}/man3/CppDataSet.3*
%lang(ja) %{_mandir}/ja/man3/CppDataSet.3*
%lang(de) %{_mandir}/de/man3/CppDataSet.3*
%lang(fr) %{_mandir}/fr/man3/CppDataSet.3*
%lang(it) %{_mandir}/it/man3/CppDataSet.3*
%lang(ko) %{_mandir}/ko/man3/CppDataSet.3*
%lang(zh_CN) %{_mandir}/zh_CN/man3/CppDataSet.3*
