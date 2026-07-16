package buildinfo

// Version is set at link time via -ldflags, e.g. make VERSION=1.2.3 arm64.
var Version = "dev"
