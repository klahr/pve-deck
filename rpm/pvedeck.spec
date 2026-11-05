Name:       pvedeck
Summary:    Proxmox VE client
Version:    0.1.1
Release:    1
License:    GPL-3.0-or-later
URL:        https://github.com/klahr/pve-deck
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(sailfishsecrets)
BuildRequires:  desktop-file-utils

%description
Monitor and control Proxmox VE virtual machines and containers,
and open a shell, from Sailfish OS.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 VERSION=%{version}
%make_build

%install
%qmake5_install
# Bundled third-party code: its license travels with the binary.
install -D -m 644 3rdparty/libvterm/LICENSE %{buildroot}%{_datadir}/%{name}/licenses/libvterm.txt

desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
   %{buildroot}%{_datadir}/applications/*.desktop

%check
make -C tests check

%files
%defattr(-,root,root,-)
%license LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
