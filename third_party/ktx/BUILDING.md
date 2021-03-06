
Building KTX
============

This document describes how to build the KTX library `libktx`, the
portable KTX loader tests `ktxtests` and the KTX tools `ktxtools`.

Status
------

You can build `libktx` and `ktxtests`for GNU/Linux, iOS, macOS and
Windows. There are three versions of the tests for the
OpenGL<sup>&reg;</sup> loader: GL3, ES1 and ES3. There are also tests
for the Vulkan<sup>&reg;</sup> loader: VK. GL3 can be built on GNU/Linux,
macOS and Windows. ES1 can be built on iOS and with the PowerVR emulator
on Windows. ES3 can be built on iOS and with any of the major emulators
on Windows. VK can be built on GNU/Linux, iOS, macOS and Windows after
installing a Vulkan SDK.

Android builds will follow.

You can build `ktxtools` for GNU/Linux, macOS and Windows.

Make/Solution/Project Files
---------------------------

The project includes separate cmake/make/project/solution files for the
KTX library, `libktx`, the KTX loader tests, `ktxtests`, and the KTX
tools, `ktxtools`. The last 2 have dependencies on the first.

All these make/solution/project files are generated by GYP and should
not be hand edited. Make changes to the GYP files and regenerate the
projects.

The current project files include both OpenGL and Vulkan loaders and
tests. The plan is to offer alternatives in future. 

Building
--------

### GNU/Linux

```bash
cd build/make/linux
# Build everything
make
# or
make -f [ktxtests|ktxtools|libktx].Makefile
```

**Note:** The generated makefiles cannot be used to build `vkloadtests`
due to a problem with the shader compile code generated by GYP. Use the
`cmake` files instead.

Alternatively you can use cmake:

```bash
cd build/cmake/linux/Debug # or .../Release
cmake .
make
```

You can create an Eclipse project with `cmake`.

```bash
cd build/cmake/linux/Debug # or .../Release
cmake . -G "Eclipse CDT4 - Unix Makefiles"
```

You can import this into Eclipse via "Import->Existing Project".

### iOS and macOS

Use the generated projects under `build/xcode/ios` to build the
library and load tests to run on iOS. The OpenGL tests will use
OpenGL ES 1.1 and OpenGL ES 3.0. The Vulkan tests will use MoltenVK.

Use the generated projects under `build/xcode/mac` to build the
library, load tests and tools to run on macOS. The OpenGL tests
will use OpenGL 3.3. The Vulkan tests will use the macOS Vulkan SDK
and MoltenVK.

To find the Vulkan validation layers when running the macOS Vulkan
load tests set the environment variable `VK_LAYER_PATH` to
`$(VULKAN_SDK)/macOS/etc/vulkan/explicit_layers.d` when running the
application. Environment variables are set in the Arguments section
of Xcode's Scheme Editor. Validation is only enabled by the debug
configuration.

#### Xcode Preferences

