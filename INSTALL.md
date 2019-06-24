NXWEB Installation Notes
========================

## 0. Download & Extract

```tar xfz nxweb-***.tar.gz
cd nxweb-***
```

*** - is the version number.

## 1. Configure

```./configure
```

This will configure NXWEB without SSL support and without ImageMagick image filter.
The script will automatically detect if zlib library is available on your computer
to enable gzip content compression filter.

```./configure --without-zlib
```

Disable gzip filter even if zlib is present.

```./configure --with-gnutls
```

This will configure NXWEB with SSL support. GNUTLS library should be available
at one of default locations.
GNUTLS version 3.0.12+ is required.

```./configure --with-gnutls=/install-prefix-of-gnutls
```

Specify GNUTLS location explicitly.

```./configure --with-imagemagick
```

This will configure NXWEB to use ImageMagick library. ImageMagick should be available
at one of default locations.

ImageMagick version 7.0.0+ is required.

```./configure --with-imagemagick=/install-prefix-of-imagemagick
```

Specify ImageMagick location explicitly.

```./configure --with-python
```

This will configure NXWEB with Python WSGI support. python-devel package is required.

```./configure --prefix=/path-to-install
```

This is how you can change NXWEB binary, includes, and libraries install location.

```./configure --disable-certificates
```

This will skip sample SSL certificate generation.

## 2. Adjust Parameters

You can tune NXWEB parameters by modifying its header files,
src/include/nxweb_config.h in particular.

## 3. Compile

```make
```

After successful compilation the following files become available:
    - src/bin/nxweb       -- NXWEB binary
    - src/include         -- NXWEB include headers
    - src/lib/libnxweb.a  -- NXWEB static library to link your modules to.
                             See sample_config/modules/* for examples.
    - sample_config/nxweb_config.json -- sample config file.
    - sample_config/www   -- sample static html & images content.
    - sample_config/ssl/* -- sample SSL keys generated for you.

## 3. Install

You can install compiled binary, include, and library files to your system's
default location (or location specified by --prefix switch):

```sudo make install
```

To uninstall run:

```sudo make uninstall
```


## 4. Run sample configuration

  Change directory to ./sample_config then run:

```nxweb
```

nxweb shall pick nxweb_config.json file from current directory and behave accordingly.

## 5. Compiling and linking your own main.c & modules:

```gcc -O2 -g main.c modules/*.c -o mynxweb `pkg-config --cflags --libs nxweb`
```

pkg-config will search for NXWEB libraries at system's default locations if not custom configured.

## 5a. Alternatively you can compile your modules into shared library:

```gcc -shared -fPIC modules/*.c -o mymodules.so `pkg-config --cflags nxweb`
```

then load it via nxweb_config.json or via command line switch -L.
