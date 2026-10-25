# Save format

[简体中文](SAVE_FORMAT.md) · [English README](../README.en.md)

`src/save.cpp` and `include/model.h` are the authoritative implementation.

- Storage is 128 KiB FLASH, divided into eight 16 KiB slots.
- The current normal-record version is 17. In the current implementation, version 1 also identifies the format containing complete campaign information; payload length distinguishes it.
- A current record header contains the magic value, version, payload byte count, sequence, and checksum, followed by `Campaign`.
- FNV checks detect corruption during loading. They do not establish atomic writes or power-loss safety.
- Historical record conversion is implemented in `save.cpp`. Changes to `Campaign` require review of version conversion and size compatibility.

Back up `.sav` files before changing save-related code. Check loading old records, writing new records, switching among all eight slots, and overwriting. Do not change struct member order while retaining an incompatible old version number. Existing host/model tests do not perform real cartridge FLASH writes.

The English edition uses separate save identifiers; see the [English edition guide](ENGLISH.md#separate-save-files).
