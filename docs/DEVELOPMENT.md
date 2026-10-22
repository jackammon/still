# Development workflow

- Commit source, scripts, tests, docs, CMake files, and presets as normal Git files. Track runtime audio with Git LFS.
- Preserve supplied sample bytes. Put generated audio in a separate directory with reproducible processing details and source hashes.
- Build and run CTest before marking a milestone validated.
- Add GitHub Actions after the first successful AU compile.
- Tag only releases that pass automated checks and are ready for performance use. A compile-only checkpoint is not performance-ready.
