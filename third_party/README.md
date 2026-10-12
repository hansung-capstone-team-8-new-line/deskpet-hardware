# Vendored dependency

- nlohmann/json **v3.11.3**, single header `json.hpp`.
- Source: https://github.com/nlohmann/json/tree/v3.11.3
- License: MIT, preserved in `JSON-LICENSE.MIT` and in the header.

SDL2, SDL2_ttf, ALSA and libgpiod are system dependencies, not vendored.
The JetPack 6 build targets libgpiod 1.6.x explicitly; libgpiod 2 uses a different API.
