# Native Bloop

The C++ renderer preserves the XML scene format and CLI shape of `bloop.py`. It uses CMake to fetch TinyXML-2 11.0.0 and a pinned LodePNG commit during the first configure, so initial setup requires network access.

## Build and test

```powershell
cmake -S native -B ../bloop-native-build
cmake --build ../bloop-native-build --config Release
ctest --test-dir ../bloop-native-build -C Release --output-on-failure
```

Run `..\bloop-native-build\Release\bloop_native.exe` on Windows, or the `bloop_native` executable produced by the selected generator on other platforms.

```text
bloop_native -i scene.xml -o output.png -g width,height[,xOffset,yOffset] [-s samples] name value [...]
```

## Expressions

Expressions support numeric and hexadecimal literals, variable names, parentheses, unary `+` and `-`, and binary `+`, `-`, `*`, and `/`. Division produces a floating-point result. Child objects can refer to inherited parameters, and their own resolved parameters shadow inherited names for descendants. Python function calls and other `eval` syntax are intentionally unsupported.