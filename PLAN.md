# PVE Deck – Proxmox VE client for Sailfish OS

Native Sailfish OS app (Silica UI) that lets you:

- Log in to one or more Proxmox VE servers/clusters, with 2FA (TOTP, Yubico OTP, recovery codes)
- See the status of all VMs (QEMU) and containers (LXC), grouped by node
- Start / shutdown / stop / reboot / reset / suspend / resume guests
- Open a shell: LXC console, node shell and VM serial console, plus a built-in SSH client for VMs
- Be logged in to several servers at the same time

Distribution: **OpenRepos** (and Chum). Package name `pvedeck`. There's no Harbour library allowlist to follow, but the app still runs under Sailjail, so it must declare its permissions in the `.desktop` file.

---

## 1. Technology choices

| Area | Choice | Why |
|---|---|---|
| Language | C++ (Qt 5.6) + QML | Sailfish OS 4.x/5.x ships Qt 5.6; Silica is QML-only |
| UI | Sailfish Silica | Native look, pulley menus, RemorsePopup for destructive actions |
| HTTP | `QNetworkAccessManager` | Built in, handles cookies and TLS |
| WebSocket | Own minimal RFC 6455 client on `QSslSocket` (`WebSocketClient`) | For the Proxmox console. QtWebSockets 5.5, the version Sailfish OS ships, cannot send request headers, so it can't pass the `PVEAuthCookie` or API token |
| SSH | **libssh** (LGPL), bundled as a shared library in `/usr/share/pvedeck/lib` | SSH into VMs (and optionally nodes). Shares the same terminal widget |
| Terminal emulation | **libvterm** (MIT), linked statically | Clean "feed bytes in, read cells out" API with no pty dependency. Better fit than fingerterm (GPL, tied to a pty) or qmltermwidget (Konsole-based, pty-centric) |
| Terminal rendering | Custom `QQuickPaintedItem` (move to a `QSGNode` later if it's slow) | Full control over fonts, colours, selection and pinch-zoom |
| Secrets | **Sailfish Secrets** (`libsailfishsecrets`, device-lock-protected collection owned by the app) | Stores passwords, API token secrets, SSH passwords and keys. Profiles (host, user, realm, fingerprint) go in QSettings. 2FA codes are never stored |
| Build | Sailfish SDK (`sfdk`) with the qmake template from the SDK | Targets aarch64, armv7hl and i486 (emulator) |

Sailjail permissions: `Internet`, plus whatever Secrets access requires (check against the SFOS 4.5+ permission list at M0).

Rejected option: xterm.js inside Sailfish WebView. It's quick to prototype, but self-signed certificates, auth cookies and on-screen keyboard input all get awkward inside Gecko, and it feels non-native.

---

## 2. Proxmox VE API: what we use

Base URL: `https://<host>:8006/api2/json`

### 2.1 Authentication and 2FA

1. `POST /access/ticket` with `username`, `password`, `realm` (pam / pve / ldap…)
2. Response without 2FA: `ticket`, `CSRFPreventionToken`, `username`, `cap`. Done.
3. Response with 2FA: `NeedTFA: 1` and a **partial ticket** (`PVE:!tfa!…`). The challenge is embedded in it and lists which methods are available (`totp`, `yubico`, `recovery`, `webauthn`).
4. Show the 2FA dialog with only the methods that are available, then:
   `POST /access/ticket` with `username`, `tfa-challenge=<partial ticket>`, `password=totp:<code>` (or `yubico:<otp>`, `recovery:<key>`).
5. Then on every call:
   - Cookie `PVEAuthCookie=<ticket>`
   - Header `CSRFPreventionToken: <token>` on POST/PUT/DELETE
6. **Renewal:** tickets live for 2 hours. About every 90 minutes, `POST /access/ticket` with `username` and `password=<current ticket>` (no 2FA needed again).
7. On HTTP 401, try to renew once. If that fails, send the user back to the login page.
8. **Alternative login: API token.** Header `Authorization: PVEAPIToken=user@realm!id=<uuid>`. No 2FA and no expiry, which is good for "stay logged in". (Check whether `vncwebsocket` accepts tokens on the PVE versions we target; it may need a ticket.)

WebAuthn/U2F: not supported in v1, because it isn't feasible on the device. Show "Use TOTP or a recovery key instead".

Challenge format (verified against proxmox-widget-toolkit `TfaWindow.js` and pve-manager `LoginWindow.js`): the partial ticket is `PVE:!tfa!<url-encoded JSON>:<time>::<sig>`. The JSON has `totp`, `yubico`, `webauthn`, `u2f`, and `recovery` (a list of unused key IDs; an empty list means all keys are used). Recovery keys look like `xxxx-xxxx-xxxx-xxxx` (lower-case hex). PVE 6's old TFA (`/access/tfa`) is not supported.

### 2.2 TLS

Proxmox uses a self-signed certificate by default. Use **trust on first use (TOFU)**:

- On the first `sslErrors`, show the SHA-256 fingerprint of the certificate and ask the user to accept it.
- Pin the fingerprint in the server profile.
- After that, silently accept only an exact fingerprint match.
- On a mismatch, show a loud warning.

### 2.3 Status

- `GET /cluster/resources?type=vm`: a single call returns every QEMU and LXC guest with `vmid`, `name`, `node`, `type`, `status`, `cpu`, `maxcpu`, `mem`, `maxmem`, `disk`, `uptime`, `template`, `tags`, `lock`. This is the main list.
- `GET /cluster/resources?type=node`: node status.
- `GET /nodes/{node}/{qemu|lxc}/{vmid}/status/current`: details.
- `GET /nodes/{node}/{type}/{vmid}/rrddata?timeframe=hour`: CPU/memory graphs (later milestone).
- `GET /nodes/{node}/qemu/{vmid}/agent/network-get-interfaces`: IP addresses, if the guest agent is running.
- Polling: every 5 seconds while a page is visible, paused while the app is in the background (only the cover keeps updating, every 60 seconds).

### 2.4 Power actions

`POST /nodes/{node}/{type}/{vmid}/status/{action}`

| Action | QEMU | LXC |
|---|---|---|
| start | ✓ | ✓ |
| shutdown (graceful) | ✓ | ✓ |
| stop (hard) | ✓ | ✓ |
| reboot | ✓ | ✓ |
| reset | ✓ | – |
| suspend / resume | ✓ | ✓ |

- Each call returns a **UPID**. Poll `GET /nodes/{node}/tasks/{upid}/status` until `status=stopped`, then show `exitstatus` (OK or an error) as a notification.
- Destructive actions (stop, reset, reboot) use `RemorsePopup` or `RemorseItem`.
- Only offer actions that match the current state. Hide them if the guest is a template or is `lock`ed.

### 2.5 Shell / console (xterm.js protocol)

1. `POST /nodes/{node}/lxc/{vmid}/termproxy`, `/nodes/{node}/qemu/{vmid}/termproxy` (VM needs a serial port: `serial0: socket`), or `/nodes/{node}/termproxy` (node shell). The response contains `port`, `ticket`, `user` and `upid`.
2. Open the WebSocket `wss://host:8006/api2/json/nodes/{node}/{type}/{vmid}/vncwebsocket?port=<port>&vncticket=<urlencoded ticket>` with the `PVEAuthCookie` cookie and the same TLS pinning.
3. First message: `<user>:<vncticket>\n`. The server answers `OK`.
4. Client → server messages:
   - `0:<len>:<data>` for keyboard input (len is a byte count of the UTF-8 data)
   - `1:<cols>:<rows>:` for resize
   - `2` as a ping, about every 30 seconds
5. Server → client messages are raw terminal output bytes, fed into libvterm.

VMs without a serial port: offer **SSH** instead (section 2.6), or the node shell. A graphical VNC console is **out of scope** (decided).

### 2.6 SSH to guests

- Target address: from the QEMU guest agent (`agent/network-get-interfaces`), or for LXC from `GET /nodes/{node}/lxc/{vmid}/interfaces`. The user can also enter or override it in the guest's settings.
- Auth: a password, or a private key. Keys can be generated on the device (ed25519) or imported. Both are stored in Sailfish Secrets.
- Host keys are TOFU-pinned and stored per guest. A changed key shows a loud warning.
- Per-guest SSH settings (user, port, address, auth) are remembered.
- The phone must be able to reach the guest's IP directly (same LAN or VPN). Proxmox doesn't proxy SSH.

---

## 3. Architecture

```
QML (Silica pages)
  │  properties / signals / Q_INVOKABLE
C++ core
  ├─ ServerStore        – profiles (host, port, realm, user, auth type, cert fingerprint) in QSettings
  ├─ SecretStore        – Sailfish Secrets wrapper: get/set/delete secret per profile/guest
  ├─ SessionManager     – holds one PveClient per logged-in server (several at once), exposes
  │                       a list of active sessions to QML, and aggregates counts for the cover
  ├─ PveClient          – QNAM wrapper: auth state machine, ticket renewal, CSRF, TLS pinning,
  │                       request queue, JSON → QVariant, error mapping
  ├─ ResourceModel      – QAbstractListModel over /cluster/resources (roles: vmid, name, node,
  │                       type, status, cpu, mem…); diff updates so the list doesn't jump
  ├─ ResourceFilterModel– QSortFilterProxyModel: search, type, status, node, tags
  ├─ TaskWatcher        – polls UPIDs, emits finished(ok, message)
  ├─ TerminalBackend    – interface: write(bytes), resize(cols,rows), signal dataReceived
  │   ├─ PveConsoleSession – termproxy call + WebSocketClient + protocol framing + reconnect
  │   └─ SshSession        – libssh on a worker thread: connect, host-key check, auth, PTY shell
  ├─ VTerm (libvterm)   – screen buffer, scrollback, damage callbacks
  └─ TerminalItem       – QQuickPaintedItem: draw cells, cursor, selection, pinch-zoom,
                          handles keyboard input (inputMethodQuery / inputMethodEvent)
```

### Pages (QML)

- **ServerListPage** (the start page). Logged-in servers are marked. Pull-down: Add server. Tap a server to log in or open it. Long-press: edit, log out or delete.
- **AllResourcesPage** (optional view): guests from every logged-in server, with a section per server/node.
- **ServerDialog**: host, port (8006), realm (fetched from `GET /access/domains` before login), user, auth type (password or API token).
- **LoginDialog**: password (prefilled from Secrets, with a "Remember password" switch), then **TfaDialog** if needed (method selector + code field, numeric keyboard for TOTP).
- **CertificateDialog**: shows the fingerprint, Accept or Reject.
- **ResourcesPage**: a `SilicaListView` with a section per node. Each row shows a status dot, icon (VM/CT), `vmid name`, and a CPU/memory mini bar. Search field. Pull-down: Refresh, Filter, Node shell, Tasks.
- **GuestPage**: status, uptime, CPU, memory, disk, IPs and tags. Pull-down: Start, Shutdown, Reboot, Stop, (Reset), Console, SSH. Later: RRD graphs.
- **SshSettingsDialog**: address (with IPs from the agent), port, user, auth (password or key).
- **SshKeysPage**: generate or import keys, and copy the public key.
- **ConsolePage**: full-screen `TerminalItem` plus an extra key row (Esc, Tab, Ctrl, Alt, arrows, `|`, `~`, `/`, `-`) and a font-size pinch gesture. Works in landscape. Copy and paste.
- **TasksPage**: `GET /cluster/tasks`, recent tasks with status.
- **Cover**: running/stopped counts summed over all logged-in servers. Cover action: refresh.

---

## 4. Milestones

Status: M0, M1 and M2 written, **not yet built or run** (no Sailfish SDK on the dev machine yet).

| # | Deliverable | Done when |
|---|---|---|
| M0 | Project skeleton: `sfdk` qmake project, pvedeck.spec, Sailjail permissions, icons, CI build for aarch64/armv7hl/i486 | Empty Silica app runs on device and emulator |
| M1 | `PveClient` + `SecretStore` + `SessionManager`: password login, TOFU TLS, ticket renewal, API token login, remember password, several sessions | Logs in to two test PVEs at once, ticket renews after 2 h, password restored from Secrets |
| M2 | 2FA: TOTP, recovery and Yubico flows, method picker from the challenge | Login with TOTP-enabled user works; wrong code shows an error and allows a retry |
| M3 | Resource list + filter + guest details + cover | All VMs/CTs visible with live status |
| M4 | Power actions + TaskWatcher + remorse + notifications | Start/stop/reboot etc. work and report the task result |
| M5 | Console: termproxy + WebSocket + libvterm + TerminalItem + key row | Interactive `htop`/`vim` in an LXC works, resize works on rotation |
| M6 | Node shell + VM serial console + tasks page | Same as M5 for node and VM serial |
| M6b | SSH: libssh bundle, `SshSession`, host-key TOFU, password and key auth, key generation, IP discovery from the guest agent / LXC interfaces | SSH into a VM without a serial port, using a key generated on the phone |
| M7 | Polish: RRD graphs, combined view of all servers, translations (en, sv), OpenRepos/Chum packaging | Released on OpenRepos |

M5 (the terminal) is the biggest risk. A spike can be done right after M1, in parallel, to de-risk it.

---

## 5. Test setup

- A Proxmox VE 8.x/9.x test instance (nested in a VM is fine), with:
  - a `pve`-realm user with TOTP enabled
  - a user with an API token
  - a user with limited permissions (to test 403 handling)
  - an LXC, a VM with `serial0: socket`, and a VM without it (with the guest agent and sshd running)
  - a second PVE instance for testing several sessions
- Unit tests (Qt Test) for: the TFA challenge parser, the console framing encoder and decoder, SecretStore (round trip), UPID parsing, and the model diffing.
- A mock server (small Python script) for the auth/2FA state machine, so tests can run without a real PVE.

---

## 6. Decisions

1. Distribution: **OpenRepos/Chum**. Bundled libraries (libvterm, libssh) and QtWebSockets are fine.
2. Passwords and keys: **Sailfish Secrets**.
3. **Several servers logged in at once.**
4. **No VNC.** VMs are reached through the serial console or SSH.

## 7. Still open

- Minimum Sailfish OS version (suggest 4.5+, needed for the current Sailjail and Secrets APIs).
- Should SSH also be offered to the Proxmox nodes themselves, as an alternative to the termproxy node shell?
