# Third-party dependencies

- JUCE 9.0.4 is fetched from the official `juce-framework/JUCE` repository and pinned to the 9.0.4 tag by CMake FetchContent. Review the current JUCE 9 licence at https://juce.com/legal/juce-9-licence/ before distributing binaries or source.
- CMake 3.22 or newer and Apple Xcode are used for macOS builds.
- Git LFS stores the supplied WAV/MP3 sources and generated FLAC banks.
- Rubber Band CLI (R3) and FFmpeg/ffprobe are required only to regenerate alternate-key audio. The official Rubber Band project is GPL-2.0-or-later and offers separate commercial licensing; this project invokes the command-line program and does not link or bundle it in STILL. Review the current terms before distributing the processing tool/script as a package: https://github.com/breakfastquay/rubberband.

No paid dependency is required for local development. JUCE licensing still applies to the plugin; Rubber Band and FFmpeg are not part of its runtime.
