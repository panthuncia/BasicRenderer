# BRNifly Licensing Boundary

BRNifly is intended to be built and distributed as a separate process from BasicRenderer and CLodCacheTool.

This executable may dynamically load and interact with GPL-licensed niflyDLL/PyNifly-compatible components. Do not link BasicRenderer, CLodCacheTool, BasicScene, BasicRHI, or OpenRenderGraph directly against niflyDLL, PyNifly, or GPL-derived wrapper code.

The IPC protocol exposed by BRNifly is a neutral data protocol for service discovery and asset conversion. Renderer-side clients should depend only on that protocol and on the returned data payloads.
