# Packaging notices and sample staging

Implemented. The current contracts and tests are in release_packaging.md and
cmake_build_system.md. No release or UI change is part of this packet.

Close BL-K7 and BL-K8 without changing application UI or publishing a release.

## Linux notices

Deploy the full AppDir first, generate THIRD_PARTY_NOTICES.txt plus the copied
license texts from its actual bundled shared libraries/plugins, then produce the
AppImage. Attribute Qt-provided files to the installed QT_PREFIX and its module
notices/SBOMs, and distribution files to their installed package and copyright
file. Include Qt's bundled dependencies such as ICU, with actual notices, not an
assumption that every Qt-prefix binary has Qt's license. Follow the existing
Windows package's provenance approach where applicable. Do not hardcode a list
of versioned libraries. Deployment can change RPATHs, so file comparison must
account for that without accepting an unrelated same-basename file. Unknown
origin or missing license material fails packaging with the offending path.

Use only tools available in the existing srv-one build image, or raise a concrete
missing input to the lead. Keep package/source paths with spaces safe. Generate
stable attribution ordering, avoid duplicating shared license texts, and verify
the final AppDir/AppImage still covers every bundled shared-library payload.
No changes to Windows dependency policy or release/publication behavior.

## Build-tree samples

Replace the executable's POST_BUILD-only copy with a dependency-aware staging
step. Building the executable after changing a sample must refresh the staged
copy even when no source needs recompiling or relinking. Stage the same complete
sample set and preserve the current runtime location. Account for generator
configuration/output paths; do not solve this with a forced executable relink.

## Proof

Capture the stale-sample reproduction before the fix in a private build/rehearsal
tree, then prove the sample changes while the executable stays untouched and
the subsequent no-change build is stable. For notices, verify real package
attribution, a deliberately unknown payload fails, and the extracted AppImage
contains nonempty notices/license files matching its shared libraries. Use
isolated fixtures for script edge cases; never alter the system package database.
Sol reviews scripts/tests; lead owns the shared srv-one build/package/smoke run.
Update the packaging/build contracts and backlog, then archive this plan.
