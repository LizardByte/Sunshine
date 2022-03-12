Name:           Sunshine
Version:        
Release:        1%{?dist}
Summary:        An NVIDIA Gamestream-Compatible Server

License:        GPLv3
URL:            https://github.com/SunshineStream/Sunshine
Source0:        

BuildRequires:  
Requires:       

%description


%prep
%autosetup


%build
%configure
%make_build


%install
%make_install


%files
%license add-license-file-here
%doc add-docs-here



%changelog
* Sat Mar 12 2022 h <65380846+thatsysadmin@users.noreply.github.com>
- 
