# Credits and third-party code

The direct foundation is [MYSOREZ/GeneralsZH-Android-Port](https://github.com/MYSOREZ/GeneralsZH-Android-Port), currently based on v1.4.0. This fork keeps the original history and copyright notices.

The Quest interaction grew from an earlier offline Quest implementation based on [Gonzorro/GeneralsVR](https://github.com/Gonzorro/GeneralsVR). Only its sealed offline v0.44 snapshot was reused. Online code comes from the MYSOREZ foundation, not later versions of that earlier Quest project.

Thanks to [EA](https://github.com/electronicarts/CnC_Generals_Zero_Hour), [TheSuperHackers](https://github.com/TheSuperHackers/GeneralsGameCode), [fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX), [Fighter19](https://github.com/Fighter19/CnC_Generals_Zero_Hour), [jmarshall2323](https://github.com/jmarshall2323/CnC_Generals_Zero_Hour), and the [GeneralsOnline development team](https://github.com/GeneralsOnlineDevelopmentTeam) for the engine, platform and multiplayer work behind this port.

Terrain batching was informed by [Cesarus85/Generals-Zero-Hour-XR](https://github.com/Cesarus85/Generals-Zero-Hour-XR).

## Runtime and source dependencies

| Project | Use | License / notices |
| --- | --- | --- |
| [DXVK](https://github.com/doitsujin/dxvk), [fbraz3 fork](https://github.com/fbraz3/dxvk) | Direct3D 8/9 to Vulkan, Quest interop patches | zlib; pinned submodule LICENSE |
| [SDL3 and SDL_image](https://github.com/libsdl-org) | Android window, input and images | zlib |
| [Khronos OpenXR](https://github.com/KhronosGroup/OpenXR-SDK-Source) | Headset, controller and stereo presentation | Apache-2.0 |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | Audio | LGPL-2.0-or-later |
| [FFmpeg](https://ffmpeg.org/) | Audio and video decoding | LGPL; exact build configuration and notices accompany releases |
| [Valve GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) | Upstream multiplayer transport | BSD-3-Clause |
| [GameSpy SDK / TheAssemblyArmada](https://github.com/TheAssemblyArmada/GamespySDK) | Legacy engine integration | Retained SDK copyright and license notices |
| [TheSuperHackers lzhl](https://github.com/TheSuperHackers/lzhl-1.0) | Compression | Retained source license notices |
| [FreeType](https://freetype.org/) | Font rendering | FreeType license / GPL alternative |
| [Liberation Fonts](https://github.com/liberationfonts/liberation-fonts) | UI fonts | SIL Open Font License 1.1 |
| [libadrenotools and liblinkernsbypass](https://github.com/bylaws/libadrenotools) | Inherited Android Vulkan loader integration | BSD-2-Clause; retained source notices |
| [SMAA](https://github.com/iryoku/smaa) | Retained shader and lookup sources; no launcher option | MIT; [license](GeneralsMD/Code/Main/QuestVR/SMAA/LICENSE.txt) |
| [curl](https://curl.se/), [OpenSSL](https://www.openssl.org/) | Upstream HTTP and TLS | curl license; Apache-2.0 |
| [Protocol Buffers](https://github.com/protocolbuffers/protobuf), [Abseil](https://github.com/abseil/abseil-cpp), [utf8_range](https://github.com/protocolbuffers/utf8_range) | Upstream transport dependencies | BSD-3-Clause / Apache-2.0 / MIT |
| [zlib](https://zlib.net/), [libpng](https://www.libpng.org/pub/png/libpng.html), [Brotli](https://github.com/google/brotli), [bzip2](https://sourceware.org/bzip2/) | Image and compression dependencies | zlib / libpng / MIT / bzip2 licenses |
| [GLM](https://github.com/g-truc/glm), [GLI](https://github.com/g-truc/gli) | Graphics support | Retained MIT and Happy Bunny license alternatives |
| [Android NDK LLVM/libc++](https://developer.android.com/ndk), [AndroidX](https://developer.android.com/jetpack/androidx), [Material Components](https://github.com/material-components/material-components-android) | Native runtime and launcher | Apache-2.0; LLVM exception where applicable |

Inherited platform sources also include [ANGLE](https://github.com/google/angle) and DirectX/Vulkan/SPIR-V headers. Keep their notices when building those targets; ANGLE is not the Quest rendering backend.

Build tools include [CMake](https://cmake.org/), [Ninja](https://ninja-build.org/), [Meson](https://mesonbuild.com/), [vcpkg](https://github.com/microsoft/vcpkg), Gradle, and the optional [Eclipse Java compiler](https://eclipse.dev/jdt/). Build tools are separate from the Android runtime.

The engine remains under [GPLv3 with EA's additional terms](LICENSE.md). No retail game assets are supplied. Dependency licenses and corresponding pinned sources must accompany any binary release; this credits page does not replace them.
