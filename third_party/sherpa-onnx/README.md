# sherpa-onnx C API header (third-party, Apache-2.0)

`sherpa-onnx/c-api/c-api.h` is copied unchanged from sherpa-onnx **v2.0.1**
(https://github.com/k2-fsa/sherpa-onnx, tag v2.0.1; licence in `LICENSE`).

Only the header is kept here, so shadowgate.exe is always built able to use
the narrator voice. The voice itself is an optional download (the "voice
package"): sherpa-onnx's Windows DLLs (`sherpa-onnx-v2.0.1-win-x64-shared-MT-Release-lib`)
and the Kokoro model. `src/sg_voice.c` loads the DLL while the game runs; if
it is not there, the game simply runs without narration.

The header and the DLLs must be the same version (2.0.1): the structures are
passed between them as-is.
