#pragma once

// Firmware semantic version. CI injects the real value from the release tag via
// -DFW_VERSION (see platformio.ini + the release-build workflow); this fallback
// marks a local/dev build. Clients read it from getspec / /api/spec and gate
// compatibility on the major.minor pair (desktop and firmware must match minor).
#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"
#endif
