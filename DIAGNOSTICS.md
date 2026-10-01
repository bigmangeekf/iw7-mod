# Script-link diagnostic fix

Both linker failure paths print the resolved missing script/function before invoking the existing Com_Error handler. Equivalent logging in a local dedicated fixture exposed missing Rhino dependencies instead of only ShutdownGame. This patch contains no game assets, private settings, voice code, or cross-map changes.

The exact fork patch still requires CI compilation; draft PR1 tracks it. Runtime acceptance of the separate Rhino experiment is not implied by a successful diagnostics build.
