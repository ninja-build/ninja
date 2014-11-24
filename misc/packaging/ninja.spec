Summary: Ninja is a small build system with a focus on speed.
Name: ninja
Version: %{ver}
Release: %{rel}%{?dist}
Group: Development/Tools
License: Apache 2.0
URL: https://github.com/martine/ninja
Source0: %{name}-%{version}-%{rel}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{rel}

BuildRequires: asciidoc

%description
Ninja is yet another build system. It takes as input the interdependencies of files (typically source code and output executables) and
orchestrates building them, quickly.

Ninja joins a sea of other build systems. Its distinguishing goal is to be fast. It is born from my work on the Chromium browser project,
which has over 30,000 source files and whose other build systems (including one built from custom non-recursive Makefiles) can take ten
seconds to start building after changing one file. Ninja is under a second.

%prep
%setup -q -n %{name}-%{version}-%{rel}

%build
echo Building..
./configure.py --bootstrap
./ninja manual

%install
mkdir -p %{buildroot}%{_bindir} %{buildroot}%{_docdir}
cp -p ninja %{buildroot}%{_bindir}/

%files
%defattr(-, root, root)
%doc COPYING README doc/manual.html
%{_bindir}/*

%clean
rm -rf %{buildroot}

#The changelog is built automatically from Git history
%changelog
