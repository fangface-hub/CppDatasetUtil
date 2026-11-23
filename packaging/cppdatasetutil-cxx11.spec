Name:           cppdatasetutil-cxx11
Version:        %{package_version}
Release:        1%{?dist}
Summary:        C++ dataset serialization library (C++11/14 build)
License:        MIT
Source0:        cppdatasetutil-cxx11-%{version}.tar.gz
BuildRequires:  gcc, gcc-c++, make

%global debug_package %{nil}

%description
Library that reads and writes CSV, JSON, YAML and TOML files as a nested
data model, built for C++11 and C++14 users. It can be installed beside the
C++17 package; the header is in %{_includedir}/cppdatasetutil-cxx11 and the
library is libcppdatasetutil_cxx11.

%prep
%setup -q -n cppdatasetutil-cxx11-%{version}

%build
make %{?_smp_mflags} VARIANT=cxx11 CXXFLAGS="%{optflags}"

%install
make install VARIANT=cxx11 DESTDIR="%{buildroot}" PREFIX="%{_prefix}" LIBDIR="%{_libdir}"

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%license %{_datadir}/licenses/cppdatasetutil-cxx11/*
%doc README.md README_ja.md README_de.md README_fr.md README_it.md README_ko.md README_zh.md
%{_includedir}/cppdatasetutil-cxx11/
%{_libdir}/libcppdatasetutil_cxx11.so
%{_libdir}/libcppdatasetutil_cxx11.so.0
