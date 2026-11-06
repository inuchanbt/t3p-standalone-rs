# Third-party notices and research inputs

- `vendor/kws-x1/` contains two files from kralonur/kws-x1-standalone-rs at commit `6942fc6052ac38e7afbd7d31a22ee8789f55c29a`, distributed under the MIT option. The upstream copyright and license are retained in `vendor/kws-x1/LICENSE-MIT`. The constructors were made const for static initialization.
- T3P startup, board and layout-audit sources come from the local DOOM on T3P research workspace. Their project license is GPL-2.0-or-later. Board register operations were researched using the stock application and JieLi SDK. The LCD sequence is generated locally from the user's own stock application.
- The board build uses external Rust, Pi32v2 LLVM and JieLi tools. The simulator uses pi32v2-isa. Each has its own license and is obtained separately.
- `pack_demo.py` uses the separate local DOOM on T3P workspace and its jl-misctools subset. Consult that workspace's third-party notices and pinned revisions when packaging.

Manufacturer firmware, generated update images, device data, official SDK files and toolchain binaries are not included. Review their applicable terms and corresponding-source requirements before redistributing a binary built using them.
