# Check verdict correction

Completed. Contracts are harvested into `docs/arch/architecture.md`,
`docs/arch/adapter_contract.md` and `docs/ref/release_packaging.md`.

Authorized scope: make `--check` reject missing bundled samples and failed MDF4
openings while preserving interactive diagnostic tabs and recoverable diagnostics.
The owner is Explorer; parser behavior and the plot stack stay unchanged.

- Declare the expected sample filename in each built-in format entry. The default
  check opens those paths, including missing ones, in the selected sample directory.
  The first existing directory owns the bundle; a broken bundle cannot borrow files
  from a fallback directory. The sidebar still lists the available supported files.
- Add an opening error to `LoadResult` for a retained diagnostic session. MDF4 derives
  it from its reader's existing readiness and diagnostic. Pass it with `fileLoaded`
  to `OpenSequence`; a diagnostic tab is retained but counts as a failed opening.
- Prove the behavior through the real executable: the complete bundle and an explicit
  valid MDF4 pass, each missing sample fails, corrupt MDF4 fails, and explicit missing
  MDF4/A2L files fail. A controller regression retains the failed MDF4 diagnostic tab,
  continues to valid inputs, and accepts the DBC sample's recoverable diagnostic.

Run the new tests before and after the source correction, then the affected controller,
format and MDF4 suites and the package gates on Windows and Linux. Keep builds and
probe files separate from other sessions. Harvest contracts and archive this plan when
the correction lands. No tag or release is part of this batch.

Evidence: the executable regression failed before the fix for each of the four
missing samples, corrupt MDF4 and explicitly missing MDF4; the controller regression
reported zero failures where two were expected. The complete-bundle, explicit-valid-MDF4
and missing-A2L controls behaved correctly. After the fix, the seven focused suites
passed on Windows MinGW (Debug) and Linux GCC (Release). Both real package gates passed
the complete bundle. The Windows gate failed for missing and corrupt MDF4, and the
extracted Linux package failed those cases and explicitly missing MDF4, each with
exit 1. No release was created.