You must create the following Custom Path preferences in Xcode:
[`DEVELOPMENT_TEAM`](#development_team) & [`VULKAN_SDK`](#vulkan_sdk).
If building for macOS you must also set [`ASSIMP_HOME`](#assimp_home).

##### DEVELOPMENT_TEAM 
As of Xcode 8.0, Apple, in its wisdom, decided to require selection of a
Development Team for signing. If you set the Development Team via the
Xcode GUI, Xcode will store that selection in the `project.pbxproj` file.
Thereafter `git status` will show the project file as `modified`.
Instead abuse Xcode's _Custom Paths_ preference to store your
development team identifier outside the `project.pbxproj` file by
setting a DEVELOPMENT_TEAM custom path (replace ABCDEFGHIJ with your
team identifier):

![Image of Xcode Preferences open at the `Locations` tab with
Custom Paths selected](https://i.stack.imgur.com/WtGcY.png)

(Xcode menu ??? Preferences??? ??? Locations ??? Custom Paths)

Note: if you change anything in the project (update a build setting,
add a new build phase, rename a target etc.) the development team will be
automatically added to the project.pbxproj file. So, as noted above,
changes should be made only to the GYP files.

Thanks to [0xced@stackoverflow](http://stackoverflow.com/users/21698/0xced)
for this solution.

The team identifier, formally known as the Organizational Unit, an
alphanumeric code (in the case of the author's "personal team"), can be found
as follows:

- Open Keychain Access (it's in /Applications/Utilities)
- Select *login* in top left pane
- Select *My Certificates* in bottom left pane
- Right click the certificate of the team you wish to use; choose *Get Info*
  from the context menu.
- Click the triangle beside *Details* to expand it, if not already expanded
- Copy the value of the *Organizational Unit* field under *Subject Name*

If you have already manually selected your team in Xcode, you can find the
Organizational Unit and revert to the original project file by running
the following in the KTX project root:

```bash
git diff | grep DEVELOPMENT_TEAM
git checkout build/xcode
```

##### VULKAN_SDK

Set this to the location where you have installed the [Vulkan SDK for macOS](#vulkan-sdk).

##### ASSIMP_HOME

Set this to the location where [`libassimp`](#libassimp) is installed. If
you use the MacPorts version, this will be `/opt/local`.

### Windows

Use the solutions under one of
`build/msvs/{win32,x64}/vs20{10,10e,13,13e,15,17}` to build the library,
load tests and tools for Win32 or x64 plaforms. There are separate
solutions for Win32 and x64 platforms.

**Note:** Builds of the Vulkan loader tests require vs2015+ because they
use `vulkan.hpp` which needs C++11 & in particular `constexpr`, so solutions
other than vs2015 and vs2017 do not include a `vkloadtests` project
and their `appfwSDL` projects do not include Vulkan app support.

Dependencies
------------

The KTX library, `libktx`, and the KTX loader tests, `ktxtests`, use
the _GL Extension Wrangler_ (GLEW) library when built for
OpenGL on Windows.

The MSVS `ktxtests` solutions on Windows include OpenGL ES versions.
To build a complete solution and run the OpenGL ES versions you need to
install an OpenGL ES emulator.

The KTX loader tests in `ktxtests` use libSDL 2.0.8+. You do not
need SDL if you only wish to build `libktx` or `ktxtools`.

Binaries of these dependencies are included in the KTX Git repo.

The KTX vulkan loader tests in `ktxtests` require a [Vulkan SDK](#vulkan-sdk)
and the Open Asset Import Library [`libassimp`](#libassimp). You must install
the former. The KTX Git repo has binaries of the latter for iOS and Windows
but you must install it on GNU/Linux and macOS.

As noted above, the KTX project uses GYP to generate make, project and
solution files. *You do not need GYP unless you want to re-generate
the supplied projects or generate additional projects.*

### GL Extension Wrangler

Builds of GLEW are provided in the KTX Git repo.

#### Building GLEW from source

If you want to build GLEW from source you need the OpenGL core profile
friendly version, i.e, 1.13.0+. You can either clone
[the master GLEW repo](https://github.com/nigels-com/glew) and, following
the instructions there, generate the code then build it, or you
can download a pre-generated snapshot from https://glew.s3.amazonaws.com/index.html
and build that following the instructions also found in
[the master GLEW repo](https://github.com/nigels-com/glew).
The snapshot used for the binary included in this repo came from
https://glew.s3.amazonaws.com/index.html?prefix=nigels-com/glew/25/25.1/.

### OpenGL ES Emulator for Windows

The generated projects work with the
[Imagination Technologies PowerVR](https://community.imgtec.com/developers/powervr/graphics-sdk/).
emulator. Install that before trying to build on Windows.

Projects can be modified to work with any of the major emulators;
[Qualcomm Adreno](https://developer.qualcomm.com/software/adreno-gpu-sdk/tools),
[Google ANGLE](https://chromium.googlesource.com/angle/angle/)<sup>*</sup>,
[ARM Mali](http://malideveloper.arm.com/resources/tools/opengl-es-emulator/)
or [PowerVR](https://community.imgtec.com/developers/powervr/graphics-sdk/).
To use a different emulator change the selection at the bottom of
`gyp_include/config.gypi` and regenerate the projects. If you want to run
the load tests for OpenGL ES 1.1 you will need to use Imagination
Technologies' PowerVR emulator as that alone supports OpenGL ES 1.1.

<sup>*</sup>You will need to build ANGLE yourself and copy the libs
and dlls to the appropriate directories under `other_lib/win`. Note
that ANGLE's OpenGL ES 3 support is not yet complete.

### SDL

Builds of SDL are provided in the KTX Git repo. These binaries
were built from a post 2.0.8 changeset given below. This changeset
includes a fix for an issue with OpenGL applications on macOS Mojave.
Standard SDL 2.0.8 works fine on all other platforms so you can download
binaries from [libsdl.org](https://libsdl.org), if you prefer.

#### macOS Notes

If you wish to use the provided version of SDL in other applications
on your system, you can install the framework. Open a shell and enter
the following command

```bash
cp -R other_lib/mac/<configuration>/SDL2.framework /Library/Frameworks
```

replacing `<configuration>` with your choice of `Debug` or `Release`.
If you do this, you can modify the projects to use this installed
SDL framework instead of copying it into every application bundle.
See`gyp_include/config.gypi` for details. You will have to regenerate
the xcode project if you wish to do this.

#### Building SDL from source

As noted above, KTX uses a post SDL 2.0.8 changeset, no.
[12343](https://hg.libsdl.org/SDL/rev/84eaa0636bac) in the canonical
Mercurial repo at https://hg.libsdl.org/SDL or the automated GitHub
mirror at https://github.com/spurious/SDL-mirror. Clone the repo,
checkout changeset [12343](https://hg.libsdl.org/SDL/rev/84eaa0636bac)
and follow the SDL build instructions.

Copy the results of your build to the appropriate place under the
`other_lib` directory.

### Vulkan SDK

For GNU/Linux install the Vulkan SDK using the `.tar.gz` file from
[LunarG](https://vulkan.lunarg.com/). Set the environment variable
`VULKAN_SDK` as instructed by LunarG.

You will need to build `glslc` whose binary is not included in the
SDK. To do this:

```bash
cd $VULKAN_SDK
./build_tools.sh --shaderc
```

It takes a while.  10 minutes or more! Add a comment to [issue 671
at LunarG](https://vulkan.lunarg.com/issue/view/58e4e57be46ffe7e73becd83)
to apply pressure on them to include this binary.

For Ubuntu Xenial (16.04) & Bionic (18.04) you can install the
Vulkan SDK from the Ubuntu distribution. Follow the instructions
give at [LunarG](https://vulkan.lunarg.com/). The `glslc` binary
is included hence use of this distribution is highly recommended.

For Windows install the Vulkan SDK for Windows from
[LunarG](https://vulkan.lunarg.com/). Set the environment variable
`VULKAN_SDK` as instructed by LunarG.

For iOS and macOS, install the Vulkan SDK for macOS from
[LunarG](https://vulkan.lunarg.com/). Set a `VULKAN_SDK` Custom
Path in the Xcode preferences to point to the `vulkansdk-macos-*`
folder you extracted from the download. This SDK contains MoltenVK
for both iOS and macOS.

### libassimp

Binaries for iOS and Windows are provided in the KTX Git repo.

#### GNU/Linux

Install from your package manager. For example on Ubuntu

```bash
sudo apt-get install libassimp3v5
```

macOS

Install via [MacPorts](https://www.macports.org/) or
[Homebrew](https://brew.sh/). For example

```bash
sudo port install assimp
```

Set an `ASSIMP_HOME` Custom Path in the Xcode preferences to the
parent of the `include` and `lib` folders where `libassimp` is
installed. For MacPorts this is `/opt/local`.

### GYP

All the builds use cmake, make or project files generated with
[GYP](https://gyp.gsrc.io/). A modified version, available in the
`remaster` branch of [this
fork](https://github.com/msc-/gyp/tree/remaster), is needed to
generate makefiles, vs2013+ & Xcode projects. To install GYP, follow the
[instructions in the
fork](https://github.com/msc-/gyp/tree/remaster#installing-gyp).
These work for either version of GYP. There are no install instructions
at GYP's home.

*You do not need GYP unless you want to re-generate the supplied projects
or generate additional projects.*

*You can use the standard version of GYP if you do not need to generate
make, Visual Studio 2013 or later or Xcode projects.*

### GNU make 3.81+

You need this to run the top-level `GNUmakefile` which runs GYP to generate the
various projects. It is possible to type the GYP commands manually, if you
really do not want to install GNU `make`. However, if you want to have GYP
generate makefiles to build the KTX project, you will need GNU `make` and
a Unix-style shell to run them.

On Linux, GNU make is available through the standard package managers in
most distributions. A suitable shell is standard.

On OS X, GNU make is included in the Xcode Tools available from
[developer.apple.com](http://developer.apple.com/tools/download/).
A suitable shell is standard.

On Windows, if you do not intend to generate makefiles to build KTX, you
can install a native Windows version of GNU make from
[GnuWin32](http://gnuwin32.sourceforge.net/packages/make.htm) and run
`make` in a Command Prompt (`cmd.exe`) window.

To get a Unix-like shell choose one of the following:

* install [Git for Windows](https://msysgit.github.io/) a.k.a `msysgit`
* install [GitHub for Windows](https://windows.github.com/)
* install [Cygwin](https://www.cygwin.com/) making sure to include `make` from
the *development* section.

The first two of these options include a copy of [MinGW](http://www.mingw.org/)
(Minimalist GNU for Windows). Sadly it is not the *same* copy; installing both
tools results in two copies of MinGW on your system. Neither copy includes
GNU `make`. You can download a pre-compiled version from the
MinGW project [32-bit](http://sourceforge.net/projects/mingw/files/MinGW/Extension/make/make-3.82.90-cvs/make-3.82.90-2-mingw32-cvs-20120902-bin.tar.lzma/download) or
[64-bit](http://sourceforge.net/projects/mingw-w64/files/External%20binary%20packages%20%28Win64%20hosted%29/make/make-3.82.90-20111115.zip/download).
Unpack the archive and you'll find a file called `mingw32-make.exe` (32-bit) or `make` (64-bit).

If using the Git for Windows shell (*Git Bash*), copy this to either

`%SystemDrive%\Program Files (x86)\Git\usr\bin\make.exe` (Windows 8.1 and 10)

(omit ` (x86)` if using 64-bit) or

`%USERPROFILE%\AppData\Local\Programs\Git\usr\bin\make.exe` (Windows 7)

:confused: I do not know if the difference in OS caused the different install locations
or if something else is at play.

If using the GitHub for Windows shell (*Git Shell*) copy this to

`%USERPROFILE%\AppData\Local\GitHub\PortableGit*\usr\bin\make.exe`

### Doxygen

You need this if you want to generate the _libktx_ and _ktxtools_
documentation. You need a minimum of version 1.8.14 to generate
the documentation correctly. You can download binaries and
also find instructions for building it from source at [Doxygen
downloads](http://www.stack.nl/~dimitri/doxygen/download.html). Make
sure the directory containing the `doxygen` executable is in your `$PATH`.


Generating Projects
-------------------

To (re-)generate the projects run the following commands in a shell:

```bash
cd <your KTX clone>
make [cmake|make|msvs|xcode]
```

All important configuration options are gathered together in the file
`gyp_include/config.gypi`. Change these as necessary to suit your local
set up.


{# vim: set ai ts=4 sts=4 sw=2 expandtab textwidth=75:}
