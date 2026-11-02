# PVE Deck

Proxmox VE client for Sailfish OS (4.5+), built with Silica.
See [PLAN.md](PLAN.md) for the design and milestones.

## Layout

- `core/` – platform-independent logic (static library, unit tested)
- `app/` – Sailfish application: `main.cpp`, QML, icons, translations
- `tests/` – Qt Test unit tests for `core/`
- `rpm/pvedeck.spec` – packaging

## Build

With the Sailfish SDK:

    sfdk config target=SailfishOS-4.5.0.16-aarch64
    sfdk build

Without the SDK, using Docker:

    ./scripts/docker-build.sh            # aarch64
    ./scripts/docker-build.sh armv7hl
    ./scripts/docker-build.sh i486

RPMs end up in `RPMS/`.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
