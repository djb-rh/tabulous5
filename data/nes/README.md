# Test ROMs

**These files are not in the repository.** `data/roms/*.nes` is gitignored: they
are other people's work, and while all three are freely downloadable, only
*Chase* carries an explicit public-domain notice (it prints "PD2012 SHIRU" on
its own title screen). Redistributing the others from an MIT repo would be
claiming a right nobody granted. Download your own copies.

All three are by Shiru, from <https://shiru.untergrund.net/software.shtml>,
and all three are mapper 0 (NROM) — the simplest kind, and the one to reach for
when something is broken and you need a ROM that is not the suspect.

| File | Source | Size | Mapper |
|---|---|---|---|
| `Alter_Ego.nes` | `shiru.untergrund.net/files/nes/alter_ego.zip` | 40 KB | 0 (NROM) |
| `Chase.nes` | `shiru.untergrund.net/files/nes/chase.zip` | 24 KB | 0 (NROM) |
| `Lan_Master.nes` | `shiru.untergrund.net/files/nes/lan_master.zip` | 40 KB | 0 (NROM) |

Put any `.nes` file here and run `pio run -e tab5 -t uploadfs` to get it onto
the device. The emulator lists whatever it finds.

Supported mappers are whatever `third_party/agnes` supports: NROM, UxROM, MMC1
and MMC3. A ROM using anything else will be rejected at load rather than
crash — check byte 6-7 of the iNES header if one is refused.

Emulation is currently **silent**: agnes has no APU.
