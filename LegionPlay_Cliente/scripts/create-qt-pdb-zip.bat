rem Run from Qt command prompt without changing directories

del symbols.zip
7z a symbols.zip *.pdb -r -xr!Qt5WebEngineCore*