nf7
====

portable visual programming platform

## REQUIREMENTS

- **OS**: Windows 10, Linux, and Chrome OS
- **CPU**: x86_64
- **GPU**: OpenGL 3.3 or higher (except Intel HD Graphics)
- **RAM**: 512 MB or more (depends on what you do)
- **Storage**: 100 MB or more (depends on what you do)

## INSTALLING

Build Nf7 by yourself, or download the binary from releases (unavailable on mirror repo).

It's expected to copy and put the executable on each of your projects to prevent old works from corruption by Nf7's update.

## BUILDING

### Succeeded Environments

- Windows 10 / CMake 3.20.21032501-MSVC_2 / cl 19.29.30137
- Arch Linux / CMake 3.24.2 / g++ 12.2.0
- Ubuntu (Chrome OS) / CMake 3.18.4 / g++ 10.2.1

### Windows

```
PS> mkdir build
PS> cd build
PS> cmake ..  # add build options before the double dots
PS> cmake --build . --config Release
```

### Linux

```
$ mkdir build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Release ..  # add build options before the double dots
$ make
```

### CMake build options
|name|default|description|
|--|--|--|
| `NF7_STATIC` | `ON` | links all libraries statically |
| `NF7_SANITIZE_THREAD` | `OFF` | enables thread-sanitizer (g++ only) |
| `NF7_SANITIZE` | `OFF` | enables address/undefined/leak sanitizers (g++ only) |
| `NF7_PROFILE` | `OFF` | enables Tracy features |

The following condition must be met:
```
!(NF7_SANITIZE_THREAD && NF7_SANITIZE) &&  // address-sanitizer cannot be with thread-sanitizer
!(NF7_SANITIZE_THREAD && NF7_PROFILE)      // TracyClient causes error because of thread-sanitizer
```

## DEPENDENCIES

see `thirdparty/CMakeLists.txt`

## LICENSE

Do What The Fuck You Want To Public License v2  
*-- expression has nothing without the real free --*
