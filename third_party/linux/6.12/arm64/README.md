# Vendored Linux Headers

This tuple was generated from upstream Linux sources.
Do not edit vendored files manually.

Regenerate with:

```sh
make vendor-linux-headers LINUX_VERSION=<version> LINUX_ARCH=<arch>
```

Surfaces:

- `uapi/include`: output from `make headers_install`.
- `kheaders/source`: copied non-generated Linux kernel source headers.
- `kheaders/generated`: copied generated Linux kernel build headers from O=.
