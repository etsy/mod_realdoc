%{!?_httpd_apxs: %{expand: %%global _httpd_apxs %%{_sbindir}/apxs}}
%{!?_httpd_mmn: %{expand: %%global _httpd_mmn %%(cat %{_includedir}/httpd/.mmn || echo missing-httpd-devel)}}
# /etc/httpd/conf.d with httpd < 2.4 and defined as /etc/httpd/conf.modules.d with httpd >= 2.4
%{!?_httpd_modconfdir: %{expand: %%global _httpd_modconfdir %%{_sysconfdir}/httpd/conf.d}}
%{!?_httpd_confdir:    %{expand: %%global _httpd_confdir    %%{_sysconfdir}/httpd/conf.d}}
%{!?_httpd_moddir:    %{expand: %%global _httpd_moddir    %%{_libdir}/httpd/modules}}

%global gh_commit    1f9cbeda1b5e037fc9e460b437a884e9e9a4f4ae
%global gh_short     %(c=%{gh_commit}; echo ${c:0:7})

Summary: realpath document root module for the Apache HTTP Server
Name: mod_realdoc
Version: 0.0.1_8_%{gh_short}
Release: 0.2%{?dist}
License: MIT
URL: https://github.com/etsy/mod_realdoc
Group: System Environment/Daemons
Source: https://github.com/etsy/mod_realdoc/archive/%{gh_commit}/mod_realdoc-%{gh_commit}.tar.gz
Source1: mod_realdoc.conf
Source2: 10-mod_realdoc.conf

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires: httpd httpd-mmn = %{_httpd_mmn}
BuildRequires: httpd-devel

%description
mod_realdoc is an Apache module which does a realpath on the
docroot symlink and sets the absolute path as the real document root for
the remainder of the request.

%prep
%setup -q -n mod_realdoc-%{gh_commit}

%build
%{_httpd_apxs} -c mod_realdoc.c

%install
rm -rf %{buildroot}

install -d %{buildroot}%{_httpd_moddir}

install -m0755 .libs/mod_realdoc.so %{buildroot}%{_httpd_moddir}/%{name}.so

%if "%{_httpd_modconfdir}" != "%{_httpd_confdir}"
# 2.4-style
install -Dp -m0644 %{SOURCE2} %{buildroot}%{_httpd_modconfdir}/10-%{name}.conf
install -Dp -m0644 %{SOURCE1} %{buildroot}%{_httpd_confdir}/%{name}.conf
%else
# 2.2-style
install -d -m0755 %{buildroot}%{_httpd_confdir}
cat %{SOURCE2} %{SOURCE1} > %{buildroot}%{_httpd_confdir}/%{name}.conf
%endif
install -m 700 -d $RPM_BUILD_ROOT%{_localstatedir}/lib/%{name}

%clean
rm -rf %{buildroot}

%files
%defattr (-,root,root)
%doc LICENSE README.md
%{_httpd_moddir}/%{name}.so
%config(noreplace) %{_httpd_confdir}/*.conf
%if "%{_httpd_modconfdir}" != "%{_httpd_confdir}"
%config(noreplace) %{_httpd_modconfdir}/*.conf
%endif
%attr(770,apache,root) %dir %{_localstatedir}/lib/%{name}

%changelog
* Fri Nov 14 2014 Andy Thompson <andy@webtatic.com> - 0.0.1_8_1f9cbed-0.2
- Update to latest commit

* Sun Oct 19 2014 Andy Thompson <andy@webtatic.com> - 0.0.1_7_397808c-0.1
- Create SPEC for redhat release
