# Exposure calibration anchors

Calibration maps a scanned scalar to the codec's white point; candidates are not necessarily linear exposure.

One anchor uses `white = anchorWhite * (anchorScan / scanNow) * trim`; inversion swaps the ratio. Multiple anchors interpolate log-white against log-scan between sorted points, clamping outside the measured range.

Up to eight positive, plausible points are stored. A scan value within 2% of an existing point replaces it. Trim and the final divisor are bounded. Semicolon-separated `scan:white` pairs persist; malformed tokens are skipped and a legacy single anchor migrates to one entry.

`DlssNr_ExposureAnchors.cpp` owns the table and mutex, independently of scanner GPU resources. Discovery lives in `DlssNr_ExposureScan.cpp`; copies and ring ownership in `DlssNr_ExposureReadback.cpp`.
