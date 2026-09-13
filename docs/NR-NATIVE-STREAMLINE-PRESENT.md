# Finished-picture NR with native Streamline FG

During native DLSSG, Streamline's display buffers differ from the game's finished buffers. NR edits underneath the interposer can be overwritten by its later copy. See NVIDIA's [interposer](https://github.com/NVIDIA-RTX/Streamline/blob/main/source/core/sl.interposer/dxgi/dxgiSwapchain.cpp) and [hook ABI](https://github.com/NVIDIA-RTX/Streamline/blob/main/source/core/sl.api/internal.h).

NR wraps the native DLSSG plugin's HWND creation and Present/Present1 callbacks. Creation records the original game queue. Before DLSSG presents, NR retrieves the app-facing index/buffer through that plugin's hooks and processes it on the game queue. Declined hooks skip NR without substituting a display buffer. Test presents and disabled NR do no work; OptiScaler's local DLSSG hook remains unchanged.

Native handoff selects matching submitted input by queue order/completion, rather than the inner display counter, which includes generated frames. Other routes retain their epoch check. Unfinished cross-queue input cannot add a presentation wait: that previously caused a Cyberpunk freeze.

`tests/nr_streamline_picture_smoke.cpp` rotates app buffers on WARP, demonstrates overwritten display edits versus surviving app edits, and rejects declined hooks. This simulates the FG copy; it does not load NVIDIA's model or establish interpolation quality. Coverage is native DX12 HWND chains; see [game observations](NR-UPSTREAM-REVIEW.md).
