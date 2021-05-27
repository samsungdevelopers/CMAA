<!--
- Copyright (c) 2019-2020, Arm Limited and Contributors
-
- SPDX-License-Identifier: Apache-2.0
-
- Licensed under the Apache License, Version 2.0 the "License";
- you may not use this file except in compliance with the License.
- You may obtain a copy of the License at
-
-     http://www.apache.org/licenses/LICENSE-2.0
-
- Unless required by applicable law or agreed to in writing, software
- distributed under the License is distributed on an "AS IS" BASIS,
- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
- See the License for the specific language governing permissions and
- limitations under the License.
-
-->

## Build

# Android

## Dependencies

For all dependencies set the following environment variables.

- CMake v3.10+
- JDK 8+ `JAVA_HOME=<SYSTEM_DIR>/java`
- Android NDK r18+ `ANDROID_NDK_ROOT=<WORK_DIR>/android-ndk`
- Android SDK `ANDROID_HOME=<WORK_DIR>/android-sdk`
- Gradle 5+ `GRADLE_HOME=<WORK_DIR>/gradle`


## Build with Gradle

`Step 1.` Generate the gradle project using the internal script by running the following command

##### Windows <!-- omit in toc -->

```
bldsys\scripts\generate_android_gradle.bat
```

##### Linux <!-- omit in toc -->

```
./bldsys/scripts/generate_android_gradle.sh
```

A new folder will be created in the root directory at `build\android_gradle`

`Step 2.` Build the project

```
cd build/android_gradle
gradle assembleDebug
```

`Step 3.` You can now run the apk on a connected device

```
adb install build/outputs/apk/debug/vulkan_samples-debug.apk
```

> Alternatively, you may import the `build/android_gradle` folder in Android Studio and run the project from here

If you are using a newer version of cmake then 3.13, you might get this error:

> Execution failed for task ':externalNativeBuildDebug'.
Expected output file at \<PATH> for target \<sample> but there was none

In this case, update the version of the gradle plugin in "bldsys/cmake/template/gradle/build.gradle.in" to 3.5.0, remove the content of build folder and repeat the build process from Step 1. This is known to work with Gradle 6.3 and NDK 20.0.55

If you are using Android Studio, you can simply do these changes after importing the `build/android_gradle` folder, opening File->Project Structure, and doing the following changes:
On the Project tab, change the Android Gradle Plugin version to 3.5.0 and the Gradle version to 6.3.(this also requires NDK 20.0.55)

## License

See [LICENSE](LICENSE).

This project has some third-party dependencies, each of which may have independent licensing:

- [astc-encoder](https://github.com/ARM-software/astc-encoder): ASTC Evaluation Codec
- [CTPL](https://github.com/vit-vit/CTPL): Thread Pool Library
- [docopt](https://github.com/docopt/docopt.cpp): A C++11 port of the Python argument parsing library
- [glfw](https://github.com/glfw/glfw): A multi-platform library for OpenGL, OpenGL ES, Vulkan, window and input
- [glm](https://github.com/g-truc/glm): OpenGL Mathematics
- [glslang](https://github.com/KhronosGroup/glslang): Shader front end and validator
- [dear imgui](https://github.com/ocornut/imgui): Immediate Mode Graphical User Interface
  - [dear imgui shaders](https://github.com/SaschaWillems/Vulkan/tree/master/data/shaders/imgui): GLSL shaders for dear imgui
- [HWCPipe](https://github.com/ARM-software/HWCPipe): Interface to mobile Hardware Counters
- [KTX-Software](https://github.com/KhronosGroup/KTX-Software): Khronos Texture Library and Tools
- [spdlog](https://github.com/gabime/spdlog): Fast C++ logging library
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross): Parses and converts SPIR-V to other shader languages
- [stb](https://github.com/nothings/stb): Single-file public domain (or MIT licensed) libraries
- [tinygltf](https://github.com/syoyo/tinygltf): Header only C++11 glTF 2.0 file parser
- [nlohmann json](https://github.com/nlohmann/json): C++ JSON Library (included by [tinygltf](https://github.com/syoyo/tinygltf))
- [vma](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator): Vulkan Memory Allocator
- [volk](https://github.com/zeux/volk): Meta loader for Vulkan API
- [vulkan](https://github.com/KhronosGroup/Vulkan-Docs): Sources for the formal documentation of the Vulkan API

This project uses assets from [vulkan-samples-assets](https://github.com/KhronosGroup/Vulkan-Samples-Assets). Each one has its own license.

### Trademarks

Vulkan is a registered trademark of the Khronos Group Inc.
