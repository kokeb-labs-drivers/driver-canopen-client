---
name: datasheet-conformance
description: Check driver code against the chip's datasheet, citing sections, test first. Use when writing or reviewing register maps, timing, reset values or error handling.
---

# Datasheet conformance

The datasheet: not linked yet. Fill in `docs/datasheets.yml` (vendor, number, revision, date, `url`,
`pdf`, `sha256` and the topics); never commit the vendor PDF.

`docs/datasheets.yml` is the record of that source (handbook ADR-0019): fill in the document (vendor, number,
revision, date, `url`, `pdf` as a stable direct link or `null`, and the `sha256` of the PDF you used; never commit
the PDF) and one topic per thing the driver takes from it (section, printed page, driver API, docs pages). A driver
made from the template starts with the fictional CANOPEN_CLIENT placeholder there: replace all of it. Then run
`kokeb-docs gen`, which rebuilds the README card, the Datasheet map and the box on each page.

`docs/hardware.yml` is the same for the chip's hardware: the package, the pins, the hooks, the blocks, the
circuit, and the package facts the README's chip card and the outline figure are drawn from. Take
`package.outline` (body length and width in millimetres, height, pitch) from the datasheet's package outline
drawing and `package.variants` (the other packages, with their part-number suffixes) from its ordering
information; give each its section and printed page, and leave a number out when the datasheet does not state it.
`kokeb-docs hardware --check` validates the file; `kokeb-docs figures && kokeb-docs gen` redraw from it.

Catalogue ids (RD-nn) are in [`docs/recurring-defects.md`](../../../docs/recurring-defects.md); the policy for where each
class is caught is in [`docs/mechanization-policy.md`](../../../docs/mechanization-policy.md).

Every number in the driver has a source. Work one subsystem at a time (reset, identification, one mode, one
register block). Datasheet misreads reached an independent reviewer on six drivers (RD-28), and a model written
by the author repeated the misreading in two (RD-07): no script finds these, so this skill is judgement work.

## Writing (test first)

1. **Collect the facts** into a table: register, address, bits, reset value, access (R, W, R/W, self-clearing,
   clear-on-read), timing (min, typ, max) and the section, table or figure. Waits use the maximum, pulses the minimum.
2. **Write the model first**, from the table, standing alone (it includes no driver header): reset values, read-only
   and self-clearing bits, clear-on-read flags, timing, invalid writes, and a golden frame copied from the datasheet's
   figure for every frame layout.
3. **Write the tests** that pin each fact: reset values after `EnsureInitialized()`, exact frame bytes, waits against
   the datasheet maximum, every documented error.
4. **Then change the driver.** Each access and wait carries its section (`// t_RST, section 8.2`); add the topic to
   `docs/datasheets.yml` and run `kokeb-docs gen`.
5. **Cross-check** `Config{}` against reset values, `Validate()` against documented ranges, every `Error` against a
   datasheet condition or a port failure.
6. **Mark what you could not verify** in the code comment, `docs/ambiguities.md` with a model knob for each reading, and
   "not verified on hardware" with a bench step. Never invent a register, bit or timing; doubt is moved to an
   ambiguity, never dropped.

## Review mode: the exact questions

Derive the facts from the datasheet first, before reading the code or the model, then compare.

1. For each register or frame the change touches (RD-28, RD-07): which bits, which position, which reset value? Does the
   code agree, and does a golden frame from the datasheet figure (not from the model) pin it?
2. Clear semantics (RD-31, RD-28): which bits clear on read, on write-1, on a new cycle, by themselves? Is each in the
   model, and does a diagnostic read still report the fault later?
3. Timing (RD-28): is every wait the maximum and every pulse the minimum, with each status flag on its own delay in the
   model? Is any wait missing that a pending timer in the chip needs (RD-10)?
4. Reset and power (RD-01): list every reset or reload path and the evidence it leaves; which function reads it?
5. Hardware docs (RD-29): for each net of the reference circuit, the highest pin voltage against the absolute maximum,
   and every wiring or threshold claim against the datasheet table (documents that disagree are an ambiguity with a knob).
6. Completeness: is every register, mode and option exposed, guarded with a reason, or out of scope with a reason in
   the coverage table (RD-08)? Sample rows and check the named symbol and test exist and the test asserts the row.
7. Independence (RD-07): does the model include a driver header, share a constant, or copy a decode from the driver?
   Would the model and the driver both decode the golden frame the other way and still pass?
8. Cited pages: spot-check five section and page references of `docs/datasheets.yml` against the PDF.

Already mechanical (do not redo): `docs/datasheets.yml` and card validity (`kokeb-docs check`), no PDF in the tree,
the model-includes-no-driver-header lint when it lands, Doxygen symbol existence, standalone naming.

## Report

Review-round format: `BLOCKER` (a wrong value returned as data, a wrong field, a clear that releases a stop),
`MAJOR` (wrong timing, a missing documented error, a false coverage row), `minor` (wording, citation). Each with
`file:line`, the datasheet section and printed page, the failing scenario (what the chip would do) and the fix with
the test that proves it. List documented ambiguities (each with its knob) and what only a bench can confirm. End with
one line: `NO BLOCKER` or `BLOCKERS REMAIN`.

Write what the bench taught as the driver's design: the behaviour, the datasheet section that explains it, and the
test that pins it. The repository describes this chip only: name no other board, product, project or driver, and keep
dated history out of code, docs and CHANGELOG entries (`tools/check_standalone.py`).
