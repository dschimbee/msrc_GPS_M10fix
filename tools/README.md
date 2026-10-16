# msrc_web.html – slim web GUI

A single HTML file to configure MSRC over USB, without installation. It uses Web Serial and needs
**Chrome or Edge**. The layout is made for a phone in portrait mode, it also works on a PC.

Open the file in Chrome (or host it, for example with GitHub Pages), tap *Verbinden* (connect) and select the MSRC port.
The interface is in German.

## Features
- *Lesen* reads the configuration, *Schreiben* writes it and checks it by reading it back; restart the RP2040 afterwards.
  The SPL06 is selectable as vario sensor.
- Serial log (*Log starten*), kept in memory (20 MB limit): *Leeren* (clear), *Kopieren* (copy), *Speichern* (save).
  Saved files are numbered, for example `msrc_log_001_2026-10-16.txt`.
- Fields that the firmware does not use are marked *nicht unterstützt*; fields that only work in some configurations
  are marked with *~* (see [docs/MSRC_config_fields_without_effect.md](../docs/MSRC_config_fields_without_effect.md)).

It does not replace MSRC Link for features that are not in the form.
