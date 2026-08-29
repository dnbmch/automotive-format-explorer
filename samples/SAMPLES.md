# Bundled sample files

Shipped with the application so the tree, memory map, signal map, and
diagnostics views can be explored without hunting for real files. The app lists
them under "open a sample" when no file is open.

| File | Format | Provenance | License |
|---|---|---|---|
| `demo_ecu.a2l` | ASAP2 (A2L) | Hand-authored synthetic engine-ECU description, written for this project. Not derived from ASAM material. | Same as this repository (GPL-3.0) |
| `demo_seat.ldf` | LIN (LDF) | Hand-authored synthetic seat-control cluster, written for this project. Not derived from the LIN specification example. | Same as this repository (GPL-3.0) |
| `tesla_can.dbc` | CAN (DBC) | [comma.ai opendbc](https://github.com/commaai/opendbc) project, unmodified. | MIT (see below) |
| `demo_recording.mf4` | ASAM MDF 4.2 | Synthetic recording emitted by `dnbmch/mdf4-writer` (`emit_minimal_mf4`), written for this project. One channel group: a `t` time master and a `speed` sine — select `speed` to see the plot; `t` is the group's axis channel and does not plot. | Same as this repository (GPL-3.0) |

`tesla_can.dbc` deliberately carries one dangling `VAL_` entry (message ID 568
has no matching definition), so opening it shows the diagnostics badge with
exactly one DROPPED entry — that is the parser's data-loss reporting working,
not a defect in the app.

## opendbc license (tesla_can.dbc)

MIT License

Copyright (c) 2019, Comma.ai, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
