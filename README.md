# RaealPF
Geometry Dash **2.2081** / Geode **5.10.1** mod. The previous pathfinder has been removed while a replacement is in development.

Press a level's play button to open RaealPF. **Play → Play** starts the level normally. The **Pathfind** tab is currently unavailable; close the window with **X** or Escape.

<img src="logo.png" width="150" alt="the mod's logo" />


## Build and automated tests

Native builds require the [Geode SDK and CLI](https://docs.geode-sdk.org/getting-started/create-mod#build):

```sh
geode build
```

For the existing Windows cross-build in this checkout:

```sh
cmake -S . -B build-win -DGEODE_DONT_INSTALL_MODS=ON -DGEODE_DONT_UPDATE_INDEX=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-win -j 4
```

The packaged mod is `build-win/cynthlodia.raealpf.geode`. Geode CLI 4 no longer accepts the SDK's optional `--dont-update-index` argument; leave `GEODE_DONT_UPDATE_INDEX` off with this SDK/CLI combination.

Standalone GDR2 codec tests require CMake and a C++23 compiler, but no game, SDK, installation changes, or saves:

```sh
cmake -S . -B build-core -DRAEALPF_STANDALONE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core -j 4
ctest --test-dir build-core --output-on-failure
```

The codec is retained as standalone code; it is not currently connected to the interface.


## References

- [APT](https://github.com/aliakigreque/APT): native-version pathfinding reference; its implementation is not copied or vendored.
- [Prism Menu](https://github.com/FireMario211/Prism-Menu): replay/UI reference; its implementation is not copied or vendored.

# Resources
* [Geode SDK Documentation](https://docs.geode-sdk.org/)
* [Geode SDK Source Code](https://github.com/geode-sdk/geode/)
* [Geode CLI](https://github.com/geode-sdk/cli)
* [Bindings](https://github.com/geode-sdk/bindings/)
* [Dev Tools](https://github.com/geode-sdk/DevTools)
