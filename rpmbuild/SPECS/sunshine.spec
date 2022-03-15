Name:           sunshine
Version:        @PROJECT_VERSION@
Release:        master
Summary:        An NVIDIA Gamestream-Compatible Server
BuildArch:      x86_64
BuildRoot:      @CMAKE_CURRENT_BINARY_DIR@/rpmbuild/

License:        GNU GPLv3
URL:            https://github.com/SunshineStream/Sunshine
Source0:        

BuildRequires:  
Requires:       systemd

%description
An NVIDIA Gamestream-Compatible Server for streaming video games over the local network of over the internets!

%prep
%setup -q

%pre
#!/bin/sh

# Sunshine Pre-Install Script
# Store backup for old config files to prevent it from being overwritten
if [ -f /etc/sunshine/sunshine.conf ]; then
        cp /etc/sunshine/sunshine.conf /etc/sunshine/sunshine.conf.old
fi

if [ -f /etc/sunshine/apps_linux.json ]; then
        cp /etc/sunshine/apps_linux.json /etc/sunshine/apps_linux.json.old
fi

%post
#!/bin/sh

# Sunshine Post-Install Script
export GROUP_INPUT=input

if [ -f /etc/group ]; then
        if ! grep -q $GROUP_INPUT /etc/group; then
                echo "Creating group $GROUP_INPUT"

                groupadd $GROUP_INPUT
        fi
else
        echo "Warning: /etc/group not found"
fi

if [ -f /etc/sunshine/sunshine.conf.old ]; then
	echo "Restoring old sunshine.conf"
	mv /etc/sunshine/sunshine.conf.old /etc/sunshine/sunshine.conf
fi

if [ -f /etc/sunshine/apps_linux.json.old ]; then
	echo "Restoring old apps_linux.json"
	mv /etc/sunshine/apps_linux.json.old /etc/sunshine/apps_linux.json
fi

# Update permissions on config files for Web Manager
if [ -f /etc/sunshine/apps_linux.json ]; then
	echo "chmod 666 /etc/sunshine/apps_linux.json"
	chmod 666 /etc/sunshine/apps_linux.json
fi

if [ -f /etc/sunshine/sunshine.conf ]; then
	echo "chmod 666 /etc/sunshine/sunshine.conf"
	chmod 666 /etc/sunshine/sunshine.conf
fi

# Ensure Sunshine can grab images from KMS
path_to_setcap=$(which setcap)
if [ -x "$path_to_setcap" ] ; then
  echo "$path_to_setcap cap_sys_admin+p /usr/bin/sunshine"
	$path_to_setcap cap_sys_admin+p /usr/bin/sunshine
fi

%install
install -m 755 sunshine %{buildroot}/usr/bin/sunshine

%files
%{_bindir}/sunshine

%license add-license-file-here

%changelog
* Sat Mar 12 2022 h <65380846+thatsysadmin@users.noreply.github.com>
- Initial packaging of Sunshine.
