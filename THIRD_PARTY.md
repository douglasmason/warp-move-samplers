# Third-party components

The source package itself does not redistribute Bungee or its submodules. `scripts/fetch_bungee.sh` retrieves the pinned upstream source.

- Bungee: https://github.com/bungee-audio-stretch/bungee — MPL-2.0
- Eigen, pffft: pulled as Bungee submodules under their respective licenses.

The Bungee build recipe is adapted from the public `charlesvestal/schwung-stretch` module build process.

## nlohmann/json

Version 3.11.3, commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`.
MIT license: `third_party/JSON-LICENSE.MIT`. Used only by the background worker
for state parsing and serialization. `scripts/fetch_json.sh` verifies SHA-256
`9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`.
